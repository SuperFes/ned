#include "Commands.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "AutoPair.h"
#include "BlankLineCleanup.h"
#include "BufferSave.h"
#include "Clipboard.h"
#include "CodeFold.h"
#include "EmbeddedDocuments.h"
#include "Fill.h"
#include "FillColumn.h"
#include "FormatOnSave.h"
#include "Indent.h"
#include "IndentStyle.h"
#include "InlineDiagnostics.h"
#include "Lsp/LspManager.h"
#include "Lsp/LspServerConfig.h"
#include "Markdown.h"
#include "Mode.h"
#include "ModeOverrides.h"
#include "Multibuffer.h"
#include "Org.h"
#include "PageScroll.h"
#include "ProjectRoot.h"
#include "ProjectSession.h"
#include "ProjectUndo.h"
#include "SnippetRegistry.h"
#include "TabWidth.h"
#include "Text/Grapheme.h"
#include "Text/ThreeWayMerge.h"
#include "Text/Utf8.h"
#include "ToolchainIncludePaths.h"
#include "Vcs/VcsRunner.h"

namespace ned::editor {

namespace {

    std::size_t LineContentEnd(const text::ITextStorage& content, std::size_t point) {
        const std::size_t line = content.ByteOffsetToLine(point);
        return (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    }

    // move-line-up/move-line-down/duplicate-line follow-up: a line's own
    // content, *not* including its trailing newline byte if it has one --
    // shared groundwork for all three, each of which needs to reconstruct
    // text around a line boundary without assuming every line ends in a
    // newline (only the buffer's very last line can lack one, but getting
    // that case right is exactly the subtlety here: naively concatenating
    // raw substrings across a swap can silently drop or duplicate a
    // newline right at that boundary).
    struct LineSpan {
        std::size_t start;
        std::size_t contentEnd; // excludes the trailing newline, if any
        bool        hasTrailingNewline;
    };

    LineSpan GetLineSpan(const text::ITextStorage& content, std::size_t line) {
        const std::size_t start              = content.LineToByteOffset(line);
        const bool        hasTrailingNewline = line + 1 < content.LineCount();
        const std::size_t contentEnd         = hasTrailingNewline ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
        return LineSpan{start, contentEnd, hasTrailingNewline};
    }

    // The bodies of move-line-up/move-line-down, extracted (tables slice 2)
    // so org-metaup/org-metadown can fall back to them when point isn't in
    // a table -- real Org's own M-up/M-down are exactly this kind of
    // context dispatch, same idiom as org-cycle's fold-else-align branch.
    // A no-op at the buffer's first/last line respectively, not an error.
    void MoveLineUp(text::Buffer& buffer) {
        const auto&       content = buffer.Content();
        const std::size_t line    = content.ByteOffsetToLine(buffer.Point());
        if (line == 0) {
            return;
        }
        const LineSpan    prev   = GetLineSpan(content, line - 1);
        const LineSpan    curr   = GetLineSpan(content, line);
        const std::size_t column = std::min(buffer.Point() - curr.start, curr.contentEnd - curr.start);

        const std::string prevText    = content.Substring(prev.start, curr.start - 1 - prev.start); // excludes prev's own newline
        const std::string currText    = content.Substring(curr.start, curr.contentEnd - curr.start);
        std::string       replacement = currText + "\n" + prevText;
        if (curr.hasTrailingNewline) {
            replacement += "\n";
        }

        buffer.ClearMark();
        buffer.DeleteRange(prev.start, curr.contentEnd + (curr.hasTrailingNewline ? 1 : 0) - prev.start);
        buffer.InsertAt(prev.start, replacement);
        buffer.SetPoint(prev.start + column);
    }

    void MoveLineDown(text::Buffer& buffer) {
        const auto&       content = buffer.Content();
        const std::size_t line    = content.ByteOffsetToLine(buffer.Point());
        if (line + 1 >= content.LineCount()) {
            return;
        }
        const LineSpan    curr   = GetLineSpan(content, line);
        const LineSpan    next   = GetLineSpan(content, line + 1);
        const std::size_t column = std::min(buffer.Point() - curr.start, curr.contentEnd - curr.start);

        const std::string currText    = content.Substring(curr.start, curr.contentEnd - curr.start);
        const std::string nextText    = content.Substring(next.start, next.contentEnd - next.start);
        std::string       replacement = nextText + "\n" + currText;
        if (next.hasTrailingNewline) {
            replacement += "\n";
        }

        buffer.ClearMark();
        buffer.DeleteRange(curr.start, next.contentEnd + (next.hasTrailingNewline ? 1 : 0) - curr.start);
        buffer.InsertAt(curr.start, replacement);
        buffer.SetPoint(curr.start + (nextText.size() + 1) + column);
    }

    std::size_t PageLineCount(std::size_t viewportHeight) {
        return std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<double>(viewportHeight) * PageScrollFraction()));
    }

    // Shift+Arrow follow-up: shared by the four shift-select-* commands
    // below. Sets a mark at the current point only if one isn't already
    // active, so a run of consecutive Shift+Arrow presses extends the same
    // selection from its original anchor rather than resetting the anchor
    // to point on every keystroke -- the standard GUI-editor "shift starts,
    // then extends" convention, layered on top of the same persistent-mark
    // model set-mark-command (C-SPC) already established rather than a
    // separate selection concept. A deliberate v1 scope cut, not an
    // oversight: real Emacs' own shift-select-mode additionally
    // deactivates a shift-started selection the instant any *non*-shifted
    // command runs (tracked via its own extra bit of state distinguishing
    // a shift-started mark from an explicitly C-SPC-set one); this doesn't
    // track that distinction, so a shift-extended region persists exactly
    // as long as any other mark would (until an editing command, mouse
    // click, kill-region, or kill-ring-save clears it) rather than
    // additionally being cleared by a plain, unshifted motion key.
    // Multi-cursor phase: wraps a command body so that, when secondary
    // cursors exist, it runs once per cursor via Buffer::ForEachCursor
    // (one undo group, per-cursor point/mark/goal-column swapped in) --
    // the command body itself stays completely multi-cursor-unaware.
    // Applied explicitly, per command, to the basic motion/editing set
    // below rather than globally: which commands are per-cursor is a real
    // decision, not a default to inherit silently. Narrowing stays
    // primary-only (still an open ROADMAP item); kill-ring/register/
    // rectangle commands round out multi-cursor support via KillPerCursor
    // below and BufferView's own per-cursor register/rectangle handling
    // (see the multi-cursor-round-2 follow-up comments there) instead of
    // this exact helper, since they need to compose a shared KillRing/
    // RegisterTable/RectangleClipboard entry across cursors rather than
    // just repeating an unaware fn per cursor.
    template <typename Fn>
    auto PerCursor(Fn fn) {
        return [fn](CommandContext& context) {
            if (!context.buffer.HasSecondaryCursors()) {
                fn(context);
                return;
            }
            context.buffer.ForEachCursor([&fn, &context] { fn(context); });
        };
    }

    // auto-pair-brackets-and-quotes follow-up: the single grapheme
    // immediately before/after point, or empty at a buffer boundary --
    // exactly what AutoPairQuery::charBefore/charAfter want. Shared by
    // self-insert-command and backward-delete-char rather than duplicating
    // the boundary lookup in both.
    std::string GraphemeBefore(const text::Buffer& buffer) {
        const std::size_t point = buffer.Point();
        const std::size_t start = text::PreviousGraphemeBoundary(buffer.Content(), point);
        return buffer.Content().Substring(start, point - start);
    }

    std::string GraphemeAfter(const text::Buffer& buffer) {
        const std::size_t point = buffer.Point();
        const std::size_t end   = text::NextGraphemeBoundary(buffer.Content(), point);
        return buffer.Content().Substring(point, end - point);
    }

    // auto-pair-brackets-and-quotes follow-up: the SyntaxClass covering the
    // grapheme immediately before point -- what DecideSelfInsert's
    // classAtPoint wants for the "already inside a string/comment" quote
    // suppression. Reads it off the character *before* point (not point
    // itself, which sits in the gap between two characters) -- the same
    // "what color would this text already render as" question BufferView's
    // own per-character rendering asks, just reused for a decision instead
    // of a paint. point == 0 or no highlighter configured both mean
    // "unknown," same as SyntaxClass's own default -- deliberately never
    // treated as "definitely not in a string," since a false negative here
    // only costs a missed suppression (still-plain pairing), not a wrong one.
    //
    // Deliberately only called for a symmetric (quote-like) opener -- see
    // self-insert-command's own call site -- since Mode::highlight is a real
    // (if now incrementally-cached, see IncrementalParseCache) parse over the
    // buffer's full text, not something to pay on every keystroke.
    SyntaxClass SyntaxClassAtPoint(const Mode* mode, const text::Buffer& buffer, std::size_t point) {
        if (!mode || !mode->highlight || point == 0) {
            return SyntaxClass::Default;
        }
        const std::vector<HighlightSpan> spans  = mode->highlight(buffer.Text());
        const std::size_t                probe  = point - 1;
        SyntaxClass                      winner = SyntaxClass::Default;
        for (const HighlightSpan& span : spans) {
            if (span.startByte <= probe && probe < span.endByte) {
                winner = span.syntaxClass; // later spans win on overlap -- Mode.h's own documented rule
            }
        }
        return winner;
    }

    // Emacs-keymap-round-2 follow-up (kill-append): the small, fixed family
    // of commands that participate in kill-append -- whether *this* kill
    // appends/prepends to the kill ring's most recent entry depends only on
    // whether the immediately preceding command was one of these (Emacs'
    // own `last-command` check), regardless of that prior kill's own
    // direction. kill-ring-save is deliberately excluded: it doesn't
    // delete anything, and real repeated-M-w use is rare enough that
    // folding it in isn't worth the surprise of a non-destructive copy
    // silently growing a kill entry.
    bool IsKillCommand(const std::string& name) {
        return name == "kill-line" || name == "kill-region" || name == "kill-word" || name == "backward-kill-word" ||
               name == "zap-to-char";
    }

    // multi-cursor-round-2 follow-up: like PerCursor, but for a kill/copy
    // that must land in one shared KillRing entry (one piece per cursor)
    // rather than each cursor acting in isolation. fn returns the killed/
    // copied text for the current cursor, or nullopt if there was nothing
    // to kill there (e.g. kill-line at buffer end, kill-region/
    // kill-ring-save with no mark) -- contributes an empty piece rather
    // than being skipped, so piece count always matches cursor count for a
    // later 1:1 yank. Nothing is pushed to the ring at all if no cursor
    // killed anything (kill-region/kill-ring-save's existing "no mark, no
    // ring push" no-op, generalized to every cursor).
    //
    // Emacs-keymap-round-2 follow-up (kill-append): prependOnAppend, when
    // non-null, is read *after* fn returns (so fn itself can compute it,
    // e.g. kill-region deciding forward-vs-backward from the region's own
    // orientation) and only ever consulted in the single-cursor path --
    // multi-cursor kill-append is a deliberate v1 cut, same "no sensible
    // single append target" reasoning KillRing::AppendToCurrent's own doc
    // comment gives for a multi-piece entry.
    template <typename Fn>
    void KillPerCursor(CommandContext& context, Fn fn, bool* prependOnAppend = nullptr) {
        if (!context.buffer.HasSecondaryCursors()) {
            if (std::optional<std::string> text = fn(context)) {
                if (IsKillCommand(context.lastCommand)) {
                    context.killRing.AppendToCurrent(std::move(*text), prependOnAppend != nullptr && *prependOnAppend);
                }
                else {
                    context.killRing.KillPieces({std::move(*text)});
                }
                CopyToSystemClipboard(context.killRing.Current());
            }
            return;
        }
        std::vector<std::string> pieces;
        bool                     any = false;
        context.buffer.ForEachCursor([&] {
            std::optional<std::string> text = fn(context);
            any |= text.has_value();
            pieces.push_back(std::move(text).value_or(std::string()));
        });
        if (any) {
            context.killRing.KillPieces(std::move(pieces));
            CopyToSystemClipboard(context.killRing.Current());
        }
    }

    // Multi-cursor phase: [start, end) of the word point sits inside or
    // immediately after, or nullopt when point touches no word at all.
    // Same ASCII-word classification Buffer's own word motion uses
    // (deliberately not Unicode-aware, matching that documented cut) --
    // duplicated here rather than exposed from Buffer.cpp, the usual
    // "not worth a new seam for something this small" call.
    std::optional<std::pair<std::size_t, std::size_t>> WordRegionAt(const text::ITextStorage& rope, std::size_t point) {
        const auto isWord = [](char32_t codepoint) {
            return (codepoint >= U'a' && codepoint <= U'z') || (codepoint >= U'A' && codepoint <= U'Z') ||
                   (codepoint >= U'0' && codepoint <= U'9') || codepoint == U'_';
        };

        std::size_t start = std::min(point, rope.ByteLength());
        while (start > 0) {
            const std::size_t previous = rope.PreviousCodepointBoundary(start);
            if (!isWord(rope.CodepointAt(previous).codepoint)) {
                break;
            }
            start = previous;
        }
        std::size_t end = std::min(point, rope.ByteLength());
        while (end < rope.ByteLength()) {
            const auto decoded = rope.CodepointAt(end);
            if (!isWord(decoded.codepoint)) {
                break;
            }
            end += decoded.byteLength;
        }
        if (start == end) {
            return std::nullopt;
        }
        return std::pair{start, end};
    }

    // snippet-expansion follow-up: if the word ending exactly at point is a
    // registered snippet trigger for the buffer's language (mode-specific
    // tier first, then the "" global tier -- see Editor/SnippetRegistry.h),
    // requests the expansion and reports true. Point must sit at the word's
    // own end -- TAB mid-word or after whitespace never triggers, so plain
    // indenting keeps working everywhere a trigger doesn't exactly match.
    // A null context.mode (headless) looks up global snippets only.
    bool TrySnippetTrigger(CommandContext& context) {
        const auto region = WordRegionAt(context.buffer.Content(), context.buffer.Point());
        if (!region || region->second != context.buffer.Point()) {
            return false;
        }
        const std::string trigger =
            context.buffer.Content().Substring(region->first, region->second - region->first);
        const std::string languageKey = context.mode != nullptr ? LanguageKeyForMode(*context.mode) : "";
        const auto        body        = SnippetBodyForTrigger(languageKey, trigger);
        if (!body) {
            return false;
        }
        context.snippetExpansion   = CommandContext::SnippetExpansionRequest{region->first, region->second, *body};
        context.interactiveRequest = InteractiveRequest::SnippetExpand;
        return true;
    }

    // The byte offset where a cursor's selection starts (its region's low
    // end), or its point when it has no mark -- what select-next-occurrence
    // compares candidate matches against so it never re-adds a cursor at an
    // occurrence one already owns.
    std::size_t CursorSelectionStart(std::size_t point, std::optional<std::size_t> mark) {
        return mark ? std::min(point, *mark) : point;
    }

    void EnsureMarkForShiftSelect(text::Buffer& buffer) {
        if (!buffer.HasMark()) {
            buffer.SetMark(buffer.Point());
        }
    }

    // ned-init-project follow-up. .ned/session.json is per-machine window/
    // buffer-layout state (see ProjectSession.h's own header comment), not
    // shared project config like .ned/init.janet -- committing it would just
    // have each developer's local layout clobber the next one's on every
    // commit. Returns true if the entry was newly appended (for the
    // command's own status message); false if there's no .gitignore to
    // append to, or it already ignores the entry. Deliberately never
    // creates a .gitignore from scratch -- that's a bigger, more
    // presumptuous step than this one-shot command should take silently.
    bool AppendSessionJsonToGitignore(const std::filesystem::path& root) {
        const std::filesystem::path gitignorePath = root / ".gitignore";
        std::error_code             ec;
        if (!std::filesystem::is_regular_file(gitignorePath, ec)) {
            return false;
        }

        const char* kEntry = ".ned/session.json";
        std::string existing;
        {
            std::ifstream file(gitignorePath, std::ios::binary);
            existing.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        std::istringstream lines(existing);
        std::string        line;
        while (std::getline(lines, line)) {
            if (line == kEntry) {
                return false; // already ignored
            }
        }

        std::ofstream file(gitignorePath, std::ios::binary | std::ios::app);
        if (!existing.empty() && existing.back() != '\n') {
            file << '\n';
        }
        file << "# ned per-machine session state (window layout, open buffers)\n"
             << kEntry << '\n';
        return true;
    }

    // project-undo follow-up: shared by the undo/redo commands' delegation
    // to ProjectUndoManager below.
    std::string FormatProjectUndoMessage(const char* verb, const ProjectUndoOutcome& outcome) {
        std::string message = std::string(verb) + " " + outcome.description;
        if (!outcome.divergedNames.empty()) {
            message += " (" + std::to_string(outcome.appliedCount) + "/" + std::to_string(outcome.totalCount) + " files -- ";
            for (std::size_t i = 0; i < outcome.divergedNames.size(); ++i) {
                message += (i == 0 ? "" : ", ") + outcome.divergedNames[i];
            }
            message += " edited separately since)";
        }
        return message;
    }

} // namespace

void RegisterBuiltinCommands(CommandRegistry& registry) {
    // Deliberately does not ClearMark(): with set-mark-command (C-SPC) now
    // keyboard-reachable, the mark must survive ordinary keyboard motion so
    // kill-region/kill-ring-save can act on a real, moved-to region -- see
    // set-mark-command's own registration below. Only an explicit mouse
    // click (BufferView::OnMouseEvent) or kill-region/kill-ring-save
    // themselves clear it now; mouse-drag never went through these commands
    // at all (BufferView::OnMouseEvent calls Buffer::SetPoint directly).
    registry.Register("forward-char", "Move point forward one grapheme cluster.",
                      PerCursor([](CommandContext& context) { context.buffer.MoveForward(); }));

    registry.Register("backward-char", "Move point backward one grapheme cluster.",
                      PerCursor([](CommandContext& context) { context.buffer.MoveBackward(); }));

    registry.Register("next-line", "Move point down one line, preserving column across a run.",
                      PerCursor([](CommandContext& context) { context.buffer.MoveToNextLine(TabWidth()); }));

    registry.Register("previous-line", "Move point up one line, preserving column across a run.",
                      PerCursor([](CommandContext& context) { context.buffer.MoveToPreviousLine(TabWidth()); }));

    // Shift+Arrow follow-up -- see EnsureMarkForShiftSelect's own comment
    // above for the selection model and its documented scope cut.
    registry.Register("shift-select-forward-char", "Move point forward one grapheme cluster, extending the selection.",
                      [](CommandContext& context) {
                          EnsureMarkForShiftSelect(context.buffer);
                          context.buffer.MoveForward();
                      });
    registry.Register("shift-select-backward-char", "Move point backward one grapheme cluster, extending the selection.",
                      [](CommandContext& context) {
                          EnsureMarkForShiftSelect(context.buffer);
                          context.buffer.MoveBackward();
                      });
    registry.Register("shift-select-next-line", "Move point down one line, extending the selection.", [](CommandContext& context) {
        EnsureMarkForShiftSelect(context.buffer);
        context.buffer.MoveToNextLine(TabWidth());
    });
    registry.Register("shift-select-previous-line", "Move point up one line, extending the selection.", [](CommandContext& context) {
        EnsureMarkForShiftSelect(context.buffer);
        context.buffer.MoveToPreviousLine(TabWidth());
    });

    registry.Register("forward-word", "Move point forward one word.",
                      PerCursor([](CommandContext& context) { context.buffer.MoveForwardWord(); }));

    registry.Register("backward-word", "Move point backward one word.",
                      PerCursor([](CommandContext& context) { context.buffer.MoveBackwardWord(); }));

    // Emacs-keymap-round-2 follow-up.
    registry.Register("forward-sentence", "Move point forward to the end of the current/next sentence.",
                      PerCursor([](CommandContext& context) { context.buffer.MoveForwardSentence(); }));

    registry.Register("backward-sentence", "Move point backward to the start of the current/previous sentence.",
                      PerCursor([](CommandContext& context) { context.buffer.MoveBackwardSentence(); }));

    registry.Register("scroll-page-down", "Move point down by roughly a page.", [](CommandContext& context) {
        context.buffer.MoveDownLines(PageLineCount(context.viewportHeight), TabWidth());
    });

    registry.Register("scroll-page-up", "Move point up by roughly a page.", [](CommandContext& context) {
        context.buffer.MoveUpLines(PageLineCount(context.viewportHeight), TabWidth());
    });

    registry.Register("beginning-of-buffer", "Move point to the start of the buffer.",
                      [](CommandContext& context) { context.buffer.SetPoint(0); });

    registry.Register("end-of-buffer", "Move point to the end of the buffer.",
                      [](CommandContext& context) { context.buffer.SetPoint(context.buffer.Content().ByteLength()); });

    // Editing commands (as opposed to the plain-motion commands above)
    // still ClearMark() -- an existing mark should deactivate once the
    // buffer is actually edited out from under it, the same "mark
    // survives motion, not editing" split real Emacs makes. Motion-only
    // commands are the exception, not editing ones.
    registry.Register("delete-char", "Delete the grapheme cluster at point.", PerCursor([](CommandContext& context) {
                          context.buffer.ClearMark();
                          context.buffer.DeleteForwardAtPoint();
                      }));

    registry.Register("backward-delete-char", "Delete the grapheme cluster before point.", PerCursor([](CommandContext& context) {
                          context.buffer.ClearMark();
                          // auto-pair-brackets-and-quotes follow-up: backspacing
                          // between an empty pair ("(|)", "\"|\"") removes both
                          // sides as one edit instead of leaving the lone closer
                          // behind. context.mode nullptr falls back to
                          // DefaultAutoPairs(), same reasoning as self-insert-command.
                          if (AutoPairEnabled()) {
                              const std::string                         before = GraphemeBefore(context.buffer);
                              const std::string                         after  = GraphemeAfter(context.buffer);
                              const std::vector<std::pair<char, char>>& pairs =
                                  context.mode ? context.mode->autoPairs : DefaultAutoPairs();
                              if (ShouldDeleteAdjacentPair(before, after, pairs)) {
                                  const std::size_t start = context.buffer.Point() - before.size();
                                  context.buffer.DeleteRange(start, before.size() + after.size());
                                  return;
                              }
                          }
                          context.buffer.DeleteBackwardAtPoint();
                      }));

    registry.Register("beginning-of-line", "Move point to the beginning of the current line.", PerCursor([](CommandContext& context) {
                          const auto&       content = context.buffer.Content();
                          const std::size_t line    = content.ByteOffsetToLine(context.buffer.Point());
                          context.buffer.SetPoint(content.LineToByteOffset(line));
                      }));

    registry.Register("end-of-line", "Move point to the end of the current line.", PerCursor([](CommandContext& context) {
                          const std::size_t target = LineContentEnd(context.buffer.Content(), context.buffer.Point());
                          context.buffer.SetPoint(target);
                      }));

    registry.Register("kill-line", "Kill from point to the end of the line, or the newline if already there.", [](CommandContext& context) {
        KillPerCursor(context, [](CommandContext& context) -> std::optional<std::string> {
            context.buffer.ClearMark();
            const std::size_t point   = context.buffer.Point();
            const auto&       content = context.buffer.Content();
            const std::size_t lineEnd = LineContentEnd(content, point);

            if (point < lineEnd) {
                return context.buffer.DeleteRange(point, lineEnd - point);
            }
            if (lineEnd < content.ByteLength()) {
                return context.buffer.DeleteRange(point, 1);
            }
            return std::nullopt;
        });
    });

    registry.Register("yank", "Insert the most recent kill-ring entry at point.", [](CommandContext& context) {
        // Emacs' own interprogram-paste-function convention: something
        // copied in another app since the last kill-ring push takes
        // priority over ned's own kill ring, so it becomes what yank
        // inserts. Pushed as a fresh entry (not consulted again by
        // yank-pop, which only ever cycles pre-existing entries) only when
        // it actually differs from the current head -- otherwise every
        // yank of an unchanged system clipboard would keep growing the
        // ring with duplicates.
        if (std::optional<std::string> pasted = PasteFromSystemClipboard()) {
            if (context.killRing.Empty() || *pasted != context.killRing.Current()) {
                context.killRing.Kill(std::move(*pasted));
            }
        }
        // Unconditional, matching the pre-multi-cursor behavior exactly:
        // yank always clears an existing mark, even on an empty kill ring.
        context.buffer.ClearMark();
        if (context.killRing.Empty()) {
            return;
        }
        if (!context.buffer.HasSecondaryCursors()) {
            context.buffer.InsertAtPoint(context.killRing.Current());
            return;
        }
        // multi-cursor-round-2 follow-up: distribute 1:1 in cursor order
        // when the entry's own piece count matches how many cursors are
        // live right now, else fall back to the whole joined blob at every
        // cursor (KillRing::Current()'s existing single-cursor meaning).
        const std::vector<std::string>& pieces      = context.killRing.CurrentPieces();
        const std::size_t               cursorCount = 1 + context.buffer.SecondaryCursors().size();
        const bool                      perCursor   = pieces.size() == cursorCount;
        std::size_t                     i           = 0;
        context.buffer.ForEachCursor([&] {
            context.buffer.ClearMark();
            context.buffer.InsertAtPoint(perCursor ? pieces[i] : context.killRing.Current());
            ++i;
        });
    });

    // Emacs' C-SPC C-SPC idiom: a second press with the (still-empty) mark
    // at point deactivates it, so an accidental mark is cancelable from the
    // same key that set it -- keyboard-quit (C-g) stays the general cancel.
    // A press after point has moved re-anchors the region at point instead,
    // matching real set-mark-command's own behavior.
    registry.Register("set-mark-command", "Set the mark at point, or deactivate it when pressed again in place.",
                      [](CommandContext& context) {
                          if (context.buffer.HasMark() && context.buffer.Mark() == context.buffer.Point()) {
                              context.buffer.ClearMark();
                              if (context.message) {
                                  *context.message = "Mark deactivated";
                              }
                              return;
                          }
                          context.buffer.SetMark(context.buffer.Point());
                          if (context.message) {
                              *context.message = "Mark set";
                          }
                      });

    // A no-op without a mark, matching kill-line's own "nothing to do" silence
    // rather than surfacing a status message for what's a routine, frequent
    // no-op (pressing C-w before ever setting a mark).
    registry.Register("kill-region", "Kill (cut) the region between point and mark into the kill ring.", [](CommandContext& context) {
        // kill-append follow-up: prepend rather than append when the
        // region sat *before* point (point == region end) -- mirrors real
        // Emacs' kill-region deciding kill-append's own before-p from the
        // same point-vs-region-end comparison.
        bool prepend = false;
        KillPerCursor(
            context,
            [&prepend](CommandContext& context) -> std::optional<std::string> {
                if (!context.buffer.HasMark()) {
                    return std::nullopt;
                }
                const auto [start, end] = context.buffer.Region();
                prepend                 = context.buffer.Point() == end;
                std::string text        = context.buffer.DeleteRange(start, end - start);
                context.buffer.ClearMark();
                return text;
            },
            &prepend);
    });

    registry.Register("exchange-point-and-mark", "Swap point and mark.", [](CommandContext& context) {
        if (!context.buffer.HasMark()) {
            return;
        }
        const std::size_t oldPoint = context.buffer.Point();
        const std::size_t oldMark  = context.buffer.Mark();
        context.buffer.SetPoint(oldMark);
        context.buffer.SetMark(oldPoint);
    });

    // Emacs-coverage follow-up: the classic kill/word/whitespace/case
    // vocabulary. All of these compose existing Buffer/KillRing primitives;
    // multi-edit ones batch through Begin/EndUndoGroup so each undoes as a
    // single step. Editing ones ClearMark() per the editing-vs-motion split
    // documented above; word-classification stays the same ASCII-only cut
    // Buffer's own word motion makes.

    registry.Register("yank-pop", "Replace a just-yanked entry with the next-older kill-ring entry.", [](CommandContext& context) {
        // Emacs' own precondition: only meaningful immediately after yank
        // (or a previous yank-pop) -- context.lastCommand is the Dispatcher's
        // last-command tracking, added for exactly this.
        if (context.lastCommand != "yank" && context.lastCommand != "yank-pop") {
            if (context.message) {
                *context.message = "Previous command was not a yank";
            }
            return;
        }
        if (context.killRing.Empty()) {
            return;
        }
        if (!context.buffer.HasSecondaryCursors()) {
            const std::string previous = context.killRing.Current();
            const std::string next     = context.killRing.YankPop();
            if (context.buffer.Point() < previous.size()) {
                return; // the yanked text can't be where we expect; leave the buffer alone
            }
            context.buffer.ClearMark();
            context.buffer.BeginUndoGroup();
            context.buffer.DeleteRange(context.buffer.Point() - previous.size(), previous.size());
            context.buffer.InsertAtPoint(next);
            context.buffer.EndUndoGroup();
            return;
        }
        // multi-cursor-round-2 follow-up: previousPieces/previousBlob
        // captured *before* advancing the ring (what the prior yank/
        // yank-pop actually left before each cursor), nextPieces/nextBlob
        // *after* -- same per-cursor-vs-whole-blob rule yank itself uses,
        // applied independently to what's deleted and what's inserted. If
        // the live cursor count changed since the previous yank/yank-pop,
        // the per-cursor delete length can mismatch what's really there;
        // the same point-vs-length bail-out guard the single-cursor body
        // above already relies on protects against corruption there too
        // (skips that cursor rather than deleting the wrong span).
        const std::size_t              cursorCount     = 1 + context.buffer.SecondaryCursors().size();
        const std::vector<std::string> previousPieces  = context.killRing.CurrentPieces();
        const std::string              previousBlob    = context.killRing.Current();
        const bool                     perCursorDelete = previousPieces.size() == cursorCount;

        const std::string&              nextBlob        = context.killRing.YankPop();
        const std::vector<std::string>& nextPieces      = context.killRing.CurrentPieces();
        const bool                      perCursorInsert = nextPieces.size() == cursorCount;

        context.buffer.BeginUndoGroup();
        std::size_t i = 0;
        context.buffer.ForEachCursor([&] {
            const std::size_t  point    = context.buffer.Point();
            const std::string& toDelete = perCursorDelete ? previousPieces[i] : previousBlob;
            if (point >= toDelete.size()) {
                context.buffer.ClearMark();
                context.buffer.DeleteRange(point - toDelete.size(), toDelete.size());
                context.buffer.InsertAtPoint(perCursorInsert ? nextPieces[i] : nextBlob);
            }
            ++i;
        });
        context.buffer.EndUndoGroup();
    });

    registry.Register("kill-word", "Kill from point to the end of the next word.", [](CommandContext& context) {
        context.buffer.ClearMark();
        const std::size_t start = context.buffer.Point();
        context.buffer.MoveForwardWord();
        const std::size_t end = context.buffer.Point();
        if (end > start) {
            std::string text = context.buffer.DeleteRange(start, end - start);
            if (IsKillCommand(context.lastCommand)) {
                context.killRing.AppendToCurrent(std::move(text), /*prepend=*/false);
            }
            else {
                context.killRing.Kill(std::move(text));
            }
            CopyToSystemClipboard(context.killRing.Current());
        }
    });

    registry.Register("backward-kill-word", "Kill from the start of the previous word to point.", [](CommandContext& context) {
        context.buffer.ClearMark();
        const std::size_t end = context.buffer.Point();
        context.buffer.MoveBackwardWord();
        const std::size_t start = context.buffer.Point();
        if (end > start) {
            std::string text = context.buffer.DeleteRange(start, end - start);
            if (IsKillCommand(context.lastCommand)) {
                context.killRing.AppendToCurrent(std::move(text), /*prepend=*/true);
            }
            else {
                context.killRing.Kill(std::move(text));
            }
            CopyToSystemClipboard(context.killRing.Current());
        }
    });

    registry.Register("mark-whole-buffer", "Put point at the beginning and mark at the end of the buffer.",
                      [](CommandContext& context) {
                          context.buffer.SetPoint(0);
                          context.buffer.SetMark(context.buffer.Content().ByteLength());
                      });

    registry.Register("transpose-chars", "Interchange the graphemes around point, moving forward.", [](CommandContext& context) {
        auto&             buffer  = context.buffer;
        const auto&       content = buffer.Content();
        const std::size_t point   = buffer.Point();
        // Emacs' end-of-line special case: transpose the two graphemes
        // *before* point instead of the pair around it.
        const bool  atLineEnd = point == LineContentEnd(content, point);
        std::size_t first;  // start of the earlier grapheme
        std::size_t middle; // boundary between the two
        std::size_t last;   // end of the later grapheme
        if (atLineEnd) {
            if (point == 0) {
                return;
            }
            last   = point;
            middle = text::PreviousGraphemeBoundary(content, last);
            if (middle == 0) {
                return;
            }
            first = text::PreviousGraphemeBoundary(content, middle);
        }
        else {
            if (point == 0) {
                return;
            }
            first  = text::PreviousGraphemeBoundary(content, point);
            middle = point;
            last   = text::NextGraphemeBoundary(content, point);
        }
        if (content.ByteOffsetToLine(first) != content.ByteOffsetToLine(last)) {
            return; // never drag a grapheme across a newline
        }
        const std::string earlier = content.Substring(first, middle - first);
        const std::string later   = content.Substring(middle, last - middle);
        buffer.ClearMark();
        buffer.BeginUndoGroup();
        buffer.DeleteRange(first, last - first);
        buffer.InsertAt(first, later + earlier);
        buffer.EndUndoGroup();
        buffer.SetPoint(last);
    });

    registry.Register("transpose-words", "Interchange the words around point, moving forward.", [](CommandContext& context) {
        auto&             buffer   = context.buffer;
        const auto&       content  = buffer.Content();
        const std::size_t original = buffer.Point();
        // Same region derivation real transpose-words gets from its
        // forward-word/backward-word probes: the word ending at-or-after
        // point and the word before it.
        buffer.MoveForwardWord();
        const std::size_t end2 = buffer.Point();
        buffer.MoveBackwardWord();
        const std::size_t start2 = buffer.Point();
        buffer.MoveBackwardWord();
        const std::size_t start1 = buffer.Point();
        buffer.MoveForwardWord();
        const std::size_t end1 = buffer.Point();
        if (!(start1 < end1 && end1 <= start2 && start2 < end2)) {
            buffer.SetPoint(original); // fewer than two distinct words here
            return;
        }
        const std::string firstWord  = content.Substring(start1, end1 - start1);
        const std::string separator  = content.Substring(end1, start2 - end1);
        const std::string secondWord = content.Substring(start2, end2 - start2);
        buffer.ClearMark();
        buffer.BeginUndoGroup();
        buffer.DeleteRange(start1, end2 - start1);
        buffer.InsertAt(start1, secondWord + separator + firstWord);
        buffer.EndUndoGroup();
        buffer.SetPoint(end2);
    });

    // upcase/downcase/capitalize-word share one shape: transform from point
    // to the end of the next word and leave point there, Emacs-style. ASCII
    // case only, matching the word classification's own documented cut.
    const auto registerCaseCommand = [&registry](const char* name, const char* doc, auto transform) {
        registry.Register(name, doc, [transform](CommandContext& context) {
            auto&             buffer = context.buffer;
            const std::size_t start  = buffer.Point();
            buffer.MoveForwardWord();
            const std::size_t end = buffer.Point();
            if (end <= start) {
                return;
            }
            buffer.ClearMark();
            buffer.BeginUndoGroup();
            std::string text = buffer.DeleteRange(start, end - start);
            transform(text);
            buffer.InsertAt(start, text);
            buffer.EndUndoGroup();
            buffer.SetPoint(end);
        });
    };
    const auto isAsciiAlpha = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); };
    const auto toUpper      = [](char ch) { return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch; };
    const auto toLower      = [](char ch) { return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch; };
    registerCaseCommand("upcase-word", "Uppercase from point to the end of the next word, moving over it.",
                        [toUpper](std::string& text) {
                            for (char& ch : text) {
                                ch = toUpper(ch);
                            }
                        });
    registerCaseCommand("downcase-word", "Lowercase from point to the end of the next word, moving over it.",
                        [toLower](std::string& text) {
                            for (char& ch : text) {
                                ch = toLower(ch);
                            }
                        });
    registerCaseCommand("capitalize-word", "Capitalize from point to the end of the next word, moving over it.",
                        [isAsciiAlpha, toUpper, toLower](std::string& text) {
                            bool atWordStart = true;
                            for (char& ch : text) {
                                if (isAsciiAlpha(ch)) {
                                    ch          = atWordStart ? toUpper(ch) : toLower(ch);
                                    atWordStart = false;
                                }
                                else {
                                    atWordStart = true;
                                }
                            }
                        });

    registry.Register("open-line", "Insert a newline after point, leaving point in place.", PerCursor([](CommandContext& context) {
                          context.buffer.ClearMark();
                          const std::size_t point = context.buffer.Point();
                          context.buffer.InsertAtPoint("\n");
                          context.buffer.SetPoint(point);
                      }));

    registry.Register("just-one-space", "Replace the whitespace around point with a single space.", [](CommandContext& context) {
        auto&       buffer  = context.buffer;
        const auto& content = buffer.Content();
        std::size_t start   = buffer.Point();
        while (start > 0) {
            const std::size_t previous = content.PreviousCodepointBoundary(start);
            const char32_t    cp       = content.CodepointAt(previous).codepoint;
            if (cp != U' ' && cp != U'\t') {
                break;
            }
            start = previous;
        }
        std::size_t end = buffer.Point();
        while (end < content.ByteLength()) {
            const auto decoded = content.CodepointAt(end);
            if (decoded.codepoint != U' ' && decoded.codepoint != U'\t') {
                break;
            }
            end += decoded.byteLength;
        }
        buffer.ClearMark();
        buffer.BeginUndoGroup();
        buffer.DeleteRange(start, end - start);
        buffer.InsertAt(start, " ");
        buffer.EndUndoGroup();
        buffer.SetPoint(start + 1);
    });

    registry.Register("delete-indentation", "Join this line to the previous one, with one space at the join.",
                      [](CommandContext& context) {
                          auto&             buffer  = context.buffer;
                          const auto&       content = buffer.Content();
                          const std::size_t line    = content.ByteOffsetToLine(buffer.Point());
                          if (line == 0) {
                              return;
                          }
                          const std::size_t lineStart = content.LineToByteOffset(line);
                          // The join range: previous line's trailing spaces/tabs,
                          // the newline itself, and this line's leading indentation.
                          std::size_t start = lineStart - 1; // the newline byte
                          while (start > 0) {
                              const std::size_t previous = content.PreviousCodepointBoundary(start);
                              const char32_t    cp       = content.CodepointAt(previous).codepoint;
                              if (cp != U' ' && cp != U'\t') {
                                  break;
                              }
                              start = previous;
                          }
                          std::size_t end = lineStart;
                          while (end < content.ByteLength()) {
                              const auto decoded = content.CodepointAt(end);
                              if (decoded.codepoint != U' ' && decoded.codepoint != U'\t') {
                                  break;
                              }
                              end += decoded.byteLength;
                          }
                          buffer.ClearMark();
                          buffer.BeginUndoGroup();
                          buffer.DeleteRange(start, end - start);
                          buffer.InsertAt(start, " ");
                          buffer.EndUndoGroup();
                          buffer.SetPoint(start);
                      });

    registry.Register("back-to-indentation", "Move point to this line's first non-whitespace character.",
                      PerCursor([](CommandContext& context) {
                          auto&       buffer  = context.buffer;
                          const auto& content = buffer.Content();
                          std::size_t target  = content.LineToByteOffset(content.ByteOffsetToLine(buffer.Point()));
                          while (target < content.ByteLength()) {
                              const auto decoded = content.CodepointAt(target);
                              if (decoded.codepoint != U' ' && decoded.codepoint != U'\t') {
                                  break;
                              }
                              target += decoded.byteLength;
                          }
                          buffer.SetPoint(target);
                      }));

    registry.Register("delete-blank-lines", "On a blank line, delete surrounding blank lines (leaving one); otherwise delete any blank lines following this one.",
                      [](CommandContext& context) {
                          auto&       buffer  = context.buffer;
                          const auto& content = buffer.Content();
                          const auto  isBlank = [&content](std::size_t line) {
                              const LineSpan span = GetLineSpan(content, line);
                              std::size_t    at   = span.start;
                              while (at < span.contentEnd) {
                                  const auto decoded = content.CodepointAt(at);
                                  if (decoded.codepoint != U' ' && decoded.codepoint != U'\t') {
                                      return false;
                                  }
                                  at += decoded.byteLength;
                              }
                              return true;
                          };
                          // Bytes spanning full lines first..last inclusive,
                          // including last's trailing newline when it has one.
                          const auto lineRunRange = [&content](std::size_t first, std::size_t last) {
                              const std::size_t start = content.LineToByteOffset(first);
                              const std::size_t end =
                                  last + 1 < content.LineCount() ? content.LineToByteOffset(last + 1) : content.ByteLength();
                              return std::pair{start, end};
                          };
                          const std::size_t line = content.ByteOffsetToLine(buffer.Point());
                          if (!isBlank(line)) {
                              // Delete the run of blank lines after this one, if any.
                              std::size_t last = line;
                              while (last + 1 < content.LineCount() && isBlank(last + 1)) {
                                  ++last;
                              }
                              if (last == line) {
                                  return;
                              }
                              const auto [start, end] = lineRunRange(line + 1, last);
                              buffer.ClearMark();
                              if (end > start) {
                                  buffer.DeleteRange(start, end - start);
                              }
                              return;
                          }
                          std::size_t first = line;
                          while (first > 0 && isBlank(first - 1)) {
                              --first;
                          }
                          std::size_t last = line;
                          while (last + 1 < content.LineCount() && isBlank(last + 1)) {
                              ++last;
                          }
                          buffer.ClearMark();
                          if (first == last) {
                              // An isolated blank line is deleted outright.
                              const auto [start, end] = lineRunRange(first, last);
                              if (end > start) {
                                  buffer.DeleteRange(start, end - start);
                              }
                              buffer.SetPoint(std::min(start, buffer.Content().ByteLength()));
                              return;
                          }
                          // A run collapses to one blank line, point on it.
                          const auto [start, end] = lineRunRange(first + 1, last);
                          if (end > start) {
                              buffer.DeleteRange(start, end - start);
                          }
                          // Fresh reference, not the outer `content` -- the
                          // DeleteRange just above replaces the buffer's
                          // internal storage (a real, ASan-caught heap-use-
                          // after-free otherwise, same shape as toggle-line-
                          // comment's own fix above).
                          buffer.SetPoint(buffer.Content().LineToByteOffset(first));
                      });

    registry.Register("recenter", "Scroll so the line at point is centered in the window.",
                      [](CommandContext& context) { context.interactiveRequest = InteractiveRequest::Recenter; });

    registry.Register("goto-line", "Jump to a line by number (prompts for it).",
                      [](CommandContext& context) { context.interactiveRequest = InteractiveRequest::GotoLine; });

    registry.Register("save-some-buffers", "Save every modified file-backed buffer.", [](CommandContext& context) {
        // Real Emacs asks y/n per buffer; saving without asking is the v1
        // cut (these are the user's own buffers, and quit still confirms).
        // No format-on-save here either -- that stays save-buffer's own
        // single-buffer concern.
        std::size_t saved = 0;
        std::string failed;
        for (const auto& buffer : context.bufferList.Buffers()) {
            if (!buffer->Path().has_value() || !buffer->Modified()) {
                continue;
            }
            try {
                WriteBufferToDisk(*buffer);
                ++saved;
            }
            catch (const std::exception&) {
                failed += failed.empty() ? buffer->Name() : ", " + buffer->Name();
            }
        }
        if (context.message) {
            *context.message = "Saved " + std::to_string(saved) + " buffer" + (saved == 1 ? "" : "s");
            if (!failed.empty()) {
                *context.message += " (failed: " + failed + ")";
            }
        }
    });

    // disk-space-safety follow-up: the confirmed, real override path for a
    // buffer Buffer::FromHugeFile forced read-only over insufficient disk
    // space -- SetReadOnly(false) already existed as a public method, but
    // nothing user-facing called it. Generic rather than huge-file-specific
    // (toggles either direction, clearing any stored ReadOnlyReason() when
    // turning off), since a manual read-only toggle is a reasonable command
    // to have regardless of why a buffer ended up read-only.
    registry.Register("toggle-read-only", "Toggle whether the current buffer accepts edits.", [](CommandContext& context) {
        const bool wasReadOnly = context.buffer.ReadOnly();
        context.buffer.SetReadOnly(!wasReadOnly);
        if (context.message) {
            *context.message = wasReadOnly ? "Buffer is now editable." : "Buffer is now read-only.";
        }
    });

    // binary-safety-guardrails follow-up: the escape hatch for
    // BinarySafeguardsActive() -- see Buffer.h's own doc comment on that
    // predicate and the guard sites it gates (save-buffer's format-on-save/
    // ensure-final-newline/forced-line-ending steps, format-buffer,
    // convert-line-endings-to-*). A no-op message (not silently ignored)
    // for a buffer that was never LikelyBinary() in the first place -- there's
    // nothing to override.
    registry.Register("toggle-binary-safeguards",
                      "Toggle whether a binary-detected buffer's format/line-ending/final-newline safeguards apply.",
                      [](CommandContext& context) {
                          if (!context.buffer.LikelyBinary()) {
                              if (context.message) {
                                  *context.message = "\"" + context.buffer.Name() + "\" was never detected as binary -- nothing to override.";
                              }
                              return;
                          }
                          const bool wasOverridden = context.buffer.BinarySafetyOverride();
                          context.buffer.SetBinarySafetyOverride(!wasOverridden);
                          if (context.message) {
                              *context.message = wasOverridden
                                                     ? "Binary safeguards restored for \"" + context.buffer.Name() + "\"."
                                                     : "Binary safeguards overridden for \"" + context.buffer.Name() +
                                                           "\" -- format-on-save, forced line-ending conversion, and "
                                                           "ensure-final-newline now apply like an ordinary text buffer.";
                          }
                      });

    // keyboard-quit follow-up: real Emacs' C-g aborts several things at
    // once (the current command, a pending prefix key, an active
    // minibuffer) -- the prefix-key/minibuffer cases are already handled
    // elsewhere (a pending Dispatcher sequence resets itself on the next
    // Unbound chord; minibuffer-shaped sessions -- isearch, prompts,
    // confirmations -- have their own dedicated Escape/C-g handling inside
    // BufferView's HandleXKey routines, none of which reach this command
    // at all since inputMode_ isn't Normal while they're active). This is
    // the one piece that was genuinely missing: deactivating an active
    // mark set via C-SPC/shift-select-*, reported live -- there was no way
    // to stop an in-progress selection short of an editing command, mouse
    // click, or actually killing/copying the region. Silent no-op without
    // a mark, matching kill-region's own "nothing to do" convention.
    registry.Register("keyboard-quit", "Deactivate the current selection and collapse to one cursor.",
                      [](CommandContext& context) {
                          context.buffer.ClearMark();
                          // Multi-cursor phase: C-g is also the collapse
                          // gesture, real Emacs multiple-cursors' own
                          // convention.
                          context.buffer.ClearSecondaryCursors();
                      });

    // Multi-cursor phase. add-cursor-below/above extend from the
    // bottom-most/top-most cursor (VS Code's own semantics -- repeated
    // presses keep growing the column of cursors), landing at the same
    // visual column via the same tab-aware ByteOffsetForLineAndColumn
    // translation mouse clicks use; a silent no-op at the buffer's own
    // first/last line, or when the target position already has a cursor
    // (AddCursorAt's dedupe).
    registry.Register("add-cursor-below", "Add a cursor one line below the bottom-most cursor.",
                      [](CommandContext& context) {
                          text::Buffer&     buffer  = context.buffer;
                          const text::ITextStorage& content = buffer.Content();
                          std::size_t       lowest  = buffer.Point();
                          for (const auto& cursor : buffer.SecondaryCursors()) {
                              lowest = std::max(lowest, cursor.point);
                          }
                          const std::size_t line = content.ByteOffsetToLine(lowest);
                          if (line + 1 >= content.LineCount()) {
                              return;
                          }
                          const std::size_t column =
                              buffer.VisualColumnForByteOffset(content.LineToByteOffset(line), lowest, TabWidth());
                          const std::size_t target = buffer.ByteOffsetForLineAndColumn(line + 1, column, TabWidth());
                          buffer.AddCursorAt(target);
                          context.newlyAddedCursorPoint = target;
                      });

    registry.Register("add-cursor-above", "Add a cursor one line above the top-most cursor.",
                      [](CommandContext& context) {
                          text::Buffer&     buffer  = context.buffer;
                          const text::ITextStorage& content = buffer.Content();
                          std::size_t       highest = buffer.Point();
                          for (const auto& cursor : buffer.SecondaryCursors()) {
                              highest = std::min(highest, cursor.point);
                          }
                          const std::size_t line = content.ByteOffsetToLine(highest);
                          if (line == 0) {
                              return;
                          }
                          const std::size_t column =
                              buffer.VisualColumnForByteOffset(content.LineToByteOffset(line), highest, TabWidth());
                          const std::size_t target = buffer.ByteOffsetForLineAndColumn(line - 1, column, TabWidth());
                          buffer.AddCursorAt(target);
                          context.newlyAddedCursorPoint = target;
                      });

    // Multi-cursor round-2 follow-up: the "undo the last add" a bare
    // AddCursorAt call never had -- an unwanted cursor from add-cursor-below/
    // -above stepping onto an empty/short line, or an unwanted match from
    // select-next-occurrence, previously meant ClearSecondaryCursors()'s
    // collapse-everything-and-start-over (keyboard-quit above) was the only
    // recourse. Repeated presses walk back through additions one at a time
    // (Buffer::RemoveLastAddedCursor's own LIFO stack), not just the very
    // last one.
    registry.Register("remove-last-cursor", "Remove the most recently added secondary cursor.", [](CommandContext& context) {
        if (!context.buffer.RemoveLastAddedCursor() && context.message) {
            *context.message = "No cursor to remove";
        }
    });

    // First press with no selection: select the word at point (mark at its
    // start, point at its end), VS Code Ctrl+D-style. Every later press:
    // add a cursor selecting the next occurrence of the primary selection's
    // text, searching forward from the furthest cursor and wrapping around
    // once -- occurrences an existing cursor already owns are skipped.
    registry.Register("select-next-occurrence",
                      "Select the word at point, or add a cursor at the next occurrence of the selection.",
                      [](CommandContext& context) {
                          text::Buffer& buffer = context.buffer;
                          if (!buffer.HasMark() || buffer.Region().first == buffer.Region().second) {
                              if (const auto word = WordRegionAt(buffer.Content(), buffer.Point())) {
                                  buffer.SetMark(word->first);
                                  buffer.SetPoint(word->second);
                              }
                              else if (context.message) {
                                  *context.message = "No word at point to select.";
                              }
                              return;
                          }

                          const auto [regionStart, regionEnd] = buffer.Region();
                          const std::string needle            = buffer.Content().Substring(regionStart, regionEnd - regionStart);
                          const std::string haystack          = buffer.Text();

                          const auto ownedByExistingCursor = [&buffer](std::size_t candidate) {
                              if (CursorSelectionStart(buffer.Point(),
                                                       buffer.HasMark() ? std::optional(buffer.Mark()) : std::nullopt) ==
                                  candidate) {
                                  return true;
                              }
                              for (const auto& cursor : buffer.SecondaryCursors()) {
                                  if (CursorSelectionStart(cursor.point, cursor.mark) == candidate) {
                                      return true;
                                  }
                              }
                              return false;
                          };

                          std::size_t furthest = std::max(buffer.Point(), buffer.Mark());
                          for (const auto& cursor : buffer.SecondaryCursors()) {
                              furthest = std::max(furthest, std::max(cursor.point, cursor.mark.value_or(cursor.point)));
                          }

                          // One forward pass from the furthest cursor, then one
                          // wrapped pass from the top -- skipping owned matches.
                          std::size_t candidate = haystack.find(needle, furthest);
                          while (candidate != std::string::npos && ownedByExistingCursor(candidate)) {
                              candidate = haystack.find(needle, candidate + 1);
                          }
                          if (candidate == std::string::npos) {
                              candidate = haystack.find(needle);
                              while (candidate != std::string::npos && candidate < furthest &&
                                     ownedByExistingCursor(candidate)) {
                                  candidate = haystack.find(needle, candidate + 1);
                              }
                              if (candidate != std::string::npos && ownedByExistingCursor(candidate)) {
                                  candidate = std::string::npos;
                              }
                          }

                          if (candidate == std::string::npos) {
                              if (context.message) {
                                  *context.message = "No more occurrences of \"" + needle + "\".";
                              }
                              return;
                          }
                          buffer.AddCursorAt(candidate + needle.size(), candidate);
                          context.newlyAddedCursorPoint = candidate + needle.size();
                          if (context.message) {
                              *context.message = std::to_string(buffer.SecondaryCursors().size() + 1) + " cursors";
                          }
                      });

    registry.Register("select-all-occurrences",
                      "Add a cursor selecting every occurrence of the current selection (or the word at point).",
                      [](CommandContext& context) {
                          text::Buffer& buffer = context.buffer;
                          if (!buffer.HasMark() || buffer.Region().first == buffer.Region().second) {
                              if (const auto word = WordRegionAt(buffer.Content(), buffer.Point())) {
                                  buffer.SetMark(word->first);
                                  buffer.SetPoint(word->second);
                              }
                              else {
                                  if (context.message) {
                                      *context.message = "No word at point to select.";
                                  }
                                  return;
                              }
                          }

                          const auto [regionStart, regionEnd] = buffer.Region();
                          const std::string needle            = buffer.Content().Substring(regionStart, regionEnd - regionStart);
                          const std::string haystack          = buffer.Text();

                          for (std::size_t candidate = haystack.find(needle); candidate != std::string::npos;
                               candidate             = haystack.find(needle, candidate + 1)) {
                              if (candidate == regionStart) {
                                  continue; // the primary's own selection
                              }
                              buffer.AddCursorAt(candidate + needle.size(), candidate);
                          }
                          if (context.message) {
                              *context.message = std::to_string(buffer.SecondaryCursors().size() + 1) + " cursors";
                          }
                      });

    registry.Register("kill-ring-save", "Copy the region between point and mark into the kill ring, without deleting it.", [](CommandContext& context) {
        KillPerCursor(context, [](CommandContext& context) -> std::optional<std::string> {
            if (!context.buffer.HasMark()) {
                return std::nullopt;
            }
            const auto [start, end] = context.buffer.Region();
            std::string text        = context.buffer.Content().Substring(start, end - start);
            context.buffer.ClearMark();
            return text;
        });
    });

    registry.Register("undo", "Undo the last change.", [](CommandContext& context) {
        context.buffer.ClearMark();
        // project-undo follow-up: if this buffer sits exactly on the edge a
        // multi-file LSP edit left it on, undo every file that edit touched
        // together rather than just this one -- see ProjectUndo.h's own
        // doc comment. Falls back to a plain per-buffer undo otherwise
        // (headless/no manager, an ordinary single-file edit, or a
        // transaction this buffer has since diverged from).
        if (context.projectUndo && context.projectUndo->IsUndoTarget(context.buffer)) {
            const ProjectUndoOutcome outcome = context.projectUndo->Undo(context.bufferList);
            if (context.message) {
                *context.message = FormatProjectUndoMessage("Undid", outcome);
            }
            return;
        }
        context.buffer.Undo();
    });

    registry.Register("redo", "Redo the last undone change.", [](CommandContext& context) {
        context.buffer.ClearMark();
        if (context.projectUndo && context.projectUndo->IsRedoTarget(context.buffer)) {
            const ProjectUndoOutcome outcome = context.projectUndo->Redo(context.bufferList);
            if (context.message) {
                *context.message = FormatProjectUndoMessage("Redid", outcome);
            }
            return;
        }
        context.buffer.Redo();
    });

    registry.Register(
        "newline", "Insert a newline at point, electric-indenting the new line when the mode supports it.",
        PerCursor([](CommandContext& context) {
            text::Buffer& buffer = context.buffer;
            buffer.ClearMark();
            buffer.BeginUndoGroup();

            // smart-blank-line-on-newline follow-up: if point currently
            // sits on a line that's ENTIRELY whitespace (typically one a
            // prior "newline" call itself auto-indented and nothing was
            // ever typed into), clear that dangling run before splitting --
            // replacing [lineStart, lineEnd) with a bare "\n" in one step
            // rather than leaving stale whitespace behind AND inserting a
            // second newline after it. Mode-agnostic and unconditional:
            // this alone is what gives a tree-sitter-backed mode (Python,
            // say) "clear the whitespace, keep the depth" behavior on a
            // second Enter, since the new line's own indent is still
            // computed fresh below regardless. See BlankLineCleanup.h's own
            // doc comment for the further, mode-specific Markdown/Org
            // "second Enter also ends list continuation" addition this
            // deliberately does NOT handle here.
            if (CleanBlankLineOnNewline()) {
                const auto&       content   = buffer.Content();
                const std::size_t line      = content.ByteOffsetToLine(buffer.Point());
                const std::size_t lineStart = content.LineToByteOffset(line);
                std::size_t        lineEnd   = (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
                if (line + 1 < content.LineCount() && lineEnd > lineStart) {
                    --lineEnd; // exclude the line's own trailing '\n'
                }
                if (lineEnd > lineStart && LineIndentEnd(content, lineStart) == lineEnd) {
                    buffer.DeleteRange(lineStart, lineEnd - lineStart);
                    buffer.SetPoint(lineStart);
                }
            }

            buffer.InsertAtPoint("\n");
            // smart-indentation follow-up: purely additive -- a mode without
            // indentColumn configured (every mode not yet migrated, e.g.
            // Fundamental this pass) leaves this a bare InsertAtPoint("\n"),
            // byte-for-byte unchanged from before. lineStart == lineStart as
            // the [lineStart, lineEnd) argument is IndentFunction's own
            // "not-yet-typed blank line" convention (Mode.h) -- the new line
            // is empty at this point, nothing to bound.
            if (context.mode != nullptr && context.mode->indentColumn) {
                const auto&        content   = buffer.Content();
                const std::size_t  line      = content.ByteOffsetToLine(buffer.Point());
                const std::size_t  lineStart = content.LineToByteOffset(line);
                if (const std::optional<int> column = context.mode->indentColumn(buffer.Text(), lineStart, lineStart)) {
                    const IndentStyle style = EffectiveIndentStyle(context.mode->name);
                    SetLineIndent(buffer, lineStart, *column, style);
                    buffer.SetPoint(lineStart + IndentString(*column, style).size());
                }
            }
            buffer.EndUndoGroup();
        }));

    registry.Register("self-insert-command", "Insert the character that was pressed.", PerCursor([](CommandContext& context) {
                          if (context.triggeringKey.Special != SpecialKey::None || context.triggeringKey.Codepoint == 0) {
                              context.buffer.ClearMark();
                              return;
                          }
                          const std::string inserted = text::EncodeCodepointUtf8(context.triggeringKey.Codepoint);

                          // auto-pair-brackets-and-quotes follow-up: only a
                          // single-byte ASCII delimiter is ever in the pair
                          // table, so a multi-byte codepoint always falls
                          // through to the plain insert below unchanged.
                          // context.mode is nullptr in a headless/test context
                          // (see Command.h's own doc comment); falls back to
                          // DefaultAutoPairs() there rather than pairing nothing.
                          if (inserted.size() == 1 && AutoPairEnabled()) {
                              text::Buffer&                             buffer = context.buffer;
                              const std::size_t                         point  = buffer.Point();
                              const std::vector<std::pair<char, char>>& pairs =
                                  context.mode ? context.mode->autoPairs : DefaultAutoPairs();
                              // Named locals, not temporaries -- query below holds
                              // string_views into these, which must outlive it.
                              const std::string before = GraphemeBefore(buffer);
                              const std::string after  = GraphemeAfter(buffer);

                              AutoPairQuery query;
                              query.typed        = inserted[0];
                              query.charBefore   = before;
                              query.charAfter    = after;
                              query.hasSelection = buffer.HasMark();
                              query.pairs        = &pairs;
                              // Only a symmetric (quote-like) opener ever reads
                              // classAtPoint (DecideSelfInsert's own
                              // IsQuotePair branch) -- gate the real
                              // Mode::highlight call behind that same check so
                              // ordinary bracket/letter keystrokes never pay
                              // for it.
                              if (const std::optional<char> closer = ClosingCharFor(query.typed, pairs);
                                  closer && *closer == query.typed) {
                                  query.classAtPoint = SyntaxClassAtPoint(context.mode, buffer, point);
                              }

                              switch (DecideSelfInsert(query)) {
                                  case PairAction::InsertPair: {
                                      const std::optional<char> closer = ClosingCharFor(query.typed, pairs);
                                      buffer.ClearMark();
                                      buffer.InsertAtPoint(std::string(1, query.typed) + std::string(1, *closer));
                                      buffer.SetPoint(point + 1); // land between the pair, not after it
                                      return;
                                  }
                                  case PairAction::SkipOver:
                                      buffer.ClearMark();
                                      buffer.SetPoint(point + query.charAfter.size());
                                      return;
                                  case PairAction::WrapSelection: {
                                      const auto [start, end]          = buffer.Region();
                                      const std::optional<char> closer = ClosingCharFor(query.typed, pairs);
                                      buffer.ClearMark();
                                      // InsertAt always records a hard undo step (no
                                      // amend, see its own doc comment) -- group the
                                      // pair of edits so wrapping a selection undoes
                                      // as one step, not two.
                                      buffer.BeginUndoGroup();
                                      buffer.InsertAt(start, std::string(1, query.typed));
                                      buffer.InsertAt(end + 1, std::string(1, *closer)); // +1: the opener just inserted at start shifted end
                                      buffer.EndUndoGroup();
                                      buffer.SetPoint(end + 2); // land after the newly-inserted closer
                                      return;
                                  }
                                  case PairAction::InsertPlain:
                                  case PairAction::DeleteAdjacentPair:
                                      break; // ordinary self-insert below
                              }
                          }

                          context.buffer.ClearMark();
                          context.buffer.InsertAtPoint(inserted);
                      }));

    // smart-indentation follow-up: when the mode has real indentColumn
    // support AND point sits at-or-before the end of the current line's own
    // leading whitespace (i.e. TAB pressed at/near the start of the line,
    // not deep inside real content), reindent this line to its computed
    // column in place. Otherwise -- indentColumn unset, or point is past the
    // leading whitespace -- falls through to the original, unchanged
    // behavior: snippet expansion first, then a literal-tab insert (Emacs'
    // own indent-for-tab-command computes indentation for every mode; this
    // codebase only has that for the modes Editor/Indent.h's engine has been
    // extended to). Global, but a mode's own keymap (e.g. org-mode's
    // org-cycle, markdown-mode's markdown-table-align) still wins via
    // KeymapStack's priority order, so this only ever fires where nothing
    // more specific claimed TAB first (snippet expansion in those modes goes
    // through the expand-snippet command instead).
    registry.Register(
        "indent-for-tab-command",
        "Reindent the current line to its computed indentation, or -- with an active region -- rigidly indent "
        "every line the region spans by one indent width; otherwise expand the snippet trigger before point, or "
        "insert a tab character.",
        [](CommandContext& context) {
            // mode-agnostic-rigid-indent follow-up: an active mark routes
            // TAB to a rigid, mode-agnostic "nudge every selected line one
            // indent width to the right" instead of the single-line
            // recompute below -- deliberately a DIFFERENT, simpler
            // operation than indent-region's own tree-sitter recompute
            // (which stays M-x-only, unbound, keeping its existing
            // "language-aware reindent" meaning). Works even when the mode
            // has no indentColumn configured at all -- RigidShiftRegion
            // only ever reads/writes existing leading whitespace, never
            // consults Mode. Mirrors indent-region's own region-to-line-
            // range resolution (Commands.cpp's own "indent-region"
            // command) for consistency. Clears the mark afterward --
            // matches every other editing command's own convention
            // ("Editing commands clear a leftover mark, unlike plain
            // motion", CommandsTest.cpp), not modern IDEs' own
            // keep-selection-for-repeated-Tab convention.
            if (context.buffer.HasMark()) {
                const auto [start, end]      = context.buffer.Region();
                const auto&        content    = context.buffer.Content();
                const std::size_t  startLine  = content.ByteOffsetToLine(start);
                const std::size_t  endLine    = content.ByteOffsetToLine(end) + 1; // exclusive
                const IndentStyle  style      = EffectiveIndentStyle(context.mode != nullptr ? context.mode->name : std::string());
                context.buffer.ClearMark();
                RigidShiftRegion(context.buffer, style, startLine, endLine, 1);
                return;
            }
            if (context.mode != nullptr && context.mode->indentColumn) {
                text::Buffer&      buffer    = context.buffer;
                const auto&        content   = buffer.Content();
                const std::size_t  line      = content.ByteOffsetToLine(buffer.Point());
                const std::size_t  lineStart = content.LineToByteOffset(line);
                const std::size_t  indentEnd = LineIndentEnd(content, lineStart);
                if (buffer.Point() <= indentEnd) {
                    std::size_t lineEnd =
                        (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
                    if (line + 1 < content.LineCount() && lineEnd > lineStart) {
                        --lineEnd; // exclude the line's own trailing '\n'
                    }
                    if (const std::optional<int> column = context.mode->indentColumn(buffer.Text(), lineStart, lineEnd)) {
                        const IndentStyle style = EffectiveIndentStyle(context.mode->name);
                        buffer.ClearMark();
                        SetLineIndent(buffer, lineStart, *column, style);
                        buffer.SetPoint(lineStart + IndentString(*column, style).size());
                        return;
                    }
                }
            }
            if (TrySnippetTrigger(context)) {
                return;
            }
            context.buffer.ClearMark();
            context.buffer.InsertAtPoint("\t");
        });

    // mode-agnostic-rigid-indent follow-up: S-TAB's own symmetric sibling
    // to indent-for-tab-command's new mark-active branch above -- no
    // leading-whitespace-position guard (unlike TAB, S-TAB never means
    // "insert a literal character", so there's no ambiguity to stay clear
    // of), and no-mark dedents just the current line instead of a no-op.
    registry.Register(
        "unindent",
        "Rigidly remove one indent width from every line the active region spans, or from the current line if no "
        "region is active.",
        [](CommandContext& context) {
            const IndentStyle style = EffectiveIndentStyle(context.mode != nullptr ? context.mode->name : std::string());
            if (context.buffer.HasMark()) {
                const auto [start, end]     = context.buffer.Region();
                const auto&        content   = context.buffer.Content();
                const std::size_t  startLine = content.ByteOffsetToLine(start);
                const std::size_t  endLine   = content.ByteOffsetToLine(end) + 1; // exclusive
                // Clears the mark afterward -- matches every other editing
                // command's own convention, see indent-for-tab-command's
                // own mark-active branch above.
                context.buffer.ClearMark();
                RigidShiftRegion(context.buffer, style, startLine, endLine, -1);
                return;
            }
            const std::size_t line = context.buffer.Content().ByteOffsetToLine(context.buffer.Point());
            RigidShiftRegion(context.buffer, style, line, line + 1, -1);
        });

    registry.Register("expand-snippet",
                      "Expand the registered snippet whose trigger word ends at point.",
                      [](CommandContext& context) {
                          if (!TrySnippetTrigger(context) && context.message != nullptr) {
                              *context.message = "No snippet matches the word before point.";
                          }
                      });

    // move-line-up/move-line-down follow-up: swaps the current line with
    // its neighbor by replacing the byte span covering both with them
    // reordered, reconstructed via GetLineSpan rather than raw substring
    // concatenation across the swap -- gets the "current line is the
    // buffer's last, trailing-newline-less line" edge case right on
    // whichever side it ends up on after the swap. Point's column *within*
    // the moved line is preserved; a no-op at the buffer's first/last line
    // respectively, not an error.
    registry.Register("move-line-up", "Move the current line up, swapping it with the line above.",
                      [](CommandContext& context) { MoveLineUp(context.buffer); });

    registry.Register("move-line-down", "Move the current line down, swapping it with the line below.",
                      [](CommandContext& context) { MoveLineDown(context.buffer); });

    // duplicate-line follow-up: inserts a copy of the current line
    // immediately below it, moving point into the duplicate at the same
    // column -- VSCode/Sublime/JetBrains' own "duplicate down" convention.
    // Uses GetLineSpan for the same trailing-newline-edge-case reason
    // move-line-up/down do.
    registry.Register("duplicate-line", "Duplicate the current line, moving point into the copy.", [](CommandContext& context) {
        auto&             buffer  = context.buffer;
        const auto&       content = buffer.Content();
        const std::size_t line    = content.ByteOffsetToLine(buffer.Point());
        const LineSpan    curr    = GetLineSpan(content, line);
        const std::size_t column  = std::min(buffer.Point() - curr.start, curr.contentEnd - curr.start);

        const std::string lineText       = content.Substring(curr.start, curr.contentEnd - curr.start);
        const std::string insertion      = curr.hasTrailingNewline ? lineText + "\n" : "\n" + lineText;
        const std::size_t insertOffset   = curr.hasTrailingNewline ? curr.contentEnd + 1 : curr.contentEnd;
        const std::size_t duplicateStart = curr.hasTrailingNewline ? insertOffset : insertOffset + 1;

        buffer.ClearMark();
        buffer.InsertAt(insertOffset, insertion);
        buffer.SetPoint(duplicateStart + column);
    });

    registry.Register("quit", "Exit the editor, or prompt for confirmation if any buffer has unsaved changes.",
                      [](CommandContext& context) {
                          const bool anyModified =
                              std::any_of(context.bufferList.Buffers().begin(), context.bufferList.Buffers().end(),
                                          [](const auto& buffer) { return buffer->Modified() && !buffer->ReadOnly(); });
                          if (anyModified) {
                              context.interactiveRequest = InteractiveRequest::ConfirmQuit;
                          }
                          else {
                              context.quit = true;
                          }
                      });

    registry.Register("suspend-frame", "Suspend ned and return to the shell (job control), like Emacs' C-z.",
                      [](CommandContext& context) { context.suspend = true; });

    // external-modification-safety follow-up: the actual save body, shared
    // by save-buffer (which gates it behind a supersession check) and
    // save-buffer-force (what BufferView's overwrite confirmation invokes
    // on y -- also M-x-reachable as the deliberate "I know, write it
    // anyway" escape hatch).
    const auto saveBufferBody = [](CommandContext& context) {
        try {
            // binary-safety-guardrails follow-up: BinarySafeguardsActive()
            // also gates the format-on-save step immediately below (auto-
            // formatting can corrupt binary content just as much as the
            // final-newline/line-ending behaviors WriteBufferToDisk itself
            // gates) -- overridable per-buffer via toggle-binary-safeguards.
            const bool binarySafeguards = context.buffer.BinarySafeguardsActive();

            // Only attempted when a command is actually configured (format-
            // on-save follow-up; see FormatOnSave.h) -- FormatCommand() is
            // checked separately from RunFormatCommand()'s result so a
            // configured-but-failing formatter can be reported distinctly
            // from "nothing configured," rather than both looking identical.
            bool formatFailed = false;
            if (FormatCommand() && !binarySafeguards) {
                if (const std::optional<std::string> formatted = RunFormatCommand(context.buffer.Text())) {
                    // Whole-buffer replace, not a targeted diff/patch -- simple
                    // and correct, at the cost of point landing at the end of
                    // the buffer after a format rather than staying put. A
                    // known v1 scope cut, not an oversight.
                    context.buffer.DeleteRange(0, context.buffer.Size());
                    context.buffer.InsertAt(0, *formatted);
                }
                else {
                    formatFailed = true;
                }
            }

            WriteBufferToDisk(context.buffer);
            if (context.message) {
                *context.message = "Wrote " + context.buffer.Name() + (formatFailed ? " (format command failed)" : "");
            }
        }
        catch (const std::exception& e) {
            if (context.message) {
                *context.message = e.what();
            }
        }
    };

    // lsp-format-on-save follow-up: false whenever an external FormatCommand()
    // is configured (that always takes precedence -- the more specific,
    // deliberately hand-configured choice), the toggle is off, or no
    // language server is actually running for this buffer's language.
    // Checked by both save-buffer and save-buffer-force, after their own
    // (save-buffer-only) guards above have already passed -- when true, the
    // rest of this save is handed off to BufferView's async continuation
    // (RequestLspFormatThenSaveBuffer) instead of running saveBufferBody
    // synchronously, since an LSP round trip can't be waited on inline.
    const auto shouldDeferToLspFormat = [](CommandContext& context) {
        // binary-safety-guardrails follow-up: an LSP formatter is exactly
        // as capable of corrupting binary content as the external
        // FormatCommand() this same early-out already skips for.
        if (FormatCommand() || context.buffer.BinarySafeguardsActive() || !editor::lsp::LspFormatOnSaveEnabled() ||
            !context.lspManager || !context.mode) {
            return false;
        }
        const std::string languageKey = LanguageKeyForMode(*context.mode);
        return context.lspManager->StatusForLanguage(languageKey) == lsp::LspManager::LspStatus::Running;
    };

    registry.Register("save-buffer", "Save the current buffer to its associated file.",
                      [saveBufferBody, shouldDeferToLspFormat](CommandContext& context) {
                          // progressive-huge-file-load follow-up: checked first -- Buffer::
                          // SaveToFile itself already refuses a still-loading buffer, but
                          // that content is still growing/incomplete, so there's no point
                          // running the checks below against it at all.
                          if (context.buffer.IsLoading()) {
                              if (context.message) {
                                  *context.message = "Cannot save \"" + context.buffer.Name() + "\" -- still loading in the background";
                              }
                              return;
                          }
                          // Never silently overwrite a file someone else wrote underneath
                          // this buffer (Emacs' supersession check): hand the decision to a
                          // y/n confirmation instead of writing anything.
                          if (context.buffer.ExternallyModified()) {
                              context.interactiveRequest = InteractiveRequest::ConfirmOverwriteSave;
                              return;
                          }
                          // external-modification-round-2 follow-up: never silently write
                          // unresolved "<<<<<<<" conflict markers to disk either -- same
                          // "hand the decision to a y/n confirmation" shape as the
                          // ExternallyModified() check just above. Buffer::HasConflictMarkers
                          // scans in bounded windows (huge-file-search-and-save follow-up),
                          // not context.buffer.Text(), so this stays cheap for a huge buffer.
                          if (context.buffer.HasConflictMarkers()) {
                              context.interactiveRequest = InteractiveRequest::ConfirmSaveWithConflicts;
                              return;
                          }
                          if (shouldDeferToLspFormat(context)) {
                              context.deferSaveForLspFormat = false; // force=false: the guards above already ran
                              return;
                          }
                          saveBufferBody(context);
                      });

    registry.Register("save-buffer-force", "Save the current buffer even if its file changed on disk.",
                      [saveBufferBody, shouldDeferToLspFormat](CommandContext& context) {
                          // See save-buffer's own comment on this guard.
                          if (context.buffer.IsLoading()) {
                              if (context.message) {
                                  *context.message = "Cannot save \"" + context.buffer.Name() + "\" -- still loading in the background";
                              }
                              return;
                          }
                          if (shouldDeferToLspFormat(context)) {
                              context.deferSaveForLspFormat = true; // force=true: mirrors this command's own "skip the guards" meaning
                              return;
                          }
                          saveBufferBody(context);
                      });

    // format-buffer follow-up: save-buffer's own formatting step, exposed
    // standalone so it can run without also saving -- reuses the exact
    // same FormatCommand()/RunFormatCommand()/whole-buffer-replace shape,
    // just without the Save() call at the end. Unlike save-buffer (silent
    // when nothing's configured, since formatting there is a side effect
    // of a save the user wanted regardless), this reports "no format
    // command configured" explicitly: a user invoking format-buffer
    // directly is asking specifically for formatting, so silence would
    // read as "did nothing happened," not "nothing to do."
    registry.Register("format-buffer", "Run the configured format command over the whole buffer, without saving.",
                      [](CommandContext& context) {
                          // progressive-huge-file-load follow-up: same reasoning as
                          // save-buffer's own guard -- checked first since the whole-buffer
                          // Text()+DeleteRange+InsertAt below fully materializes the buffer,
                          // now newly reachable mid-load since a huge buffer is genuinely
                          // editable while loading (see Buffer::ReadOnly()'s own doc
                          // comment).
                          if (context.buffer.IsLoading()) {
                              if (context.message) {
                                  *context.message = "Cannot format \"" + context.buffer.Name() + "\" -- still loading in the background";
                              }
                              return;
                          }
                          if (!FormatCommand()) {
                              if (context.message) {
                                  *context.message = "No format command configured.";
                              }
                              return;
                          }
                          // binary-safety-guardrails follow-up: refuses an explicit
                          // invocation too, not just the automatic format-on-save side
                          // effect (see save-buffer's own guard) -- a formatter is just
                          // as capable of corrupting binary content whether it runs as
                          // a save side effect or because the user asked for it
                          // directly. toggle-binary-safeguards is the escape hatch.
                          if (context.buffer.BinarySafeguardsActive()) {
                              if (context.message) {
                                  *context.message = "\"" + context.buffer.Name() +
                                                     "\" looks like binary content -- refusing to format it "
                                                     "(run toggle-binary-safeguards to override)";
                              }
                              return;
                          }
                          if (const std::optional<std::string> formatted = RunFormatCommand(context.buffer.Text())) {
                              context.buffer.DeleteRange(0, context.buffer.Size());
                              context.buffer.InsertAt(0, *formatted);
                              if (context.message) {
                                  *context.message = "Formatted " + context.buffer.Name();
                              }
                          }
                          else if (context.message) {
                              *context.message = "Format command failed.";
                          }
                      });

    // crlf-handling follow-up: sets only this buffer's own tracked line
    // ending (Buffer::SetLineEndingOverride) -- a disk-only-until-saved
    // preference, exactly like ensureFinalNewline/trimTrailingWhitespace.
    // Nothing on disk or in the live Rope changes until the next explicit
    // save; that save is what actually re-encodes the file (still subject
    // to a Force policy, see Editor/LineEndingPolicy.h -- unusual, but the
    // same override-vs-global-policy relationship ensureFinalNewline has).
    const auto convertLineEndings = [](text::LineEnding ending) {
        return [ending](CommandContext& context) {
            // binary-safety-guardrails follow-up: refused outright rather
            // than set-and-silently-ignored -- saveBufferBody skips
            // ResolveLineEndingForSave entirely for a BinarySafeguardsActive()
            // buffer (see its own comment), so setting the override here
            // would otherwise look like it took effect but never actually
            // apply at save time. toggle-binary-safeguards is the escape
            // hatch, same as format-buffer's own guard.
            if (context.buffer.BinarySafeguardsActive()) {
                if (context.message) {
                    *context.message = "\"" + context.buffer.Name() +
                                       "\" looks like binary content -- refusing to convert its line endings "
                                       "(run toggle-binary-safeguards to override)";
                }
                return;
            }
            context.buffer.SetLineEndingOverride(ending);
            if (context.message) {
                *context.message = std::string("Buffer will be saved as ") + text::LineEndingName(ending) + " next.";
            }
        };
    };
    registry.Register("convert-line-endings-to-lf", "Save this buffer with LF (Unix) line endings.",
                      convertLineEndings(text::LineEnding::LF));
    registry.Register("convert-line-endings-to-crlf", "Save this buffer with CRLF (Windows) line endings.",
                      convertLineEndings(text::LineEnding::CRLF));
    registry.Register("convert-line-endings-to-cr", "Save this buffer with CR (classic Mac) line endings.",
                      convertLineEndings(text::LineEnding::CR));

    registry.Register("isearch-forward", "Incrementally search forward.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::IsearchForward;
    });

    registry.Register("isearch-backward", "Incrementally search backward.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::IsearchBackward;
    });

    registry.Register("query-replace-regexp", "Interactively replace regexp matches, confirming each one.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::QueryReplace;
    });

    registry.Register("find-file", "Open a file in a new buffer (or create one for a path that doesn't exist yet).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::FindFile;
                      });

    registry.Register("switch-to-buffer", "Switch to another open buffer by name.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::SwitchToBuffer;
    });

    registry.Register("list-buffers", "Open a keyboard-navigable buffer list panel (mark/kill, switch).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ListBuffers;
                      });

    // Tab-reorder follow-up: direct BufferList mutations, not
    // interactiveRequests -- nothing to prompt for, and CommandContext
    // already carries the (mutable) bufferList. Buffers() order is what
    // TabBar renders and SaveProjectSessionNow persists, so both take
    // effect immediately and survive a restart. The keyboard counterpart of
    // TabBar's own drag-to-reorder.
    // Tab-cycling follow-up: switching (as opposed to moving) is a one-shot
    // InteractiveRequest -- the active-buffer pointer lives in BufferView's
    // ActiveBuffer, not in anything CommandContext carries (LspShowLog's
    // exact reasoning).
    registry.Register("tab-next", "Switch to the next tab in the tab bar, wrapping at the end.",
                      [](CommandContext& context) { context.interactiveRequest = InteractiveRequest::TabNext; });

    registry.Register("tab-previous", "Switch to the previous tab in the tab bar, wrapping at the start.",
                      [](CommandContext& context) { context.interactiveRequest = InteractiveRequest::TabPrevious; });

    registry.Register("tab-move-left", "Move the current buffer's tab one position left in the tab bar.",
                      [](CommandContext& context) {
                          const auto& buffers = context.bufferList.Buffers();
                          for (std::size_t i = 0; i < buffers.size(); ++i) {
                              if (buffers[i].get() != &context.buffer) {
                                  continue;
                              }
                              if (i == 0) {
                                  if (context.message) {
                                      *context.message = "Already the leftmost tab.";
                                  }
                                  return;
                              }
                              context.bufferList.MoveBufferToIndex(context.buffer, i - 1);
                              return;
                          }
                      });

    registry.Register("tab-move-right", "Move the current buffer's tab one position right in the tab bar.",
                      [](CommandContext& context) {
                          const auto& buffers = context.bufferList.Buffers();
                          for (std::size_t i = 0; i < buffers.size(); ++i) {
                              if (buffers[i].get() != &context.buffer) {
                                  continue;
                              }
                              if (i + 1 == buffers.size()) {
                                  if (context.message) {
                                      *context.message = "Already the rightmost tab.";
                                  }
                                  return;
                              }
                              context.bufferList.MoveBufferToIndex(context.buffer, i + 1);
                              return;
                          }
                      });

    registry.Register(
        "project-search",
        "Search all files under the current directory (recursively) for a regex pattern, in a new results buffer.",
        [](CommandContext& context) {
            context.interactiveRequest = InteractiveRequest::ProjectSearch;
        });

    // A no-op everywhere except a project-search results buffer -- safe to
    // bind globally. See BufferView::StartInteractiveSession's
    // VisitSearchResult case for the actual line-parsing/jump logic; this
    // command only signals the intent, the same "just set interactiveRequest"
    // pattern find-file/switch-to-buffer/project-search above all use.
    registry.Register("project-search-visit-result",
                      "Jump to the file:line under point in a project-search results buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VisitSearchResult;
                      });

    registry.Register(
        "project-replace",
        "Search-and-replace a regex pattern across all files under the current directory, with one "
        "whole-batch confirmation (not per-match) after previewing every affected file/line.",
        [](CommandContext& context) {
            context.interactiveRequest = InteractiveRequest::ProjectReplace;
        });

    // find-all-references follow-up: same "just signal intent" shape as
    // project-search/project-replace above -- the identifier-at-point scan,
    // SearchDirectory call, and multibuffer construction all live in
    // BufferView, which is what owns BufferList/ActiveBuffer.
    registry.Register(
        "project-find-references",
        "Find every whole-word match for the identifier at point across the project, in a *references* multibuffer.",
        [](CommandContext& context) {
            context.interactiveRequest = InteractiveRequest::ProjectFindReferences;
        });

    // Editable-multibuffer follow-up (wgrep-style commit): a no-op
    // everywhere except a multibuffer carrying at least one editable
    // excerpt (e.g. *diagnostics*, *references: ...*) -- safe to bind
    // globally, the same "just signal intent, real behavior is buffer-
    // shape-gated" posture project-search-visit-result/vcs-visit-result
    // already use, except this one needs no InteractiveRequest round-trip
    // through BufferView: CommitExcerptChanges only needs context.buffer/
    // context.bufferList, both already on CommandContext.
    registry.Register(
        "multibuffer-commit-changes",
        "Write every edited excerpt in the current multibuffer (e.g. *diagnostics*, *references: ...*) back to "
        "its real source buffer. Does not save to disk.",
        [](CommandContext& context) {
            if (context.buffer.ExcerptRanges().empty()) {
                return;
            }
            const multibuffer::CommitResult result = multibuffer::CommitExcerptChanges(context.bufferList, context.buffer);
            if (!context.message) {
                return;
            }
            if (result.committedExcerpts == 0 && result.skipped.empty()) {
                *context.message = "No changes to commit.";
                return;
            }
            *context.message =
                "Committed " + std::to_string(result.committedExcerpts) + " excerpt" + (result.committedExcerpts == 1 ? "" : "s");
            if (!result.skipped.empty()) {
                *context.message += " (" + std::to_string(result.skipped.size()) + " skipped: " + result.skipped.front().second + ")";
            }
        });

    registry.Register("toggle-project-sidebar", "Show or hide the left-side project tree.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ToggleProjectSidebar;
                      });

    registry.Register("toggle-terminal",
                      "Show and focus the built-in terminal drawer; hide it if it is already focused.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ToggleTerminal;
                      });

    registry.Register("focus-project-sidebar",
                      "Move keyboard focus into the project sidebar tree (Up/Down or C-p/C-n to move, Enter to "
                      "open/toggle, Left/Right to collapse/expand, Escape or C-g to return to the editor).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::FocusProjectSidebar;
                      });

    // VCS side panel: same "just set interactiveRequest" shape as
    // toggle-project-sidebar/focus-project-sidebar just above -- docked
    // left, mutually exclusive with the project sidebar (see
    // BufferView::SetVcsPanel's own doc comment).
    registry.Register("toggle-vcs-panel",
                      "Show or hide the left-side VCS status panel (staged/unstaged/untracked files); "
                      "collapses the project sidebar if it's currently shown in the same slot.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ToggleVcsPanel;
                      });
    registry.Register("focus-vcs-panel",
                      "Move keyboard focus into the VCS status panel (Up/Down to move, Space to mark, Enter to "
                      "open/toggle, 'a'/'u' to stage/unstage the marked (or focused) file, 'c' to compose a "
                      "commit, 'w'/'n' to switch/create a branch, Escape or C-g to return to the editor).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::FocusVcsPanel;
                      });

    // session-persistence slice 3: creates the project's .ned/ directory --
    // the strictly-opt-in marker nothing else ever creates -- so the
    // session moves to <root>/.ned/session.json and a .ned/init.janet can
    // be added. A one-shot direct action (no prompt needed), so it acts
    // here rather than via interactiveRequest, same as quit's own direct
    // logic. Also activates session persistence for the *current* run when
    // the root wasn't a marker-carrying project at startup -- without
    // this, the newly initialized project wouldn't start saving until the
    // next launch. Session-persistence-gaps follow-up: also offers (silently
    // appends, matching the rest of this command's own no-prompt shape) a
    // .gitignore entry for the newly-personal session.json -- see
    // AppendSessionJsonToGitignore's own comment for why that one file, and
    // not .ned/ wholesale (init.janet/plugins/ are meant to be committed).
    registry.Register("ned-init-project",
                      "Create the project's .ned/ directory (opt-in home for session data and a project init.janet).",
                      [](CommandContext& context) {
                          const std::filesystem::path root   = ProjectRoot();
                          const std::filesystem::path nedDir = root / ".ned";
                          std::error_code             ec;
                          if (std::filesystem::is_directory(nedDir, ec)) {
                              if (context.message) {
                                  *context.message = nedDir.string() + " already exists.";
                              }
                              return;
                          }
                          std::filesystem::create_directories(nedDir);
                          if (SessionRestoreEnabled() && !ActiveProjectSessionRoot()) {
                              SetActiveProjectSessionRoot(root);
                          }
                          const bool gitignoreUpdated = AppendSessionJsonToGitignore(root);
                          if (context.message) {
                              *context.message = "Created " + nedDir.string() +
                                                 (gitignoreUpdated ? "; added .ned/session.json to .gitignore" : "");
                          }
                      });

    // Minimap widget follow-up: same "just set interactiveRequest" shape as
    // toggle-project-sidebar just above.
    registry.Register("toggle-minimap", "Show or hide the minimap (replacing/restoring the plain scrollbar).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ToggleMinimap;
                      });

    // kill-buffer follow-up: the actual close-with-confirmation logic
    // already lives in BufferView (RequestCloseBuffer/CloseBufferNow),
    // previously reachable only via TabBar's own close-icon click -- this
    // just signals intent the same "just set interactiveRequest" way every
    // one-shot direct action here does.
    registry.Register("kill-buffer", "Close the current buffer, prompting to save first if it has unsaved changes.",
                      [](CommandContext& context) { context.interactiveRequest = InteractiveRequest::KillBuffer; });

    // org-agenda follow-up: global, not gated to org-mode's own keymap --
    // useful from any buffer, same "reachable from every mode" precedent
    // project-search/toggle-project-sidebar already follow.
    registry.Register("org-agenda", "List active (non-DONE) TODO headlines across every .org file in the project.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ProjectAgenda;
                      });

    // diagnostics-log follow-up: global, same "reachable from any mode"
    // precedent org-agenda above already follows.
    registry.Register("show-messages", "Show the *Messages* buffer -- a filterable, on-disk-backed log of ned's own errors/diagnostics.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ShowMessages;
                      });

    registry.Register("create-directory", "Create a new directory (prompts for its path).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::CreateDirectory;
                      });
    registry.Register(
        "delete-file", "Delete a file or directory (prompts for its path, then confirms -- recursive for a directory).",
        [](CommandContext& context) {
            context.interactiveRequest = InteractiveRequest::DeleteFile;
        });
    registry.Register("rename-file", "Rename/move a file or directory (prompts for its current path, then the new one).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::RenameFile;
                      });
    registry.Register(
        "find-scratch",
        "Open or create a named scratch note (prompts for its name; not tied to any project, auto-saved).",
        [](CommandContext& context) {
            context.interactiveRequest = InteractiveRequest::FindScratch;
        });

    // backup-and-recovery follow-up. M-x-only, no default keybinding --
    // recovery is a rare, deliberate act, matching Emacs' own unbound
    // recover-file. The scriptable/macro-able path is ned/list-backups +
    // ned/recover-backup (EditorBindings.cpp), not this prompt.
    registry.Register("recover-file",
                      "Restore the current buffer's content from a recent backup or crash-recovery autosave "
                      "(prompts for the version; the restore is one undoable step and must be saved to keep).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::RecoverFile;
                      });

    registry.Register("split-window-below", "Split the current window into two, one above the other.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::SplitBelow;
                      });
    registry.Register("split-window-right", "Split the current window into two, side by side.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::SplitRight;
                      });
    registry.Register("delete-window", "Close the current window (does nothing if it's the only one).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DeleteWindow;
                      });
    registry.Register("delete-other-windows", "Close every window except the current one.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DeleteOtherWindows;
                      });
    registry.Register("other-window", "Move focus to the next window.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::OtherWindow;
    });

    // Split-resize follow-up: real Emacs' own enlarge-window/shrink-window/
    // enlarge-window-horizontally/shrink-window-horizontally, same division
    // of which three get a default binding -- Emacs itself leaves plain
    // shrink-window (vertical) unbound, M-x/Janet-only, so this does too.
    registry.Register("enlarge-window", "Grow the current window taller against its nearest horizontal split.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::EnlargeWindow;
                      });
    registry.Register("shrink-window", "Shrink the current window against its nearest horizontal split.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ShrinkWindow;
                      });
    registry.Register("enlarge-window-horizontally", "Grow the current window wider against its nearest vertical split.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::EnlargeWindowHorizontally;
                      });
    registry.Register("shrink-window-horizontally", "Shrink the current window against its nearest vertical split.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ShrinkWindowHorizontally;
                      });

    registry.Register("execute-extended-command",
                      "Run a command by name (M-x), narrowed by fuzzy matching as you type.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ExecuteCommand;
                      });

    // project-find-file follow-up: same "just signal intent" shape as
    // execute-extended-command above -- the candidate list (every file
    // under ProjectRoot()) and the fuzzy-narrow/open-on-Enter logic both
    // live in BufferView, which is what actually owns BufferList/
    // ActiveBuffer.
    registry.Register("project-find-file",
                      "Open a file under the project root, narrowed by fuzzy matching as you type.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ProjectFindFile;
                      });

    // editor-ergonomics follow-up: find-recent-file, same "just signal
    // intent" shape as project-find-file above, over Editor/RecentFiles.h's
    // cross-session list instead of a fresh directory walk.
    registry.Register("find-recent-file",
                      "Open a recently-opened file (any project), narrowed by fuzzy matching as you type.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::FindRecentFile;
                      });

    // named-projects follow-up: switch-project/open-project, same "just
    // signal intent" shape as project-find-file/find-recent-file above --
    // the registered-project list, fuzzy-narrow, and the actual
    // detect-terminal/custom-command/replace-in-place activation chain all
    // live in BufferView/Editor/ProjectSwitch.h.
    registry.Register("switch-project",
                      "Switch to a registered project (Editor/ProjectRegistry.h), narrowed by fuzzy matching as "
                      "you type.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::SwitchProject;
                      });
    registry.Register("open-project",
                      "Open a project by path, registering it under a name (prompted, defaulting to the "
                      "directory's own basename) if it isn't already known, then switch to it.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::OpenProject;
                      });

    // editor-ergonomics follow-up: bookmark-set/-jump/-delete (Emacs
    // bookmark.el's own core trio). bookmark-set needs an open, file-backed
    // buffer -- checked here, org-set-tags' own "guard in the command body"
    // precedent, rather than inside StartInteractiveSession's handler.
    registry.Register("bookmark-set",
                      "Save a named bookmark at point in the current file (prompts for a name, pre-filled with the "
                      "filename).",
                      [](CommandContext& context) {
                          if (!context.buffer.Path()) {
                              if (context.message) {
                                  *context.message = "Buffer has no file to bookmark";
                              }
                              return;
                          }
                          context.interactiveRequest = InteractiveRequest::BookmarkSet;
                      });
    registry.Register("bookmark-jump", "Jump to a saved bookmark (prompts for its name, narrowed by fuzzy matching).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::BookmarkJump;
                      });
    registry.Register("bookmark-delete", "Delete a saved bookmark (prompts for its name, narrowed by fuzzy matching).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::BookmarkDelete;
                      });

    // next-error follow-up: Emacs' unifying "walk the last set of located
    // things" primitive -- pure point motion + jump, same one-shot shape as
    // vcs-next-hunk/vcs-previous-hunk, but generic over whichever results
    // buffer (*vcs status*, *search results*, *diagnostics*, *references:
    // ...*, *agenda*, *test results*, ...) was most recently built. See
    // Editor/NextError.h.
    registry.Register("next-error", "Jump to the next location in the last results buffer built (search, VCS status, diagnostics, ...).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::NextError;
                      });
    registry.Register("previous-error", "Jump to the previous location in the last results buffer built.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::PreviousError;
                      });

    // rich-theme-set follow-up (Phase 1): same "just signal intent" shape as
    // project-find-file above. M-x reachable only, no default chord -- theme
    // switching is an occasional act, not an editing motion worth a global
    // binding.
    registry.Register("select-theme",
                      "Switch the color theme, narrowed by fuzzy matching, previewing the highlighted candidate live.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::SelectTheme;
                      });
    // theme-editing follow-up: M-x only, like select-theme above.
    registry.Register("save-theme",
                      "Write the active theme to theme.janet (one ned/theme-set call per color) for hand-editing; "
                      "load it from init.janet with (dofile ...) to make it the startup theme.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::SaveTheme;
                      });

    registry.Register("kmacro-start-macro", "Begin recording a keyboard macro.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::StartKbdMacro;
    });
    registry.Register("kmacro-end-or-call-macro",
                      "Stop recording a keyboard macro, or replay the last recorded one if not currently recording.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::EndOrCallKbdMacro;
                      });

    // prefix-argument follow-up: starts a multi-keystroke reading session
    // (same shape as isearch, not a one-shot direct action) -- BufferView
    // drives Editor/PrefixArgument.h's PrefixArgumentReader from here. See
    // Dispatcher::Feed for where the resolved value actually acts on the
    // command the session's terminating key resolves to.
    registry.Register("universal-argument", "Begin reading a numeric prefix argument for the next command.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::UniversalArgument;
                      });

    registry.Register("point-to-register", "Save point in a register (prompts for the register name).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::PointToRegister;
                      });
    registry.Register("jump-to-register", "Jump to the point saved in a register (prompts for the register name).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::JumpToRegister;
                      });
    registry.Register("copy-to-register", "Copy the region into a register (prompts for the register name).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::CopyToRegister;
                      });
    registry.Register("insert-register", "Insert the text saved in a register (prompts for the register name).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::InsertRegister;
                      });

    // Emacs-keymap-round-2 follow-up: reads one further character (the
    // target) the same prompt-shaped, no-MinibufferPrompt way the register
    // commands above do -- see BufferView::HandleZapToCharKey for the
    // actual scan-and-kill and CommandContext::zapToCharAppend's own doc
    // comment for why the kill-append decision is made here rather than
    // there.
    registry.Register("zap-to-char", "Kill forward from point up to and including the next occurrence of a character.",
                      [](CommandContext& context) {
                          context.zapToCharAppend    = IsKillCommand(context.lastCommand);
                          context.interactiveRequest = InteractiveRequest::ZapToChar;
                      });

    registry.Register("kill-rectangle", "Kill the rectangle defined by point and mark, saving it for yank-rectangle.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::KillRectangle;
                      });
    registry.Register("delete-rectangle", "Delete the rectangle defined by point and mark, without saving it.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DeleteRectangle;
                      });
    registry.Register("yank-rectangle", "Insert the last killed rectangle at point.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::YankRectangle;
    });
    registry.Register("string-rectangle",
                      "Replace the rectangle defined by point and mark with a typed string on every line.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::StringRectangle;
                      });

    registry.Register("narrow-to-region", "Restrict editing/display to the region defined by point and mark.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::NarrowToRegion;
                      });
    registry.Register("widen", "Remove any narrowing, restoring the full buffer.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::Widen;
    });

    // structural-selection-expansion follow-up: expand-selection needs
    // BufferView's own expansion-history stack (see BufferView.h) to shrink
    // back down correctly, so -- same reasoning as the rectangle/register/
    // narrowing commands just above -- this just signals intent rather than
    // acting directly on context.buffer.
    registry.Register("expand-selection",
                      "Grow the selection to the next enclosing syntax node (word, expression, statement, ...).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ExpandSelection;
                      });
    registry.Register("shrink-selection", "Shrink the selection back to the node it was expanded from.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ShrinkSelection;
                      });

    // LSP client follow-up: same direct "act on context.buffer, report
    // through context.message" shape the Org commands just below use --
    // Buffer::Diagnostics() needs nothing beyond the buffer itself, so
    // there's no reason to round-trip through an interactive session for
    // this either.
    // diagnostics-UX follow-up: falls back from "covering point exactly" to
    // "anywhere on point's line" -- the gutter icon is a per-LINE signal, so
    // a user pressing this while on a marked line expects the message, not
    // "No diagnostic at point." because point happens to sit one column off
    // the diagnostic's own span.
    registry.Register("lsp-show-diagnostic", "Show the LSP diagnostic message at point (or on point's line), if any.",
                      [](CommandContext& context) {
                          const std::size_t point = context.buffer.Point();
                          for (const text::Buffer::Diagnostic& diagnostic : context.buffer.Diagnostics()) {
                              const bool atPoint = (diagnostic.startByte == diagnostic.endByte)
                                                       ? (point == diagnostic.startByte)
                                                       : (diagnostic.startByte <= point && point < diagnostic.endByte);
                              if (atPoint) {
                                  if (context.message) {
                                      *context.message = diagnostic.message;
                                  }
                                  return;
                              }
                          }
                          const auto&       content   = context.buffer.Content();
                          const std::size_t line      = content.ByteOffsetToLine(point);
                          const std::size_t lineStart = content.LineToByteOffset(line);
                          const std::size_t lineEnd =
                              (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) : content.ByteLength();
                          const text::Buffer::Diagnostic* onLine    = nullptr;
                          std::size_t                     extraHere = 0;
                          for (const text::Buffer::Diagnostic& diagnostic : context.buffer.Diagnostics()) {
                              if (diagnostic.startByte >= lineStart && diagnostic.startByte < lineEnd) {
                                  if (onLine == nullptr) {
                                      onLine = &diagnostic;
                                  }
                                  else {
                                      ++extraHere;
                                  }
                              }
                          }
                          if (onLine != nullptr && context.message) {
                              *context.message = onLine->message;
                              if (extraHere > 0) {
                                  *context.message += " (+" + std::to_string(extraHere) + " more on this line)";
                              }
                              return;
                          }
                          if (context.message) {
                              *context.message = "No diagnostic at point.";
                          }
                      });

    // hover/completion follow-up. Async: the response arrives well after
    // this command function itself returns, via LspManager::RequestHover's
    // callback, so context.message can't be written synchronously the way
    // lsp-show-diagnostic's is -- captures the raw std::string* instead,
    // valid for as long as the owning BufferView is (see CommandContext::
    // lspManager's own doc comment in Command.h for why this is the same
    // accepted lifetime shape the diagnostics-publish handler already
    // relies on, not a new risk).
    registry.Register("lsp-hover", "Show hover information from the language server at point.",
                      [](CommandContext& context) {
                          if (!context.lspManager) {
                              if (context.message) {
                                  *context.message = "No LSP manager available.";
                              }
                              return;
                          }
                          std::string* message = context.message;
                          // embedded-language-documents follow-up: routes to
                          // point's own embedded server (e.g. "javascript"
                          // inside an HTML <script> block) via the uncached
                          // free-function resolver -- this command has no
                          // BufferView&/per-Paint() cache to reuse, unlike
                          // BufferView's own Request*AtPoint methods.
                          const std::string serverKey =
                              context.mode ? ResolveLspServerKey(*context.mode, context.buffer.Text(), context.buffer.Point())
                                           : std::string{};
                          context.lspManager->RequestHover(
                              context.buffer, context.buffer.Point(),
                              [message](std::optional<std::string> text) {
                                  if (message) {
                                      *message = text.value_or("No hover information available.");
                                  }
                              },
                              serverKey);
                      });

    // signature-help follow-up: same async-write-into-context.message shape
    // as lsp-hover just above, since ExtractSignatureHelp (LspContent.h)
    // already reduces the response to one plain, already-formatted string --
    // no BufferView-owned session needed, same reasoning lsp-hover's own
    // doc comment gives.
    registry.Register("lsp-signature-help", "Show parameter/signature information from the language server at point.",
                      [](CommandContext& context) {
                          if (!context.lspManager) {
                              if (context.message) {
                                  *context.message = "No LSP manager available.";
                              }
                              return;
                          }
                          std::string*      message = context.message;
                          const std::string serverKey =
                              context.mode ? ResolveLspServerKey(*context.mode, context.buffer.Text(), context.buffer.Point())
                                           : std::string{};
                          context.lspManager->RequestSignatureHelp(
                              context.buffer, context.buffer.Point(),
                              [message](std::optional<std::string> text) {
                                  if (message) {
                                      *message = text.value_or("No signature help available.");
                                  }
                              },
                              serverKey);
                      });

    // hover/completion follow-up: a one-shot direct action (see
    // InteractiveRequest::LspComplete's own doc comment in Command.h) --
    // BufferView::StartInteractiveSession is what actually sends the
    // textDocument/completion request and owns the resulting ghost-text
    // state, not this command.
    registry.Register("lsp-complete", "Request completion candidates from the language server at point.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspComplete;
                      });

    // documentHighlight follow-up: a one-shot direct action (see
    // InteractiveRequest::LspDocumentHighlight's own doc comment in
    // Command.h) -- BufferView::RequestDocumentHighlightAtPoint is what
    // actually sends the textDocument/documentHighlight request and owns the
    // resulting highlight state, not this command. No default binding, same
    // M-x-only precedent as lsp-signature-help above -- the live-on-cursor-
    // move behavior (MaybeScheduleDocumentHighlight) is what most users will
    // actually experience.
    registry.Register("lsp-document-highlight", "Highlight all occurrences of the symbol at point in this buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspDocumentHighlight;
                      });

    // code-actions follow-up: a one-shot direct action (see
    // InteractiveRequest::LspCodeAction's own doc comment in Command.h) --
    // BufferView::RequestCodeActionsAtPoint is what actually sends the
    // textDocument/codeAction request and owns the resulting select/confirm
    // session, not this command.
    registry.Register("lsp-code-action", "Show LSP code actions (quick fixes) available at point.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspCodeAction;
                      });

    // quick-fix follow-up: lsp-code-action without the ceremony -- applies
    // the single unambiguous fix at point immediately (undo is the safety
    // net), falling back to the selection list only when it's genuinely
    // ambiguous. See BufferView::RequestQuickFixAtPoint.
    registry.Register("lsp-quick-fix", "Apply the LSP quick fix at point immediately, no confirmation.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspQuickFix;
                      });

    // codeLens follow-up: unbound by default, same as expand-snippet --
    // M-x only. See BufferView::RequestCodeLensAtPoint's own doc comment
    // for the "only the first lens on the line" v1 scope cut.
    registry.Register("lsp-run-code-lens-at-point", "Run the code lens at point, resolving it first if needed.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspRunCodeLensAtPoint;
                      });

    // inline-diagnostics follow-up: a plain process-wide toggle, actable
    // right here (no BufferView state involved -- the very next Paint()
    // reads the flag fresh), so no InteractiveRequest is needed at all.
    // M-x reachable only, matching lsp-show-log just below.
    registry.Register("toggle-inline-diagnostics",
                      "Show or hide inline diagnostic annotation rows (carets + message under a line with a diagnostic).",
                      [](CommandContext& context) {
                          const bool enabled = !InlineDiagnosticsEnabled();
                          SetInlineDiagnosticsEnabled(enabled);
                          if (context.message) {
                              *context.message = enabled ? "Inline diagnostics on." : "Inline diagnostics off.";
                          }
                      });

    // error-visibility follow-up: no dedicated keybinding, M-x reachable --
    // matches org-agenda's own precedent (Commands.cpp/Command.h).
    registry.Register("lsp-show-log", "Switch to the *lsp log* buffer of LSP errors/disconnects.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspShowLog;
                      });

    // go-to-definition/rename follow-up: two more one-shot direct actions
    // (see InteractiveRequest::LspGotoDefinition/LspRename's own doc
    // comment in Command.h) -- BufferView owns the actual request/session.
    registry.Register("lsp-goto-definition", "Jump to the definition of the symbol at point, via the language server.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspGotoDefinition;
                      });

    // declaration/typeDefinition/implementation follow-up: three more
    // one-shot direct actions, identical in shape to lsp-goto-definition
    // (see InteractiveRequest::LspGotoDeclaration's own doc comment in
    // Command.h) -- deliberately unbound in the default keymap, same
    // "reachable via M-x only" precedent as lsp-show-log/lsp-diagnostics-buffer,
    // since none of the three has an established default binding to borrow
    // the way M-. does for goto-definition.
    registry.Register("lsp-goto-declaration", "Jump to the declaration of the symbol at point, via the language server.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspGotoDeclaration;
                      });
    registry.Register("lsp-goto-type-definition", "Jump to the type definition of the symbol at point, via the language server.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspGotoTypeDefinition;
                      });
    registry.Register("lsp-goto-implementation", "Jump to the implementation of the symbol at point, via the language server.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspGotoImplementation;
                      });

    // symbol-search follow-up: two more one-shot direct actions --
    // BufferView owns the actual request/picker session for both (see
    // InteractiveRequest::LspGotoSymbol/LspWorkspaceSymbol's own doc
    // comment in Command.h).
    registry.Register("lsp-goto-symbol", "Jump to a symbol in the current buffer, via the language server (textDocument/documentSymbol).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspGotoSymbol;
                      });
    registry.Register("lsp-workspace-symbol", "Search for a symbol across the whole project, via the language server (workspace/symbol).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspWorkspaceSymbol;
                      });

    // call/type-hierarchy follow-up: four more one-shot direct actions --
    // BufferView::RequestHierarchyAtPoint owns the actual prepare/expand/
    // browse session for all four (see InteractiveRequest::
    // LspCallHierarchyIncoming's own doc comment in Command.h).
    registry.Register("lsp-call-hierarchy-incoming", "Show callers of the symbol at point, via the language server (callHierarchy/incomingCalls).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspCallHierarchyIncoming;
                      });
    registry.Register("lsp-call-hierarchy-outgoing", "Show what the symbol at point calls, via the language server (callHierarchy/outgoingCalls).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspCallHierarchyOutgoing;
                      });
    registry.Register("lsp-type-hierarchy-supertypes", "Show supertypes of the symbol at point, via the language server (typeHierarchy/supertypes).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspTypeHierarchySupertypes;
                      });
    registry.Register("lsp-type-hierarchy-subtypes", "Show subtypes of the symbol at point, via the language server (typeHierarchy/subtypes).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspTypeHierarchySubtypes;
                      });

    registry.Register("lsp-rename", "Rename the symbol at point across every file the language server reports it in.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspRename;
                      });

    // prepareRename/linkedEditingRange follow-up: see InteractiveRequest::
    // LspLinkedEditingRange's own doc comment in Command.h -- BufferView::
    // RequestLinkedEditingRangeAtPoint owns the actual request and the
    // resulting live-mirroring session.
    registry.Register("lsp-linked-editing-range",
                      "Start live-mirrored editing across every range the language server reports as linked to "
                      "the one at point (e.g. a markup element's matching opening/closing tag name).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspLinkedEditingRange;
                      });

    // header-source-switching follow-up: one more one-shot direct action
    // (see InteractiveRequest::SwitchHeaderSource's own doc comment in
    // Command.h) -- BufferView::SwitchHeaderSource owns the actual
    // request/fallback logic.
    registry.Register("switch-header-source", "Switch between a C/C++ header and its implementation file.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::SwitchHeaderSource;
                      });

    // jump-back-stack follow-up: one-shot direct action, same shape as
    // switch-header-source above (see InteractiveRequest::JumpBack's own doc
    // comment in Command.h) -- BufferView::JumpBack owns the actual pop/restore.
    registry.Register("jump-back",
                      "Jump back to the position before the last location-jumping command "
                      "(goto-definition, goto-line, bookmark-jump, jump-to-register, ...).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::JumpBack;
                      });

    // jump-back-stack follow-up, forward direction: jump-back's redo --
    // BufferView::JumpForward owns the actual pop/restore, retracing
    // whatever jump-back most recently backed out of.
    registry.Register("jump-forward",
                      "Jump forward again after jump-back -- the redo direction of jump-back.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::JumpForward;
                      });

    // task-runner follow-up: two more prompt-shaped one-shot requests, same
    // "just signal intent" shape as every InteractiveRequest-routed command
    // above -- BufferView::HandlePromptKey's InputMode::TaskName case is
    // what actually calls TaskRunner::RunTask/CancelTask, see that method
    // and Editor/Tasks/TaskRunner.h.
    registry.Register("run-task", "Run a Janet-configured task (see ned/set-task-command), streaming its output into a buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::RunTask;
                      });
    registry.Register("cancel-task", "Cancel a running task started by run-task.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::CancelTask;
    });

    // test-runner integration: one-shot direct actions (no prompt -- one
    // project-wide test command, see Editor/TestRun/TestRunConfig.h), same
    // "just signal intent" shape as run-task/cancel-task above --
    // BufferView holds the shared TestRunner and does the actual work.
    registry.Register("run-tests",
                      "Run the project's tests (see ned/set-test-command), streaming output into *test output* and "
                      "parsing results into *test results* and the per-test gutter marks.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::RunTests;
                      });
    registry.Register("cancel-tests", "Cancel the test run started by run-tests.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::CancelTests;
    });
    registry.Register("show-test-results", "Show the parsed failures from the last test run (*test results*).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ShowTestResults;
                      });
    registry.Register("run-test-at-point",
                      "Run only the test definition containing point (Mode::testDiscovery), through the configured "
                      "filter template (see ned/set-test-filter-command).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::RunTestAtPoint;
                      });
    registry.Register("rerun-failed-tests",
                      "Re-run every currently-failed test, one filtered run per test, merging the results.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::RerunFailedTests;
                      });

    // DAP client slice 1: four one-shot direct actions, same "just set
    // interactiveRequest" shape as run-task/cancel-task above --
    // BufferView holds the shared DapManager and does the actual work (see
    // its StartInteractiveSession Dap* cases). Adapter and launch
    // configuration both come from init.janet (ned/set-dap-adapter,
    // ned/set-dap-launch) -- see Editor/Dap/DapConfig.h.
    registry.Register("dap-continue", "Start a debug session for the active language, or continue a stopped one.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapContinue;
                      });
    registry.Register("dap-stop", "Stop the running debug session.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::DapStop;
    });
    registry.Register("dap-pause", "Pause the running debuggee.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::DapPause;
    });
    registry.Register("dap-toggle-breakpoint", "Toggle a breakpoint on the current line.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::DapToggleBreakpoint;
    });
    // DAP slices 2/3: stepping, the *debug* inspection buffer, and
    // expression evaluation -- same shapes as the four above (DapEvaluate
    // is the one prompt-shaped member, see Command.h).
    registry.Register("dap-step-over", "Step over the current line in the stopped debug session.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapStepOver;
                      });
    registry.Register("dap-step-into", "Step into the call on the current line in the stopped debug session.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapStepInto;
                      });
    registry.Register("dap-step-out", "Step out of the current function in the stopped debug session.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapStepOut;
                      });
    registry.Register("dap-show-debug", "Show the stopped debug session's stack and variables in a *debug* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapShowDebug;
                      });
    registry.Register("dap-expand-variable", "Expand the composite variable on the current *debug* buffer line.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapExpandVariable;
                      });
    registry.Register("dap-evaluate", "Evaluate an expression in the stopped debug session's top frame.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapEvaluate;
                      });

    // DAP round 2: conditional/logpoint breakpoints, watch expressions, the
    // thread picker, editable *debug* buffer variables, and the debug
    // console panel toggle -- M-x only (matching the F-key-quartet-only
    // policy above), except dap-toggle-console (bound to C-c D below, a
    // frequently-toggled panel getting a real binding the same way
    // toggle-terminal/acp-toggle-panel did).
    registry.Register("dap-set-breakpoint-condition", "Set or clear a condition on the breakpoint at the current line.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapSetBreakpointCondition;
                      });
    registry.Register("dap-set-breakpoint-log-message",
                      "Set or clear a log message on the breakpoint at the current line (a logpoint never halts the debuggee).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapSetBreakpointLogMessage;
                      });
    registry.Register("dap-add-watch", "Add a watch expression, re-evaluated every time the *debug* buffer is rebuilt.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapAddWatch;
                      });
    registry.Register("dap-remove-watch", "Remove the watch expression on the current *debug* buffer line.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapRemoveWatch;
                      });
    registry.Register("dap-select-thread", "Pick which thread inspection/stepping/continue target in the stopped debug session.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapSelectThread;
                      });
    registry.Register("dap-set-variable", "Edit the variable's value on the current *debug* buffer line.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::DapSetVariable;
    });
    registry.Register("dap-toggle-console", "Show or hide the debug console (REPL) panel.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::DapToggleConsole;
    });
    registry.Register("dap-toggle-threads",
                      "Show or hide the live threads panel (refreshes on every stop, unlike dap-select-thread's one-shot picker).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapToggleThreadsPanel;
                      });

    // Debugging wishlist: show-massif-graph -- prompts for a massif.out.<pid> file
    // (Valgrind's --tool=massif output), then renders a heap-usage-over-time
    // sparkline plus a per-snapshot summary table. M-x only, no keybinding --
    // standalone against Valgrind's own output file, not tied to a live debug
    // session (unlike everything else on this dap-* list).
    registry.Register("show-massif-graph", "Parse a Valgrind massif.out file and show a heap-usage-over-time graph.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ShowMassifGraph;
                      });

    // DAP round 3: hit-count/function/exception breakpoints (SetBreakpointCondition's own
    // pattern extended to two more DAP breakpoint kinds) and attach mode (StartOrContinue's
    // own launch path, sibling entry point) -- M-x only, same policy as DAP round 2 above.
    registry.Register("dap-set-breakpoint-hit-condition",
                      "Set or clear a hit-count condition on the breakpoint at the current line "
                      "(e.g. \"> 5\" -- only stops once satisfied).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapSetBreakpointHitCondition;
                      });
    registry.Register("dap-toggle-function-breakpoint", "Toggle a breakpoint on a named function, by prompted name.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapToggleFunctionBreakpoint;
                      });
    registry.Register("dap-select-exception-breakpoints",
                      "Toggle which of the adapter's advertised exception filters halt the debuggee.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapSelectExceptionBreakpoints;
                      });
    registry.Register("dap-attach", "Attach a debug session to a running process for the active language (ned/set-dap-attach).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapAttach;
                      });

    // DAP round 4: reruns the stopped thread from the top of the *debug*
    // buffer stack frame at point (DAP's restartFrame request).
    registry.Register("dap-restart-frame", "Restart execution from the top of the stack frame on the current *debug* buffer line.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapRestartFrame;
                      });

    // DAP round 5: disassembly/memory view -- M-x only, same policy as every
    // other DAP inspection command beyond the core F-key quartet.
    registry.Register("dap-show-disassembly",
                      "Show instructions around the stopped frame's program counter in a *disassembly* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapShowDisassembly;
                      });
    registry.Register("dap-show-memory-at-point",
                      "Show a hex dump of memory for the variable on the current *debug* buffer line in a *memory* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapShowMemoryAtPoint;
                      });
    registry.Register("dap-show-memory-image-at-point",
                      "Show a grayscale image of memory for the variable on the current *debug* buffer line "
                      "(repeating structures/zero-fill/embedded text visible at a glance, gf's Data tab).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapShowMemoryImageAtPoint;
                      });

    // Debugging wishlist: run-to-cursor -- M-x only, same policy as every
    // other DAP command beyond the core F-key quartet.
    registry.Register("dap-run-to-cursor",
                      "Run the stopped debug session until it reaches the current line (a temporary breakpoint, "
                      "cleared again on the next stop).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapRunToCursor;
                      });
    registry.Register("dap-jump-to-line",
                      "Move the stopped thread's execution point directly to the current line, without running "
                      "through the skipped code (adapter support required, DAP's gotoTargets/goto requests).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapJumpToLine;
                      });
    registry.Register("dap-toggle-hex-format",
                      "Toggle hex display for the watch or variable value on the current *debug* buffer line.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapToggleHexFormat;
                      });
    registry.Register("dap-toggle-watch-graph",
                      "Toggle a sparkline (scalar watch history) or bar chart (numeric array watch) on the "
                      "current *debug* buffer watch line.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapToggleWatchGraph;
                      });
    registry.Register("dap-line-inspect",
                      "Evaluate every sub-expression on the current line in the stopped debug session at once.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapLineInspect;
                      });

    // Debugging wishlist: reverse debugging -- M-x only, same policy as
    // every other DAP command beyond the core F-key quartet. Only useful
    // against an adapter that itself replays a recording (e.g. rr); a
    // typical adapter just never advertises support and these no-op.
    registry.Register("dap-reverse-continue", "Continue the stopped debug session backwards (adapter support required).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapReverseContinue;
                      });
    registry.Register("dap-step-back", "Step the stopped debug session backwards one line (adapter support required).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapStepBack;
                      });

    // Debugging wishlist: pointer/linked-list graph view -- same "just set
    // interactiveRequest" shape as dap-expand-variable, operating on the
    // same [ref:N] *debug* buffer line, but opens it as a lazily-expandable
    // TreeView graph instead of splicing more inline text.
    registry.Register("dap-show-pointer-graph",
                      "Browse the composite variable on the current *debug* buffer line as an expandable pointer/field graph.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DapShowPointerGraph;
                      });

    // ACP client slice 2: same "just set interactiveRequest" shape as
    // run-task/dap-continue above -- BufferView holds the shared AcpManager
    // and does the actual work (see Editor/Acp/AcpManager.h). Agent and
    // launch command both come from init.janet (ned/set-acp-agent).
    registry.Register("acp-start-session", "Start an Agent Client Protocol (ACP) session with a configured agent, streaming into a buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::AcpStartSession;
                      });
    registry.Register("acp-send-prompt", "Send a message to the active ACP session.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::AcpSendPrompt;
    });
    registry.Register("acp-stop-session", "Stop the active ACP session.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::AcpStopSession;
    });
    // ACP chat panel follow-up: same "just set interactiveRequest" shape as
    // toggle-terminal -- WindowManager owns the actual OverlayHost panel.
    registry.Register("acp-toggle-panel", "Show, focus, or hide the ACP chat panel.", [](CommandContext& context) {
        context.interactiveRequest = InteractiveRequest::AcpTogglePanel;
    });
    // ACP checkpoint/rewind follow-up: same "just set interactiveRequest"
    // shape as acp-toggle-panel above -- the actual picker lives in
    // AcpPanel (an OverlayHost overlay above this class), see Command.h's
    // own AcpRewind doc comment for the full forwarding chain.
    registry.Register("acp-rewind", "Rewind the ACP conversation and its file edits to before an earlier turn.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::AcpRewind;
                      });

    // VCS blame gutter follow-up: same "just set interactiveRequest" shape
    // as lsp-show-log/run-task above -- BufferView owns the actual
    // VcsRunner request. vcs-show-blame stays on the current buffer,
    // populating only the gutter -- the primary, "inline where you're
    // already reading" action.
    registry.Register("vcs-show-blame", "Show per-line commit attribution for the current file, inline in the gutter.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsShowBlame;
                      });
    // Reads already-loaded gutter data for point's current line -- no new
    // request -- and reports the full author/date/summary via the status
    // line, since the gutter's own fixed-width column only ever fits a
    // short hash.
    registry.Register("vcs-blame-detail-at-point", "Show full commit info (author/date/summary) for the blamed line at point.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsBlameDetailAtPoint;
                      });
    // The full-history views -- M-x reachable only, same "no dedicated
    // binding needed" precedent lsp-show-log established, now that
    // vcs-show-blame's own default no longer switches buffers.
    registry.Register("vcs-blame-buffer", "Show per-line commit attribution for the current file in a *vcs blame* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsBlameBuffer;
                      });
    registry.Register("vcs-show-log", "Show commit history for the current file in a *vcs log* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsShowLog;
                      });
    // A no-op everywhere except a *vcs blame* results buffer -- safe to bind
    // globally, same convention project-search-visit-result already
    // established.
    registry.Register("vcs-visit-result", "Jump to the file:line under point in a *vcs blame* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VisitVcsResult;
                      });

    // VCS vocabulary-completion follow-up: status/stage/unstage/commit/
    // branch, all the same "just set interactiveRequest" shape -- see
    // Command.h's own enum comment for which are one-shot and which drive
    // a prompt.
    registry.Register("vcs-status", "Show the working tree's changed/untracked files in a *vcs status* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsStatus;
                      });
    registry.Register("vcs-stage-file", "Stage the file on the *vcs status* line at point, or the current file.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsStageFile;
                      });
    registry.Register("vcs-unstage-file", "Unstage the file on the *vcs status* line at point, or the current file.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsUnstageFile;
                      });
    registry.Register("vcs-commit", "Commit the staged changes -- opens a *vcs commit message* buffer to compose in.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsCommit;
                      });
    // multi-line-commit-message follow-up: only ever reachable via the
    // commit-message buffer's own Mode-local keymap (see this function's
    // ned::editor::RegisterMode/SetModeForFilename calls below), so these
    // never appear on M-x or the global keymap.
    registry.Register("vcs-commit-finish", "Finish composing and commit (bound C-c C-c in *vcs commit message*).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsCommitFinish;
                      });
    registry.Register("vcs-commit-abort", "Discard the in-progress commit message (bound C-c C-k in *vcs commit message*).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsCommitAbort;
                      });
    // multi-line-commit-message follow-up: registered once, here, so it's
    // ready before the first vcs-commit ever runs -- a plain keymap-only
    // Mode (no highlighting/folding of its own), resolved via
    // ModeForBuffer/ModeForPath the instant BeginVcsCommitMessage switches
    // to the commit-message buffer, since that buffer's real (if
    // disposable) path is exactly kVcsCommitMessageFilename (see
    // Editor/Vcs/VcsRunner.h's own doc comment on why a real path is
    // required for this to resolve at all). wrapLines/lineCommentPrefix
    // mirror MarkdownMode's own prose-buffer defaults -- a commit message
    // is prose, and '#' is git's own comment convention.
    {
        Mode commitMode;
        commitMode.name              = "vcs-commit-message-mode";
        commitMode.lineCommentPrefix = "#";
        commitMode.wrapLines         = true;
        commitMode.keymap.Bind(ParseKeySequence("C-c C-c"), "vcs-commit-finish");
        commitMode.keymap.Bind(ParseKeySequence("C-c C-k"), "vcs-commit-abort");
        RegisterMode("vcs-commit-message-mode", std::move(commitMode));
        SetModeForFilename(std::string(vcs::kVcsCommitMessageFilename), "vcs-commit-message-mode");
    }
    registry.Register("vcs-stage-hunk", "Stage just the change hunk covering the line at point.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsStageHunk;
                      });
    registry.Register("vcs-unstage-hunk", "Unstage the staged hunk covering the line at point.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsUnstageHunk;
                      });
    // Hunk-navigation follow-up: gitsigns' ]c/[c convention, Emacs-style
    // naming/binding to match every other vcs-* command here. Pure point
    // motion, no staging -- doesn't share vcs-stage-hunk's Modified() gate.
    registry.Register("vcs-next-hunk", "Move point to the next changed hunk in this buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsNextHunk;
                      });
    registry.Register("vcs-previous-hunk", "Move point to the previous changed hunk in this buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsPreviousHunk;
                      });
    // Multibuffers follow-up: M-x/keybinding reachable, same shape as
    // vcs-blame-buffer/vcs-show-log above.
    registry.Register("vcs-full-diff-buffer", "Show every changed file's real diff, stitched into one *vcs diff* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsFullDiffBuffer;
                      });
    // Diagnostics-multibuffer follow-up: M-x reachable only, same "no
    // dedicated binding needed" precedent vcs-blame-buffer/vcs-show-log
    // established -- this is a "look around the whole project" companion to
    // lsp-show-diagnostic's own point-based, single-buffer lookup.
    registry.Register("lsp-diagnostics-buffer", "Show every open buffer's LSP diagnostics, stitched into one *diagnostics* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::DiagnosticsBuffer;
                      });
    // M-x only, like vcs-blame-buffer -- vcs-switch-branch's own prompt
    // already Tab-completes against the real branch list, so the buffer
    // view is the "look around" companion, not the primary path.
    registry.Register("vcs-branches", "List branches in a *vcs branches* buffer.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsBranches;
                      });
    registry.Register("vcs-switch-branch", "Switch to another branch, with Tab completion over the branch list.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsSwitchBranch;
                      });
    registry.Register("vcs-create-branch", "Create and switch to a new branch, prompting for its name.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsCreateBranch;
                      });

    // Org's three editing commands act directly on context.buffer, unlike
    // the interactiveRequest-routed commands above (rectangle/register/
    // narrowing) -- those need state (RectangleClipboard, RegisterTable, or
    // BufferView's own viewport for narrowing's post-edit scroll) that only
    // lives on BufferView, but org::Cycle*AtPoint/ToggleCheckboxAtPoint need
    // nothing beyond the buffer itself, so there's no reason to round-trip
    // through an interactive session for them -- same direct "do the work,
    // report through context.message" shape save-buffer already uses.
    registry.Register("org-cycle-todo", "Cycle the TODO keyword of the headline at point.",
                      [](CommandContext& context) {
                          if (!org::CycleTodoKeywordAtPoint(context.buffer) && context.message) {
                              *context.message = "Not on a headline.";
                          }
                      });
    registry.Register("org-cycle-priority", "Cycle the [#A]/[#B]/[#C] priority cookie of the headline at point.",
                      [](CommandContext& context) {
                          if (!org::CyclePriorityAtPoint(context.buffer) && context.message) {
                              *context.message = "Not on a headline.";
                          }
                      });
    registry.Register("org-toggle-checkbox", "Toggle the checkbox at point, reflecting the change up into any parent.",
                      [](CommandContext& context) {
                          if (!org::ToggleCheckboxAtPoint(context.buffer) && context.message) {
                              *context.message = "Not on a checkbox.";
                          }
                      });
    // Clocking follow-up: same direct "act on context.buffer, report
    // through context.message" shape as the three commands above -- neither
    // needs a prompt (clock-in/out take no free-form input, matching real
    // Org's own org-clock-in/org-clock-out).
    registry.Register("org-clock-in", "Clock in to the Org headline at point.", [](CommandContext& context) {
        const auto result = org::ClockInAtPoint(context.buffer);
        if (!context.message)
            return;
        switch (result.status) {
            case org::ClockInStatus::Ok:
                break;
            case org::ClockInStatus::NotOnHeadline:
                *context.message = "Not on a headline.";
                break;
            case org::ClockInStatus::AlreadyRunningHere:
                *context.message = "Already clocked in here.";
                break;
            case org::ClockInStatus::AlreadyRunningElsewhere:
                *context.message = "Already clocked in on \"" + result.otherHeadlineTitle + "\"; clock out first.";
                break;
        }
    });
    registry.Register("org-clock-out", "Clock out of whichever headline currently has a running clock.",
                      [](CommandContext& context) {
                          if (org::ClockOut(context.buffer) == org::ClockOutStatus::NoRunningClock && context.message) {
                              *context.message = "No running clock.";
                          }
                      });
    // org-clock-display follow-up: same "just set interactiveRequest" shape
    // as org-agenda -- switching this pane's own active buffer needs
    // activeBuffer_, which only BufferView has (BuildAgendaMultibuffer's own
    // reasoning).
    registry.Register("org-clock-report", "List every headline in the current buffer with clocked time, including subtree totals.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::OrgClockReport;
                      });
    // Real Org's own command name -- same direct "act on context.buffer,
    // report through context.message" shape as the three commands above.
    // Tables follow-up: TAB is a single, context-dispatching command in
    // real Emacs Org too, not two competing bindings -- this is that same
    // idiom, not a workaround. AlignOrgTableAtPoint is only ever tried once
    // CycleFoldAtPoint has already reported point isn't on a headline, so a
    // point that's genuinely on both a headline line and inside a table
    // (can't really happen -- a headline is never itself a table row) has
    // no ambiguity to resolve.
    registry.Register("org-cycle", "Cycle the fold state of the subtree at point, or realign the table at point.",
                      [](CommandContext& context) {
                          if (org::CycleFoldAtPoint(context.buffer))
                              return;
                          if (org::AlignOrgTableAtPoint(context.buffer))
                              return;
                          if (context.message) {
                              *context.message = "Not on a headline.";
                          }
                      });
    // generic-code-folding follow-up: the keyboard/M-x/Janet path onto the
    // same gutter-click fold BufferView drives directly (see that class's
    // own OnMouseEvent) -- not bound to any default key, since there's no
    // obviously-free global chord to claim; reachable via M-x or
    // ned/define-key. Needs context.mode (unlike org-cycle, which is
    // entirely self-contained) since which blocks are foldable depends on
    // the active Mode's own fold query -- see CommandContext::mode's own
    // doc comment.
    registry.Register(
        "code-fold-toggle", "Toggle folding the code block starting on the line at point.", [](CommandContext& context) {
            if (context.mode == nullptr || !context.mode->fold) {
                if (context.message) {
                    *context.message = "No folding available in this mode.";
                }
                return;
            }
            const text::ITextStorage& content = context.buffer.Content();
            const std::size_t line    = content.ByteOffsetToLine(context.buffer.Point());
            const auto        blocks  = codefold::FoldableBlocks(*context.mode, context.buffer.Text());
            if (!codefold::ToggleFoldAtLine(context.buffer, content, blocks, line) && context.message) {
                *context.message = "No foldable block starts here.";
            }
        });
    // Emacs-keymap-round-2 follow-up: needs context.mode for the same
    // reason code-fold-toggle above does -- structural motion depends on
    // the active Mode's own parsed syntax tree (Mode::sexpMotion), not
    // something Buffer/Text can compute on its own.
    registry.Register(
        "forward-sexp", "Move point forward over one balanced expression, using the active mode's syntax tree.",
        PerCursor([](CommandContext& context) {
            if (context.mode == nullptr || !context.mode->sexpMotion) {
                if (context.message) {
                    *context.message = "No sexp motion available in this mode.";
                }
                return;
            }
            if (const auto target = context.mode->sexpMotion(context.buffer.Text(), context.buffer.Point(), true)) {
                context.buffer.SetPoint(*target);
            }
        }));
    registry.Register(
        "backward-sexp", "Move point backward over one balanced expression, using the active mode's syntax tree.",
        PerCursor([](CommandContext& context) {
            if (context.mode == nullptr || !context.mode->sexpMotion) {
                if (context.message) {
                    *context.message = "No sexp motion available in this mode.";
                }
                return;
            }
            if (const auto target = context.mode->sexpMotion(context.buffer.Text(), context.buffer.Point(), false)) {
                context.buffer.SetPoint(*target);
            }
        }));

    // toggle-line-comment follow-up: needs context.mode for the same
    // reason code-fold-toggle above does -- comment-prefix-per-language is
    // a Mode property (Mode::lineCommentPrefix), not a Command one.
    // Operates on every line the mark's region spans if one is set,
    // otherwise just the current line -- standard multi-line toggle
    // convention, matching how a region-scoped command already behaves
    // elsewhere in this file (kill-region/kill-ring-save). A blank
    // (whitespace-only) line is left untouched even when it falls inside
    // the range, same "nothing meaningful to toggle there" reasoning
    // real editors use. If *any* non-blank line in range is still
    // uncommented, the action is "comment" (every uncommented line in
    // range gets the prefix, already-commented ones are left alone rather
    // than double-prefixed); only when *every* non-blank line is already
    // commented does the action flip to "uncomment." Processes bottom-to-
    // top so editing an earlier line's byte offsets never invalidates a
    // later line's already-computed ones -- point/mark relocate correctly
    // regardless, via Buffer::InsertAt/DeleteRange's own existing
    // relocation, not tracked separately here.
    registry.Register("toggle-line-comment", "Comment or uncomment the current line, or every line the region spans.",
                      [](CommandContext& context) {
                          if (context.mode == nullptr || context.mode->lineCommentPrefix.empty()) {
                              if (context.message) {
                                  *context.message = "No comment syntax configured for this mode.";
                              }
                              return;
                          }
                          const std::string& prefix = context.mode->lineCommentPrefix;

                          text::Buffer&     buffer  = context.buffer;
                          // Safe to capture once here -- only read-only
                          // uses below (line-range resolution, the first
                          // pass' own scan) ever touch it; the second
                          // pass's mutating loop re-fetches its own fresh
                          // reference per iteration instead (see there).
                          const text::ITextStorage& content = buffer.Content();
                          std::size_t       firstLine, lastLine;
                          if (buffer.HasMark()) {
                              const auto [start, end] = buffer.Region();
                              firstLine               = content.ByteOffsetToLine(start);
                              lastLine                = content.ByteOffsetToLine(end);
                              // A region end sitting exactly at the start of
                              // a line (e.g. two S-DOWN presses from column
                              // 0) shouldn't pull that line into the range --
                              // matches Emacs' own comment-region convention
                              // for the same "off by one" gotcha.
                              if (lastLine > firstLine && end == content.LineToByteOffset(lastLine)) {
                                  --lastLine;
                              }
                          }
                          else {
                              firstLine = lastLine = content.ByteOffsetToLine(buffer.Point());
                          }
                          buffer.ClearMark();

                          // First pass (read-only): is every non-blank line
                          // in range already commented?
                          bool anyUncommented = false;
                          for (std::size_t line = firstLine; line <= lastLine; ++line) {
                              const std::size_t start  = content.LineToByteOffset(line);
                              const std::size_t end    = LineContentEnd(content, start);
                              const std::string text   = content.Substring(start, end - start);
                              const std::size_t indent = text.find_first_not_of(" \t");
                              if (indent == std::string::npos) {
                                  continue; // blank line -- doesn't count either way
                              }
                              if (text.compare(indent, prefix.size(), prefix) != 0) {
                                  anyUncommented = true;
                                  break;
                              }
                          }
                          const bool shouldComment = anyUncommented;

                          // Second pass (bottom-to-top, so earlier lines'
                          // offsets are never invalidated by editing a
                          // later one): apply the toggle. Re-fetches
                          // buffer.Content() fresh each iteration -- a real,
                          // ASan-caught heap-use-after-free otherwise: each
                          // InsertAt/DeleteRange below replaces the buffer's
                          // internal storage (Buffer::InsertAtImpl), which
                          // frees whatever the OUTER `content` reference
                          // (captured once, above the first pass) still
                          // pointed at, the moment the very first mutation
                          // in this loop runs.
                          for (std::size_t line = lastLine + 1; line-- > firstLine;) {
                              const text::ITextStorage& lineContent = buffer.Content();
                              const std::size_t start  = lineContent.LineToByteOffset(line);
                              const std::size_t end    = LineContentEnd(lineContent, start);
                              const std::string text   = lineContent.Substring(start, end - start);
                              const std::size_t indent = text.find_first_not_of(" \t");
                              if (indent == std::string::npos) {
                                  continue;
                              }
                              const bool isCommented = text.compare(indent, prefix.size(), prefix) == 0;
                              if (shouldComment && !isCommented) {
                                  buffer.InsertAt(start + indent, prefix + " ");
                              }
                              else if (!shouldComment && isCommented) {
                                  std::size_t removeLength = prefix.size();
                                  if (indent + prefix.size() < text.size() && text[indent + prefix.size()] == ' ') {
                                      ++removeLength; // symmetric with the inserted "<prefix> "
                                  }
                                  buffer.DeleteRange(start + indent, removeLength);
                              }
                          }
                      });

    // fill-paragraph follow-up: needs context.mode the same way
    // toggle-line-comment above does, for the same reason -- whether the
    // paragraph at point is comment-prefixed is a Mode property
    // (Mode::lineCommentPrefix), the algorithm itself (Editor/Fill.h) has
    // no Mode dependency of its own.
    registry.Register(
        "fill-paragraph",
        "Reflow the paragraph at point to fill-column, preserving indentation and (if uniform) a per-line comment prefix.",
        [](CommandContext& context) {
            const std::string prefix = (context.mode != nullptr) ? context.mode->lineCommentPrefix : std::string();
            FillParagraph(context.buffer, static_cast<std::size_t>(FillColumn()), prefix);
        });

    // smart-indentation follow-up: the batch/linter-reuse half of
    // Editor/Indent.h -- the same per-line primitive indent-for-tab-command/
    // newline drive interactively, looped over a range. Requires a mark, the
    // same "no mark, no-op" convention kill-region already established
    // (there is no "whole buffer" fallback here -- that's indent-buffer's
    // own, separate job below).
    registry.Register("indent-region", "Reindent every line the region between point and mark spans.",
                      [](CommandContext& context) {
                          if (context.mode == nullptr || !context.mode->indentColumn) {
                              if (context.message) {
                                  *context.message = "No indent rules configured for this mode.";
                              }
                              return;
                          }
                          if (!context.buffer.HasMark()) {
                              if (context.message) {
                                  *context.message = "No region selected.";
                              }
                              return;
                          }
                          const auto [start, end] = context.buffer.Region();
                          const auto&        content   = context.buffer.Content();
                          const std::size_t  startLine = content.ByteOffsetToLine(start);
                          const std::size_t  endLine   = content.ByteOffsetToLine(end) + 1; // exclusive
                          context.buffer.ClearMark();
                          const std::size_t changed = IndentRegion(context.buffer, *context.mode, startLine, endLine);
                          if (context.message) {
                              *context.message = std::to_string(changed) + " line(s) reindented.";
                          }
                      });

    registry.Register("indent-buffer", "Reindent the whole buffer to its computed indentation.", [](CommandContext& context) {
        if (context.mode == nullptr || !context.mode->indentColumn) {
            if (context.message) {
                *context.message = "No indent rules configured for this mode.";
            }
            return;
        }
        const std::size_t changed = IndentBuffer(context.buffer, *context.mode);
        if (context.message) {
            *context.message = std::to_string(changed) + " line(s) reindented.";
        }
    });

    // Real Org's own C-c C-q ("org-set-tags-command"): unlike the direct
    // commands above, tags are free-form text, so this just checks the
    // precondition (same "Not on a headline." report) and hands off to a
    // real prompt -- BufferView::StartInteractiveSession's own
    // SetHeadlineTags case pre-fills it with the headline's current tags.
    registry.Register("org-set-tags", "Set the tags of the headline at point (colon-separated, e.g. \"work:urgent\").",
                      [](CommandContext& context) {
                          if (org::HeadlineAtPoint(context.buffer)) {
                              context.interactiveRequest = InteractiveRequest::SetHeadlineTags;
                          }
                          else if (context.message) {
                              *context.message = "Not on a headline.";
                          }
                      });
    // Property drawers follow-up: real Org's own C-c C-x p
    // ("org-set-property") -- same "check the precondition, hand off to a
    // real prompt" shape as org-set-tags above, but a two-stage prompt
    // (property name, then value) since unlike tags there's no single
    // colon-separated line to type in one go. See
    // BufferView::HandleSetPropertyKey for the actual two-stage session.
    registry.Register("org-set-property", "Set a property of the headline at point (prompts for name, then value).",
                      [](CommandContext& context) {
                          if (org::HeadlineAtPoint(context.buffer)) {
                              context.interactiveRequest = InteractiveRequest::SetProperty;
                          }
                          else if (context.message) {
                              *context.message = "Not on a headline.";
                          }
                      });
    // The delete-property mirror -- one prompt (the property name), so this
    // fits the shared SetHeadlineTags-shaped prompt flow directly.
    registry.Register("org-delete-property", "Delete a property from the headline at point.",
                      [](CommandContext& context) {
                          if (org::HeadlineAtPoint(context.buffer)) {
                              context.interactiveRequest = InteractiveRequest::DeleteProperty;
                          }
                          else if (context.message) {
                              *context.message = "Not on a headline.";
                          }
                      });
    // Scheduling/recurrence follow-up: real Org's own C-c C-s
    // ("org-schedule")/C-c C-d ("org-deadline") -- same "check the
    // precondition, hand off to a real prompt" shape as org-set-tags/
    // org-set-property above. See BufferView::StartInteractiveSession's
    // OrgSchedule/OrgDeadline cases for the prompt's own pre-fill.
    registry.Register("org-schedule", "Set the SCHEDULED: timestamp of the headline at point.",
                      [](CommandContext& context) {
                          if (org::HeadlineAtPoint(context.buffer)) {
                              context.interactiveRequest = InteractiveRequest::OrgSchedule;
                          }
                          else if (context.message) {
                              *context.message = "Not on a headline.";
                          }
                      });
    registry.Register("org-deadline", "Set the DEADLINE: timestamp of the headline at point.",
                      [](CommandContext& context) {
                          if (org::HeadlineAtPoint(context.buffer)) {
                              context.interactiveRequest = InteractiveRequest::OrgDeadline;
                          }
                          else if (context.message) {
                              *context.message = "Not on a headline.";
                          }
                      });
    // Capture-templates follow-up: real Org's own org-capture, minus its
    // precondition -- unlike every other org-* command above, this must work
    // from any buffer, not just when point is on a headline (you capture
    // *into* an org file from wherever you happen to be). See
    // BufferView::StartInteractiveSession's OrgCapture case for the actual
    // one-keystroke template-selection session.
    registry.Register("org-capture", "Capture a note into a registered org-capture template.",
                      [](CommandContext& context) { context.interactiveRequest = InteractiveRequest::OrgCapture; });
    // Tables follow-up: registered separately from org-cycle's own
    // fallback branch above too -- M-x / explicit binding / Janet
    // scripting reachability, the same "manually invoked, available
    // regardless of the default keybinding" shape every command in this
    // registry already has (CommandRegistry is one global namespace; Mode
    // only gates default keybindings, never which commands can run).
    registry.Register("org-table-align", "Realign the columns of the table at point to their content width.",
                      [](CommandContext& context) {
                          if (!org::AlignOrgTableAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
                          }
                      });
    // Tables slice 2: the editing surface around slice 1's align-and-step.
    // All the same direct "act on context.buffer, report through
    // context.message" shape as org-table-align above; each op realigns
    // the whole table as a side effect (they share its rebuild machinery,
    // see Org.cpp's RewriteOrgTable).
    registry.Register("org-table-previous-cell", "Realign the table at point and move to the previous cell.",
                      [](CommandContext& context) {
                          if (!org::MoveToPreviousOrgTableCellAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
                          }
                      });
    registry.Register("org-table-insert-row", "Insert an empty table row above the current one.",
                      [](CommandContext& context) {
                          if (!org::InsertOrgTableRowAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
                          }
                      });
    registry.Register("org-table-kill-row", "Remove the current table row.", [](CommandContext& context) {
        if (!org::KillOrgTableRowAtPoint(context.buffer) && context.message) {
            *context.message = "Not in a table.";
        }
    });
    registry.Register("org-table-insert-column", "Insert an empty table column to the right of the current one.",
                      [](CommandContext& context) {
                          if (!org::InsertOrgTableColumnAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
                          }
                      });
    registry.Register("org-table-delete-column", "Delete the current table column.", [](CommandContext& context) {
        if (!org::DeleteOrgTableColumnAtPoint(context.buffer) && context.message) {
            // Covers both failure modes -- off a table entirely, and a
            // one-column table (deleting the only column is refused, see
            // Org.h); one generic message rather than plumbing a second
            // failure channel through a bool return.
            *context.message = "Not in a table (or table has only one column).";
        }
    });
    registry.Register("org-table-move-column-left", "Swap the current table column with the one to its left.",
                      [](CommandContext& context) {
                          if (!org::MoveOrgTableColumnLeftAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table (or already the leftmost column).";
                          }
                      });
    registry.Register("org-table-move-column-right", "Swap the current table column with the one to its right.",
                      [](CommandContext& context) {
                          if (!org::MoveOrgTableColumnRightAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table (or already the rightmost column).";
                          }
                      });
    registry.Register("org-table-insert-hline", "Insert a separator hrule below the current table row.",
                      [](CommandContext& context) {
                          if (!org::InsertOrgTableHruleAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
                          }
                      });
    // Real Org's own org-metaup/org-metadown context dispatch: move the
    // table row at point, else fall back to the global M-UP/M-DOWN
    // line-move behavior (MoveLineUp/MoveLineDown, the extracted bodies of
    // move-line-up/move-line-down) -- bound over the global chords by
    // OrgMode()'s own keymap, so an org-mode buffer keeps line-moving
    // everywhere outside a table instead of losing it to a "Not in a
    // table." error. Subtree moving (real org-metaup's third context) is
    // not attempted -- same scope line the rest of this Org slice draws.
    // Gated on FindOrgTableAtPoint rather than just falling back whenever
    // the row move reports false: a false *inside* a table means "already
    // at the table's edge," and falling through to a line move there would
    // silently drag the row out of the table -- real Org refuses at the
    // edge too.
    registry.Register("org-metaup", "Move the table row at point up, or the current line otherwise.",
                      [](CommandContext& context) {
                          if (!org::FindOrgTableAtPoint(context.buffer)) {
                              MoveLineUp(context.buffer);
                          }
                          else if (!org::MoveOrgTableRowUpAtPoint(context.buffer) && context.message) {
                              *context.message = "Already the table's first row.";
                          }
                      });
    registry.Register("org-metadown", "Move the table row at point down, or the current line otherwise.",
                      [](CommandContext& context) {
                          if (!org::FindOrgTableAtPoint(context.buffer)) {
                              MoveLineDown(context.buffer);
                          }
                          else if (!org::MoveOrgTableRowDownAtPoint(context.buffer) && context.message) {
                              *context.message = "Already the table's last row.";
                          }
                      });
    // Tables follow-up: Markdown's own equivalent -- see Editor/Markdown.h.
    // Not gated on the active Mode in any way (this function, like every
    // other command here, operates on context.buffer directly); it works
    // in any buffer containing a real GFM table, not only ones opened as
    // .md files.
    // TAB-fallback-outside-table follow-up: unlike every other TAB binding
    // in this codebase (org-cycle, indent-for-tab-command), this used to
    // hard-stop on "Not in a table." outside a table -- leaving TAB
    // effectively dead in a markdown buffer whenever point isn't on a GFM
    // table. Falls through to indent-for-tab-command's own exact body
    // (TrySnippetTrigger, else a literal tab) the same way
    // markdown-metaup/markdown-metadown already fall through to a plain
    // line move outside a table.
    registry.Register(
        "markdown-table-align",
        "Realign the columns of the GFM table at point to their content width, or expand a snippet trigger / insert a "
        "tab character otherwise.",
        [](CommandContext& context) {
            if (markdown::AlignTableAtPoint(context.buffer)) {
                return;
            }
            if (TrySnippetTrigger(context)) {
                return;
            }
            context.buffer.ClearMark();
            context.buffer.InsertAtPoint("\t");
        });
    // Markdown table editing surface follow-up: the rest of GFM's own
    // table-editing ops, same "context.message on failure" shape as
    // markdown-table-align above; each op realigns the whole table as a
    // side effect (see Markdown.cpp's own RewriteTable).
    registry.Register("markdown-table-previous-cell", "Realign the table at point and move to the previous cell.",
                      [](CommandContext& context) {
                          if (!markdown::MoveToPreviousTableCellAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
                          }
                      });
    registry.Register("markdown-table-insert-row", "Insert an empty table row above the current one.",
                      [](CommandContext& context) {
                          if (!markdown::InsertTableRowAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
                          }
                      });
    registry.Register("markdown-table-kill-row", "Remove the current table row.", [](CommandContext& context) {
        if (!markdown::KillTableRowAtPoint(context.buffer) && context.message) {
            *context.message = "Not in a table, or on the header row.";
        }
    });
    registry.Register("markdown-table-insert-column", "Insert an empty table column to the right of the current one.",
                      [](CommandContext& context) {
                          if (!markdown::InsertTableColumnAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
                          }
                      });
    registry.Register("markdown-table-delete-column", "Delete the current table column.", [](CommandContext& context) {
        if (!markdown::DeleteTableColumnAtPoint(context.buffer) && context.message) {
            *context.message = "Not in a table, or it has only one column.";
        }
    });
    registry.Register("markdown-table-move-column-left", "Swap the current table column with the one to its left.",
                      [](CommandContext& context) {
                          if (!markdown::MoveTableColumnLeftAtPoint(context.buffer) && context.message) {
                              *context.message = "Already the table's first column.";
                          }
                      });
    registry.Register("markdown-table-move-column-right", "Swap the current table column with the one to its right.",
                      [](CommandContext& context) {
                          if (!markdown::MoveTableColumnRightAtPoint(context.buffer) && context.message) {
                              *context.message = "Already the table's last column.";
                          }
                      });
    // markdown-metaup/markdown-metadown: real Org's own org-metaup/
    // org-metadown context dispatch (see that pair's own comment above).
    // Same FindTableAtPoint gate for the same reason: MoveTableRowUp/
    // DownAtPoint returning false is ambiguous between "not in a table"
    // and "already at the header/data-row edge," and only the former
    // should fall through to a plain line move.
    registry.Register("markdown-metaup", "Move the table row at point up, or the current line otherwise.",
                      [](CommandContext& context) {
                          if (!markdown::FindTableAtPoint(context.buffer)) {
                              MoveLineUp(context.buffer);
                          }
                          else if (!markdown::MoveTableRowUpAtPoint(context.buffer) && context.message) {
                              *context.message = "Already the table's first row.";
                          }
                      });
    registry.Register("markdown-metadown", "Move the table row at point down, or the current line otherwise.",
                      [](CommandContext& context) {
                          if (!markdown::FindTableAtPoint(context.buffer)) {
                              MoveLineDown(context.buffer);
                          }
                          else if (!markdown::MoveTableRowDownAtPoint(context.buffer) && context.message) {
                              *context.message = "Already the table's last row.";
                          }
                      });

    // Links follow-up: a no-op-everywhere-until-acted-on signal, the same
    // "just set interactiveRequest" shape project-search-visit-result/
    // toggle-project-sidebar above already use -- see
    // BufferView::OpenLinkAtPoint for the actual detect-and-open logic
    // (Org's own [[target][description]] bracket syntax first in an
    // org-mode buffer, Editor/Link.h's generic bare-URL/file detection
    // everywhere else). Bound globally (below) so it's reachable from any
    // mode, matching the user's own "any feature available to any mode
    // should be available to all modes" principle -- Org additionally binds
    // its own real C-c C-o to this same command, see OrgMode()'s own doc
    // comment in Mode.h/.cpp.
    registry.Register("open-link-at-point", "Follow the link (Org bracket link, URL, or file path) at point.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::OpenLinkAtPoint;
                      });

    // toolchain-include-paths follow-up: M-x only, no dedicated binding --
    // same "look around/act on demand" precedent vcs-blame-buffer/
    // vcs-show-log established for an infrequent, non-interactive action.
    // Clears Editor/ToolchainIncludePaths.h's on-disk cache of compiler-
    // derived default include paths (used by open-link-at-point/LSP
    // resolution as a last-resort fallback below ProjectSettings' own
    // configured includePaths), so the next lookup re-probes the real
    // toolchain instead of reusing a result that might now be stale (e.g.
    // right after a compiler upgrade).
    registry.Register("refresh-toolchain-include-paths",
                      "Clear the cached compiler-derived default include paths, so the next lookup re-probes the "
                      "real toolchain.",
                      [](CommandContext& context) {
                          ClearToolchainIncludePathCache();
                          if (context.message) {
                              *context.message = "Toolchain include-path cache cleared.";
                          }
                      });
}

Keymap BuildDefaultGlobalKeymap() {
    Keymap keymap;

    keymap.Bind(ParseKeySequence("C-f"), "forward-char");
    keymap.Bind(ParseKeySequence("C-b"), "backward-char");
    keymap.Bind(ParseKeySequence("C-d"), "delete-char");
    keymap.Bind(ParseKeySequence("DEL"), "backward-delete-char");
    keymap.Bind(ParseKeySequence("DELETE"), "delete-char");
    keymap.Bind(ParseKeySequence("C-a"), "beginning-of-line");
    keymap.Bind(ParseKeySequence("C-e"), "end-of-line");
    keymap.Bind(ParseKeySequence("HOME"), "beginning-of-line");
    keymap.Bind(ParseKeySequence("END"), "end-of-line");
    keymap.Bind(ParseKeySequence("C-HOME"), "beginning-of-buffer");
    keymap.Bind(ParseKeySequence("C-END"), "end-of-buffer");
    keymap.Bind(ParseKeySequence("C-k"), "kill-line");
    keymap.Bind(ParseKeySequence("C-y"), "yank");
    keymap.Bind(ParseKeySequence("C-u"), "universal-argument");
    // Both C-_ and C-/, real Emacs' own pair of undo bindings -- and under
    // Notcurses both are genuinely needed, one per keyboard protocol: a
    // legacy terminal sends byte 0x1F for a physical Ctrl+/ (or Ctrl+_)
    // press, decoded as Control+'_' by KeyTranslation.cpp's DecodeBaseKey,
    // while a kitty-protocol terminal reports the same press as a real
    // Control+'/' chord (id '/', NCKEY_MOD_CTRL -- no C0-byte ambiguity in
    // that protocol at all). Binding only one of the two leaves undo
    // unreachable on the other protocol's terminals -- a real, live-tested
    // regression each direction.
    keymap.Bind(ParseKeySequence("C-_"), "undo");
    keymap.Bind(ParseKeySequence("C-/"), "undo");
    // redo has no standard Emacs binding (stock Emacs has no built-in redo
    // at all -- undo-tree/undo-fu-style packages each pick their own key).
    // M-/ works cleanly regardless of the C-_/C-/ byte ambiguity above:
    // Meta is detected via a leading ESC byte (see KeyTranslation.h's own
    // header comment), not a raw control byte, so a real Alt+/ press
    // decodes correctly either way.
    keymap.Bind(ParseKeySequence("M-/"), "redo");
    keymap.Bind(ParseKeySequence("ESC /"), "redo");

    // structural-selection-expansion follow-up: M-=/M-- are the real
    // er/expand-region Emacs package's own actual keybinding, not an
    // arbitrary choice -- also, unlike Ctrl+=/Ctrl+-, real terminals send no
    // distinguishable C0 byte at all for Ctrl+=/Ctrl+- (see
    // KeyTranslation.cpp's DecodeBaseKey), so a Control-chord binding here
    // could never actually fire from real input.
    keymap.Bind(ParseKeySequence("M-="), "expand-selection");
    keymap.Bind(ParseKeySequence("ESC ="), "expand-selection");
    keymap.Bind(ParseKeySequence("M--"), "shrink-selection");
    keymap.Bind(ParseKeySequence("ESC -"), "shrink-selection");
    keymap.Bind(ParseKeySequence("C-SPC"), "set-mark-command");
    keymap.Bind(ParseKeySequence("C-g"), "keyboard-quit");
    keymap.Bind(ParseKeySequence("C-w"), "kill-region");
    // Emacs-coverage follow-up: kill/word/whitespace/case vocabulary, each
    // on its real Emacs default; every M- binding gets the usual ESC twin.
    keymap.Bind(ParseKeySequence("M-y"), "yank-pop");
    keymap.Bind(ParseKeySequence("ESC y"), "yank-pop");
    keymap.Bind(ParseKeySequence("M-d"), "kill-word");
    keymap.Bind(ParseKeySequence("ESC d"), "kill-word");
    keymap.Bind(ParseKeySequence("M-DEL"), "backward-kill-word");
    keymap.Bind(ParseKeySequence("ESC DEL"), "backward-kill-word");
    keymap.Bind(ParseKeySequence("C-x h"), "mark-whole-buffer");
    keymap.Bind(ParseKeySequence("C-t"), "transpose-chars");
    keymap.Bind(ParseKeySequence("M-t"), "transpose-words");
    keymap.Bind(ParseKeySequence("ESC t"), "transpose-words");
    keymap.Bind(ParseKeySequence("M-u"), "upcase-word");
    keymap.Bind(ParseKeySequence("ESC u"), "upcase-word");
    keymap.Bind(ParseKeySequence("M-l"), "downcase-word");
    keymap.Bind(ParseKeySequence("ESC l"), "downcase-word");
    keymap.Bind(ParseKeySequence("M-c"), "capitalize-word");
    keymap.Bind(ParseKeySequence("ESC c"), "capitalize-word");
    keymap.Bind(ParseKeySequence("C-o"), "open-line");
    keymap.Bind(ParseKeySequence("C-x C-o"), "delete-blank-lines");
    keymap.Bind(ParseKeySequence("M-SPC"), "just-one-space");
    keymap.Bind(ParseKeySequence("ESC SPC"), "just-one-space");
    keymap.Bind(ParseKeySequence("M-^"), "delete-indentation");
    keymap.Bind(ParseKeySequence("ESC ^"), "delete-indentation");
    keymap.Bind(ParseKeySequence("M-m"), "back-to-indentation");
    keymap.Bind(ParseKeySequence("ESC m"), "back-to-indentation");
    keymap.Bind(ParseKeySequence("C-x u"), "undo"); // real Emacs' other undo binding, alongside C-_
    keymap.Bind(ParseKeySequence("C-x s"), "save-some-buffers");
    keymap.Bind(ParseKeySequence("C-l"), "recenter");
    keymap.Bind(ParseKeySequence("M-g g"), "goto-line");
    keymap.Bind(ParseKeySequence("M-g M-g"), "goto-line");
    keymap.Bind(ParseKeySequence("ESC g g"), "goto-line");
    // next-error/previous-error: real Emacs' own default bindings under the
    // same M-g prefix goto-line uses, with the ESC two-key fallback M-g g's
    // own binding above establishes.
    keymap.Bind(ParseKeySequence("M-g n"), "next-error");
    keymap.Bind(ParseKeySequence("ESC g n"), "next-error");
    keymap.Bind(ParseKeySequence("M-g p"), "previous-error");
    keymap.Bind(ParseKeySequence("ESC g p"), "previous-error");
    // Same "bind both real input shapes" reasoning as M-x below -- a fast
    // Alt+w press arrives as one Meta-chord, a genuinely separate Escape-
    // then-w press arrives as two.
    keymap.Bind(ParseKeySequence("M-w"), "kill-ring-save");
    keymap.Bind(ParseKeySequence("ESC w"), "kill-ring-save");
    keymap.Bind(ParseKeySequence("RET"), "newline");
    keymap.Bind(ParseKeySequence("TAB"), "indent-for-tab-command");
    // mode-agnostic-rigid-indent follow-up: safe to bind globally --
    // Markdown/Org's own table-cell S-TAB bindings and the snippet
    // session's own S-TAB handling both take priority over this (mode
    // keymaps win via KeymapStack's priority order; snippet mode
    // intercepts S-TAB before dispatch entirely, see BufferView.h).
    keymap.Bind(ParseKeySequence("S-TAB"), "unindent");
    keymap.Bind(ParseKeySequence("LEFT"), "backward-char");
    keymap.Bind(ParseKeySequence("RIGHT"), "forward-char");
    // KeyTranslation.cpp already decodes Control+Arrow (ArrowLeftCtrl/
    // ArrowRightCtrl) into Control+Special::Left/Right chords -- ParseKeyChord
    // resolves "C-LEFT"/"C-RIGHT" the same way (the C- prefix strips off
    // first, then LEFT/RIGHT resolve via NamedKeys()), so this is just
    // wiring an already-decodable chord to a command, not new decoding work.
    keymap.Bind(ParseKeySequence("C-LEFT"), "backward-word");
    keymap.Bind(ParseKeySequence("C-RIGHT"), "forward-word");
    // KeyTranslation.cpp decodes Shift+Arrow off ncinput::modifiers the
    // same uniform way it decodes every other modifier; ParseKeyChord
    // resolves "S-LEFT" etc the same way it already resolves "C-LEFT".
    keymap.Bind(ParseKeySequence("S-LEFT"), "shift-select-backward-char");
    keymap.Bind(ParseKeySequence("S-RIGHT"), "shift-select-forward-char");
    keymap.Bind(ParseKeySequence("S-UP"), "shift-select-previous-line");
    keymap.Bind(ParseKeySequence("S-DOWN"), "shift-select-next-line");
    keymap.Bind(ParseKeySequence("C-n"), "next-line");
    keymap.Bind(ParseKeySequence("C-p"), "previous-line");
    keymap.Bind(ParseKeySequence("DOWN"), "next-line");
    keymap.Bind(ParseKeySequence("UP"), "previous-line");
    keymap.Bind(ParseKeySequence("PAGEDOWN"), "scroll-page-down");
    keymap.Bind(ParseKeySequence("PAGEUP"), "scroll-page-up");
    keymap.Bind(ParseKeySequence("C-v"), "scroll-page-down");
    keymap.Bind(ParseKeySequence("M-v"), "scroll-page-up");
    keymap.Bind(ParseKeySequence("ESC v"), "scroll-page-up");
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    keymap.Bind(ParseKeySequence("C-x C-c"), "quit");
    keymap.Bind(ParseKeySequence("C-z"), "suspend-frame");
    keymap.Bind(ParseKeySequence("C-x C-x"), "exchange-point-and-mark");
    keymap.Bind(ParseKeySequence("M-<"), "beginning-of-buffer");
    keymap.Bind(ParseKeySequence("ESC <"), "beginning-of-buffer");
    keymap.Bind(ParseKeySequence("M->"), "end-of-buffer");
    keymap.Bind(ParseKeySequence("ESC >"), "end-of-buffer");
    keymap.Bind(ParseKeySequence("C-s"), "isearch-forward");
    keymap.Bind(ParseKeySequence("C-r"), "isearch-backward");
    // Emacs binds these to M-%/M-f/M-b. Both real Meta chords and the
    // ESC-prefix fallback are bound (same "cover both real input shapes"
    // reasoning as M-x/M-w/M-/ elsewhere in this function).
    keymap.Bind(ParseKeySequence("M-%"), "query-replace-regexp");
    keymap.Bind(ParseKeySequence("ESC %"), "query-replace-regexp");
    keymap.Bind(ParseKeySequence("C-x C-f"), "find-file");
    keymap.Bind(ParseKeySequence("C-x b"), "switch-to-buffer");
    keymap.Bind(ParseKeySequence("C-x C-b"), "list-buffers");
    keymap.Bind(ParseKeySequence("C-c C-s"), "project-search");
    keymap.Bind(ParseKeySequence("C-c C-f"), "project-find-file");
    keymap.Bind(ParseKeySequence("C-c C-e"), "lsp-show-diagnostic");
    keymap.Bind(ParseKeySequence("C-c C-j"), "lsp-hover");
    keymap.Bind(ParseKeySequence("C-c C-a"), "lsp-code-action");
    keymap.Bind(ParseKeySequence("C-c C-q"), "lsp-quick-fix");
    // hover/completion follow-up: "M-/" (company-mode's usual manual-
    // completion binding) is already bound to redo elsewhere in this
    // function -- C-M-i is Emacs' own traditional complete-symbol binding
    // instead, confirmed free (grepped the full bind list in this function).
    keymap.Bind(ParseKeySequence("C-M-i"), "lsp-complete");
    keymap.Bind(ParseKeySequence("M-."), "lsp-goto-definition"); // real Emacs' own xref-find-definitions binding
    keymap.Bind(ParseKeySequence("ESC ."), "lsp-goto-definition");
    keymap.Bind(ParseKeySequence("M-?"), "project-find-references"); // real Emacs' own xref-find-references binding
    keymap.Bind(ParseKeySequence("ESC ?"), "project-find-references");
    // declaration/typeDefinition/implementation-keybindings follow-up: none
    // of the three has a standard real-Emacs binding to align with (they're
    // eglot/lsp-mode extensions, not core xref commands) -- "C-c l" as a new
    // mnemonic prefix ("l" for LSP), matching this codebase's own existing
    // "C-c v"/"C-c T"/"C-c A" per-subsystem-prefix convention. Confirmed free
    // (grepped the full bind list in this function): no existing "C-c l"
    // binding at all, only the unrelated "C-c C-l" chord (open-link-at-point).
    keymap.Bind(ParseKeySequence("C-c l d"), "lsp-goto-declaration");
    keymap.Bind(ParseKeySequence("C-c l t"), "lsp-goto-type-definition");
    keymap.Bind(ParseKeySequence("C-c l i"), "lsp-goto-implementation");
    // symbol-search follow-up: "M-g i" is real Emacs' own default binding
    // for `imenu` (goto-map, Emacs 28+) -- lsp-goto-symbol is this
    // codebase's in-buffer symbol picker, the same role. lsp-workspace-symbol
    // has no vanilla-Emacs default to align with (imenu has no project-wide
    // counterpart built in) -- "C-c l w" continues the "C-c l" mnemonic
    // prefix just established above ("w" for workspace).
    keymap.Bind(ParseKeySequence("M-g i"), "lsp-goto-symbol");
    keymap.Bind(ParseKeySequence("C-c l w"), "lsp-workspace-symbol");
    // call/type-hierarchy follow-up: continues the "C-c l" mnemonic prefix
    // -- no standard real-Emacs/eglot default to align with (call/type
    // hierarchy predates neither xref nor imenu). "c"/shifted-"C" for
    // call-hierarchy incoming/outgoing (callers vs. callees), "s"/shifted-"S"
    // for type-hierarchy supertypes/subtypes -- the same "shifted variant is
    // the paired/stronger action" trick "C-c v h"/"C-c v H" already uses.
    keymap.Bind(ParseKeySequence("C-c l c"), "lsp-call-hierarchy-incoming");
    keymap.Bind(ParseKeySequence("C-c l C"), "lsp-call-hierarchy-outgoing");
    keymap.Bind(ParseKeySequence("C-c l s"), "lsp-type-hierarchy-supertypes");
    keymap.Bind(ParseKeySequence("C-c l S"), "lsp-type-hierarchy-subtypes");
    // prepareRename/linkedEditingRange follow-up: continues the "C-c l"
    // mnemonic prefix -- "r" for "related"/"linked range," distinct from
    // lsp-rename's own unprefixed "C-c C-M-r" binding below (a different
    // command: this one mirrors edits live, it never renames across files).
    keymap.Bind(ParseKeySequence("C-c l r"), "lsp-linked-editing-range");
    // header-source-switching follow-up: "M-o" is free (grepped the full
    // bind list in this function) and matches the VS Code/CLion C/C++
    // extensions' own Alt+O convention for this exact action -- no
    // equivalent real-Emacs binding to align with instead (ff-find-other-
    // file has no standard default keybinding of its own).
    keymap.Bind(ParseKeySequence("M-o"), "switch-header-source");
    keymap.Bind(ParseKeySequence("C-c C-M-r"), "lsp-rename"); // C-c C-r is already project-replace
    keymap.Bind(ParseKeySequence("C-c C-b"), "run-task");
    // task-runner follow-up: same "shift/meta variant is the stronger
    // version of the same action" slot lsp-rename's own C-c C-M-r binding
    // establishes -- cancelling is rare, but not rare enough to leave
    // M-x-only once it's actually needed (unlike lsp-show-log's own
    // "no binding yet" precedent, this one's a "stop something running now"
    // action worth a direct key).
    keymap.Bind(ParseKeySequence("C-c C-M-b"), "cancel-task");
    // DAP client slices 1/2: the VS/JetBrains-standard debug F-keys (the
    // user's explicit ask -- see ROADMAP.md's DAP entry). dap-show-debug/
    // dap-expand-variable/dap-evaluate stay M-x-only -- no established
    // cross-editor F-key exists for them, and the F-key promise was
    // specifically about run control.
    keymap.Bind(ParseKeySequence("F5"), "dap-continue");
    keymap.Bind(ParseKeySequence("S-F5"), "dap-stop");
    keymap.Bind(ParseKeySequence("F9"), "dap-toggle-breakpoint");
    keymap.Bind(ParseKeySequence("F10"), "dap-step-over");
    keymap.Bind(ParseKeySequence("F11"), "dap-step-into");
    keymap.Bind(ParseKeySequence("S-F11"), "dap-step-out");
    // ACP client slice 2 (keymap-collision follow-up): "C-c A" prefix
    // (shifted "a" for agent), not plain "C-c a" -- that's already
    // org-agenda's own leaf binding (real Org's actual binding), and
    // Keymap::Resolve fires the instant a node's own command is set without
    // ever consulting its children, so "C-c a <anything>" is structurally
    // unreachable once "C-c a" itself is bound (confirmed live via tmux;
    // see Keymap::AmbiguousBindings and CommandsTest.cpp's regression test).
    // Uppercase A is just a distinct codepoint chord at the terminal level,
    // the same trick "C-c v H" already uses for vcs-unstage-hunk beside its
    // lowercase twin.
    keymap.Bind(ParseKeySequence("C-c A s"), "acp-start-session");
    keymap.Bind(ParseKeySequence("C-c A p"), "acp-send-prompt");
    keymap.Bind(ParseKeySequence("C-c A k"), "acp-stop-session"); // "k" for kill, matching Emacs' own kill-process vocabulary
    keymap.Bind(ParseKeySequence("C-c A r"), "acp-rewind");       // "r" for rewind
    keymap.Bind(ParseKeySequence("C-c c"), "acp-toggle-panel");   // "c" for chat
    // test-runner integration: "C-c T" prefix (shifted "t" for tests --
    // plain "C-c t" is toggle-terminal's own leaf binding below, so a
    // "C-c t <x>" prefix is structurally unreachable, the exact
    // Keymap::Resolve trap "C-c A" documents above). Every binding under it
    // is a 3-key chord with no command on the prefix node itself, keeping
    // Keymap::AmbiguousBindings (and CommandsTest.cpp's regression test)
    // clean.
    keymap.Bind(ParseKeySequence("C-c T t"), "run-tests");
    keymap.Bind(ParseKeySequence("C-c T k"), "cancel-tests"); // "k" for kill, the C-c A k convention
    keymap.Bind(ParseKeySequence("C-c T r"), "show-test-results");
    keymap.Bind(ParseKeySequence("C-c T ."), "run-test-at-point"); // "." for at-point
    keymap.Bind(ParseKeySequence("C-c T f"), "rerun-failed-tests");
    keymap.Bind(ParseKeySequence("C-c C-v"), "project-search-visit-result");
    // Editable-multibuffer follow-up: wgrep's own real Emacs binding is
    // "C-c C-e", already taken here by lsp-show-diagnostic -- "C-c C-c" is
    // the closest fidelity available (also a real wgrep convention in some
    // configs), and is otherwise unbound globally (only a *local* leaf
    // inside vcs-commit-message-mode's own keymap, see vcs-commit-finish
    // below -- no collision, KeymapStack layers are independent).
    keymap.Bind(ParseKeySequence("C-c C-c"), "multibuffer-commit-changes");
    // VCS blame gutter follow-up: "C-c v" prefix, mirroring "C-c C-b"/
    // "C-c C-M-b" run-task/cancel-task's own choice of an otherwise-unused
    // letter+prefix combination.
    keymap.Bind(ParseKeySequence("C-c v b"), "vcs-show-blame");
    keymap.Bind(ParseKeySequence("C-c v i"), "vcs-blame-detail-at-point"); // "i" for info, next to "b"
    keymap.Bind(ParseKeySequence("C-c v l"), "vcs-show-log");
    keymap.Bind(ParseKeySequence("C-c v v"), "vcs-visit-result");
    // Vocabulary-completion follow-up: "a" for add (git's own verb for
    // staging, and "s" is taken by status), "w" for sWitch ("s" again),
    // "n" for new branch. vcs-branches stays M-x only -- see its
    // registration comment.
    keymap.Bind(ParseKeySequence("C-c v s"), "vcs-status");
    keymap.Bind(ParseKeySequence("C-c v a"), "vcs-stage-file");
    keymap.Bind(ParseKeySequence("C-c v u"), "vcs-unstage-file");
    keymap.Bind(ParseKeySequence("C-c v c"), "vcs-commit");
    keymap.Bind(ParseKeySequence("C-c v w"), "vcs-switch-branch");
    keymap.Bind(ParseKeySequence("C-c v n"), "vcs-create-branch");
    // Hunk-staging follow-up: "h" for hunk, its shifted twin for the
    // reverse -- an uppercase letter is just a distinct codepoint chord at
    // the terminal level, nothing special-cased.
    // Multibuffers follow-up: "d" for diff, next to the other "C-c v"
    // one-shot views (b/l above).
    keymap.Bind(ParseKeySequence("C-c v d"), "vcs-full-diff-buffer");
    keymap.Bind(ParseKeySequence("C-c v h"), "vcs-stage-hunk");
    keymap.Bind(ParseKeySequence("C-c v H"), "vcs-unstage-hunk");
    // Hunk-navigation follow-up: "n"/"p" (next/previous) are already taken
    // by vcs-create-branch/focus-vcs-panel, so this uses the same shifted-
    // twin trick "h"/"H" above already establishes -- a distinct codepoint
    // chord, nothing special-cased.
    keymap.Bind(ParseKeySequence("C-c v N"), "vcs-next-hunk");
    keymap.Bind(ParseKeySequence("C-c v P"), "vcs-previous-hunk");
    // VCS side panel: "C-c C-v" is already project-search-visit-result, so
    // this uses the shifted "C-c V" trick "C-c A"/"C-c T" already use beside
    // their own lowercase twins -- a distinct codepoint chord, nothing
    // special-cased. "p" for panel is unbound under the "C-c v" prefix
    // (every other leaf letter above is already taken), mirroring
    // "C-c C-p"/"C-c p" toggle-project-sidebar/focus-project-sidebar's own
    // pair.
    keymap.Bind(ParseKeySequence("C-c V"), "toggle-vcs-panel");
    keymap.Bind(ParseKeySequence("C-c v p"), "focus-vcs-panel");
    keymap.Bind(ParseKeySequence("C-c C-r"), "project-replace");
    keymap.Bind(ParseKeySequence("C-c C-p"), "toggle-project-sidebar");
    // sidebar-keyboard-focus follow-up: the non-control second key beside
    // the toggle's own C-c C-p, same pairing pattern the "C-c v" VCS
    // family established for an otherwise-unused plain-letter slot.
    keymap.Bind(ParseKeySequence("C-c p"), "focus-project-sidebar");
    // named-projects follow-up: "C-c P" prefix (shifted "p", ACP/Tests' own
    // "C-c A"/"C-c T" uppercase-prefix trick) -- plain "C-c p" is already
    // focus-project-sidebar's own leaf binding, so "C-c p <anything>" is
    // structurally unreachable the same way "C-c a" blocks "C-c a <x>" (see
    // the ACP prefix's own comment above); uppercase P is a distinct
    // codepoint chord at the terminal level, confirmed free.
    keymap.Bind(ParseKeySequence("C-c P s"), "switch-project");
    keymap.Bind(ParseKeySequence("C-c P o"), "open-project");
    keymap.Bind(ParseKeySequence("C-c m"), "toggle-minimap");
    // Terminal-panel follow-up: C-` is the reserved primary (the VS Code
    // convention; only ever deliverable under the kitty keyboard protocol
    // -- legacy encodings send NUL, which the Notcurses NUL patch maps to
    // C-Space, deliberately left to the shell/set-mark), with "C-c t" as
    // the portable fallback every terminal can produce. While the panel
    // itself is focused only C-` toggles -- C-c must reach the shell as a
    // plain byte, so a "C-c t" prefix chord can't exist there (see
    // TerminalPanel.h's header comment).
    keymap.Bind(ParseKeySequence("C-`"), "toggle-terminal");
    keymap.Bind(ParseKeySequence("C-c t"), "toggle-terminal");
    // editor-ergonomics follow-up: no vanilla-Emacs default binding for
    // recentf-open-files exists to match (menu-only upstream), so this
    // picks a free "C-c f" prefix ("f" for files) rather than squatting an
    // established chord.
    keymap.Bind(ParseKeySequence("C-c f r"), "find-recent-file");
    keymap.Bind(ParseKeySequence("C-x k"), "kill-buffer");
    keymap.Bind(ParseKeySequence("C-c a"), "org-agenda"); // real Org's own actual binding
    // capture-templates follow-up: real Org's own org-capture is "C-c c",
    // already acp-toggle-panel here -- "C-c k" is the common fallback binding
    // Emacs users reach for when "c" is taken, and is otherwise unbound.
    keymap.Bind(ParseKeySequence("C-c k"), "org-capture");
    keymap.Bind(ParseKeySequence("C-c C-d"), "create-directory");
    keymap.Bind(ParseKeySequence("C-c C-k"), "delete-file");
    // Not "C-c C-m": Ctrl+M and Enter are the same byte at the terminal
    // level (see KeyTranslation.cpp's Enter case), so a "C-m" chord can
    // never actually be produced by real input -- TranslateKey always
    // reports SpecialKey::Enter instead, and that binding would be dead.
    keymap.Bind(ParseKeySequence("C-c C-n"), "rename-file");
    keymap.Bind(ParseKeySequence("C-c C-o"), "find-scratch");
    // Tab-reorder follow-up: "<" and ">" read as "shove the tab that way",
    // and both C-c chords were free (M-</M-> stay
    // beginning/end-of-buffer, untouched).
    keymap.Bind(ParseKeySequence("C-c <"), "tab-move-left");
    keymap.Bind(ParseKeySequence("C-c >"), "tab-move-right");
    // Tab-cycling follow-up: the unshifted keys under the same < and > --
    // tap C-c ,/. to walk the tabs, hold Shift on the same chord to drag
    // the tab along instead. C-x LEFT/RIGHT ride along on Emacs' own
    // previous-buffer/next-buffer spots (both were free).
    keymap.Bind(ParseKeySequence("C-c ,"), "tab-previous");
    keymap.Bind(ParseKeySequence("C-c ."), "tab-next");
    keymap.Bind(ParseKeySequence("C-x LEFT"), "tab-previous");
    keymap.Bind(ParseKeySequence("C-x RIGHT"), "tab-next");
    // Links follow-up: free everywhere else in this keymap (confirmed by
    // reading the full bind list above) -- see open-link-at-point's own
    // registration comment above and OrgMode()'s doc comment for why Org
    // additionally binds its own real C-c C-o to the same command.
    keymap.Bind(ParseKeySequence("C-c C-l"), "open-link-at-point");
    // move-line-up/move-line-down/duplicate-line follow-up: M-UP/M-DOWN
    // aren't a real Emacs binding for anything already, and the usual
    // "cover both real input shapes" dual binding applies the same as
    // every other Meta chord above. duplicate-line has no standard
    // cross-editor chord to align with (Ctrl+D is already delete-char
    // here, and Ctrl+Shift+<letter> isn't reliably decodable from a real
    // terminal at all -- same class of gap as Ctrl+Shift+Z for redo would
    // hit) -- C-c d picked as a free, mnemonic chord under this codebase's
    // own C-c-prefix convention instead.
    keymap.Bind(ParseKeySequence("M-UP"), "move-line-up");
    keymap.Bind(ParseKeySequence("ESC UP"), "move-line-up");
    keymap.Bind(ParseKeySequence("M-DOWN"), "move-line-down");
    keymap.Bind(ParseKeySequence("ESC DOWN"), "move-line-down");
    // C-UP/C-DOWN were free (nothing ever bound them) and terminal-reliable,
    // unlike the cross-editor Ctrl+Alt+Arrow
    // or Ctrl+D conventions (C-d is delete-char here, per the keybinding
    // audit's own note that no standard chord was free). M-n mirrors
    // Emacs' multiple-cursors ecosystem living on M-prefixed keys; its
    // C-> convention itself isn't reliably decodable from a terminal.
    keymap.Bind(ParseKeySequence("C-DOWN"), "add-cursor-below");
    keymap.Bind(ParseKeySequence("C-UP"), "add-cursor-above");
    // Same Ctrl+Arrow chords with Shift added -- the natural "undo that add"
    // gesture, bound to both directions since remove-last-cursor is
    // direction-agnostic (it always removes whichever cursor was added
    // most recently, regardless of which arrow key added it).
    keymap.Bind(ParseKeySequence("C-S-DOWN"), "remove-last-cursor");
    keymap.Bind(ParseKeySequence("C-S-UP"), "remove-last-cursor");
    keymap.Bind(ParseKeySequence("M-n"), "select-next-occurrence");
    keymap.Bind(ParseKeySequence("ESC n"), "select-next-occurrence");
    keymap.Bind(ParseKeySequence("C-c d"), "duplicate-line");
    // DAP round 2: "C-c D" (shifted "d", distinct chord from its lowercase
    // twin "C-c d" above) -- the same trick "C-c A"/"C-c T" already use
    // beside their own lowercase twins.
    keymap.Bind(ParseKeySequence("C-c D"), "dap-toggle-console");
    // toggle-line-comment follow-up: real Emacs' own actual binding for
    // comment-dwim/comment-line is M-;, not C-/ (which is a non-Emacs
    // convention this codebase never adopted anyway) -- chosen over C-/
    // for a second reason too: C-/ has the exact same real-terminal-
    // unreachability problem the pre-fix undo binding had (no terminal
    // byte distinguishes Ctrl+/ from Ctrl+_, and only the latter is
    // decoded -- see KeyTranslation.cpp's DecodeBaseKey), while M-;
    // decodes reliably via the ESC-prefix Meta path like every other Meta
    // chord here.
    keymap.Bind(ParseKeySequence("M-;"), "toggle-line-comment");
    keymap.Bind(ParseKeySequence("ESC ;"), "toggle-line-comment");
    keymap.Bind(ParseKeySequence("C-x 2"), "split-window-below");
    keymap.Bind(ParseKeySequence("C-x 3"), "split-window-right");
    keymap.Bind(ParseKeySequence("C-x 0"), "delete-window");
    keymap.Bind(ParseKeySequence("C-x 1"), "delete-other-windows");
    keymap.Bind(ParseKeySequence("C-x o"), "other-window");
    keymap.Bind(ParseKeySequence("C-x ^"), "enlarge-window");
    keymap.Bind(ParseKeySequence("C-x }"), "enlarge-window-horizontally");
    keymap.Bind(ParseKeySequence("C-x {"), "shrink-window-horizontally");
    keymap.Bind(ParseKeySequence("M-f"), "forward-word");
    keymap.Bind(ParseKeySequence("ESC f"), "forward-word");
    keymap.Bind(ParseKeySequence("M-b"), "backward-word");
    keymap.Bind(ParseKeySequence("ESC b"), "backward-word");
    // Emacs-keymap-round-2 follow-up: sentence/sexp motion and zap-to-char,
    // each on its real Emacs default.
    keymap.Bind(ParseKeySequence("M-e"), "forward-sentence");
    keymap.Bind(ParseKeySequence("ESC e"), "forward-sentence");
    keymap.Bind(ParseKeySequence("M-a"), "backward-sentence");
    keymap.Bind(ParseKeySequence("ESC a"), "backward-sentence");
    keymap.Bind(ParseKeySequence("C-M-f"), "forward-sexp");
    keymap.Bind(ParseKeySequence("C-M-b"), "backward-sexp");
    keymap.Bind(ParseKeySequence("M-z"), "zap-to-char");
    keymap.Bind(ParseKeySequence("ESC z"), "zap-to-char");
    // fill-paragraph follow-up: real Emacs' own default binding.
    keymap.Bind(ParseKeySequence("M-q"), "fill-paragraph");
    keymap.Bind(ParseKeySequence("ESC q"), "fill-paragraph");
    // Unlike the ESC-only bindings above, M-x is bound both ways
    // deliberately: a real fast Alt+x press arrives as a single Meta-chord
    // ("M-x"), while a genuinely separate Escape-then-x press arrives as two
    // chords ("ESC x") -- Keymap/KeymapStack::Resolve do pure exact-chord
    // matching with no Escape<->Meta equivalence, so both bindings are
    // needed for both real input shapes to work.
    keymap.Bind(ParseKeySequence("M-x"), "execute-extended-command");
    keymap.Bind(ParseKeySequence("ESC x"), "execute-extended-command");
    // Real Emacs binds C-x (/C-x )/C-x e -- unreachable here on purpose:
    // this codebase's terminal-input decoding (KeyTranslation.cpp's
    // DecodeBaseKey) only ever produces Control=true for C0 control bytes
    // 1-26 (Control+<a-z>), and real terminals don't send a distinguishable
    // byte for Ctrl+parenthesis at all -- the same class of gap already
    // documented at rename-file's own C-c C-n binding (not C-c C-m, same
    // byte as Enter). F3/F4 are modern Emacs' own real alternate bindings
    // (kmacro-start-macro / kmacro-end-or-call-macro) and map cleanly here.
    keymap.Bind(ParseKeySequence("F3"), "kmacro-start-macro");
    keymap.Bind(ParseKeySequence("F4"), "kmacro-end-or-call-macro");
    // jump-back-stack follow-up: real Emacs' own pop-global-mark binding --
    // the closest match, since this is a cross-buffer jump stack, not the
    // local-buffer mark-ring plain pop-mark/C-u C-SPC covers.
    keymap.Bind(ParseKeySequence("C-x C-SPC"), "jump-back");
    // jump-back-stack follow-up, forward direction: no stock Emacs binding
    // pairs with pop-global-mark (same gap noted at redo/M-/ above). Modern
    // Emacs' own xref-go-back/xref-go-forward convention (M-,/C-M-,) was
    // the first instinct, but C-M-, needs Ctrl+comma specifically, and
    // comma has no C0 control-byte mapping at all -- the same
    // real-terminal unreachability class already documented at C-x (/C-x )
    // above, just for a different punctuation. Co-locating under the same
    // C-x prefix as jump-back instead, on a plain Ctrl+letter suffix chord.
    // C-x C-j (the obvious mnemonic) turned out to be a second unreachable
    // chord, this time in this codebase's own vendored Notcurses, not a
    // real terminal: found live, not assumed -- in.c's final-normalization
    // pass (`/* ned patch: ... */`) collapses raw '\n' (Ctrl+J's own C0
    // byte) into the exact same NCKEY_ENTER as a real Enter/Return press,
    // same file, same block that already collapses '\r'. C-x C-n instead;
    // 'n' isn't one of that block's collapsed bytes (0/DEL/BS/LF/CR/TAB).
    keymap.Bind(ParseKeySequence("C-x C-n"), "jump-forward");
    keymap.Bind(ParseKeySequence("C-x r SPC"), "point-to-register");
    keymap.Bind(ParseKeySequence("C-x r j"), "jump-to-register");
    keymap.Bind(ParseKeySequence("C-x r s"), "copy-to-register");
    keymap.Bind(ParseKeySequence("C-x r i"), "insert-register");
    keymap.Bind(ParseKeySequence("C-x r k"), "kill-rectangle");
    keymap.Bind(ParseKeySequence("C-x r d"), "delete-rectangle");
    keymap.Bind(ParseKeySequence("C-x r y"), "yank-rectangle");
    keymap.Bind(ParseKeySequence("C-x r t"), "string-rectangle");
    // editor-ergonomics follow-up: real Emacs' own bookmark.el bindings --
    // "m" for mark/set, "b" for bookmark-jump, both otherwise-free letters
    // in this C-x r prefix (see the register/rectangle bindings just
    // above). bookmark-delete is M-x-only, recover-file's own "rare,
    // deliberate act" precedent.
    keymap.Bind(ParseKeySequence("C-x r m"), "bookmark-set");
    keymap.Bind(ParseKeySequence("C-x r b"), "bookmark-jump");
    keymap.Bind(ParseKeySequence("C-x n n"), "narrow-to-region");
    keymap.Bind(ParseKeySequence("C-x n w"), "widen");

    // Every printable ASCII character self-inserts by default, same as Emacs'
    // global map: this isn't a fallback, each one is a real keymap entry.
    for (char32_t codepoint = 0x20; codepoint <= 0x7E; ++codepoint) {
        KeyChord chord;
        chord.Codepoint = codepoint;
        keymap.Bind({chord}, "self-insert-command");
    }

    return keymap;
}

} // namespace ned::editor
