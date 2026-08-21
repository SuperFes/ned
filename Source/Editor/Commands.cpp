#include "Commands.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

#include "Backup.h"
#include "CodeFold.h"
#include "FinalNewline.h"
#include "FormatOnSave.h"
#include "InlineDiagnostics.h"
#include "Lsp/LspManager.h"
#include "Markdown.h"
#include "Org.h"
#include "ProjectRoot.h"
#include "ProjectSession.h"
#include "TabWidth.h"
#include "Text/Grapheme.h"
#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    std::size_t LineContentEnd(const text::Rope& content, std::size_t point) {
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

    LineSpan GetLineSpan(const text::Rope& content, std::size_t line) {
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

    // Fraction of the viewport height a page up/down moves -- a fixed constant
    // for now rather than user-configurable (same "hardcoded C++ for now"
    // scope call as Theme selection in Phase 6; see ROADMAP.md). Emacs' own
    // default (`next-screen-context-lines` = 2 lines of overlap) works out to
    // roughly this same ballpark for a typical terminal height.
    constexpr double kPageScrollFraction = 0.65;

    std::size_t PageLineCount(std::size_t viewportHeight) {
        return std::max<std::size_t>(1, static_cast<std::size_t>(static_cast<double>(viewportHeight) * kPageScrollFraction));
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
    // decision (kill-ring commands, rectangles, registers, narrowing all
    // deliberately stay primary-only in v1 -- see ROADMAP.md), not a
    // default to inherit silently.
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

    // Multi-cursor phase: [start, end) of the word point sits inside or
    // immediately after, or nullopt when point touches no word at all.
    // Same ASCII-word classification Buffer's own word motion uses
    // (deliberately not Unicode-aware, matching that documented cut) --
    // duplicated here rather than exposed from Buffer.cpp, the usual
    // "not worth a new seam for something this small" call.
    std::optional<std::pair<std::size_t, std::size_t>> WordRegionAt(const text::Rope& rope, std::size_t point) {
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
        context.buffer.ClearMark();
        const std::size_t point   = context.buffer.Point();
        const auto&       content = context.buffer.Content();
        const std::size_t lineEnd = LineContentEnd(content, point);

        if (point < lineEnd) {
            context.killRing.Kill(context.buffer.DeleteRange(point, lineEnd - point));
        }
        else if (lineEnd < content.ByteLength()) {
            context.killRing.Kill(context.buffer.DeleteRange(point, 1));
        }
    });

    registry.Register("yank", "Insert the most recent kill-ring entry at point.", [](CommandContext& context) {
        context.buffer.ClearMark();
        if (!context.killRing.Empty()) {
            context.buffer.InsertAtPoint(context.killRing.Current());
        }
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
        if (!context.buffer.HasMark()) {
            return;
        }
        const auto [start, end] = context.buffer.Region();
        context.killRing.Kill(context.buffer.DeleteRange(start, end - start));
        context.buffer.ClearMark();
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
    });

    registry.Register("kill-word", "Kill from point to the end of the next word.", [](CommandContext& context) {
        context.buffer.ClearMark();
        const std::size_t start = context.buffer.Point();
        context.buffer.MoveForwardWord();
        const std::size_t end = context.buffer.Point();
        if (end > start) {
            context.killRing.Kill(context.buffer.DeleteRange(start, end - start));
        }
    });

    registry.Register("backward-kill-word", "Kill from the start of the previous word to point.", [](CommandContext& context) {
        context.buffer.ClearMark();
        const std::size_t end = context.buffer.Point();
        context.buffer.MoveBackwardWord();
        const std::size_t start = context.buffer.Point();
        if (end > start) {
            context.killRing.Kill(context.buffer.DeleteRange(start, end - start));
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
                          buffer.SetPoint(content.LineToByteOffset(first));
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
                // Same backup/autosave pair as save-buffer's own body.
                BackupFileBeforeSave(*buffer->Path());
                buffer->Save();
                RemoveAutoSave(*buffer->Path());
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
                          const text::Rope& content = buffer.Content();
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
                          buffer.AddCursorAt(buffer.ByteOffsetForLineAndColumn(line + 1, column, TabWidth()));
                      });

    registry.Register("add-cursor-above", "Add a cursor one line above the top-most cursor.",
                      [](CommandContext& context) {
                          text::Buffer&     buffer  = context.buffer;
                          const text::Rope& content = buffer.Content();
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
                          buffer.AddCursorAt(buffer.ByteOffsetForLineAndColumn(line - 1, column, TabWidth()));
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
        if (!context.buffer.HasMark()) {
            return;
        }
        const auto [start, end] = context.buffer.Region();
        context.killRing.Kill(context.buffer.Content().Substring(start, end - start));
        context.buffer.ClearMark();
    });

    registry.Register("undo", "Undo the last change.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.Undo();
    });

    registry.Register("redo", "Redo the last undone change.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.Redo();
    });

    registry.Register("newline", "Insert a newline at point.", PerCursor([](CommandContext& context) {
                          context.buffer.ClearMark();
                          context.buffer.InsertAtPoint("\n");
                      }));

    registry.Register("self-insert-command", "Insert the character that was pressed.", PerCursor([](CommandContext& context) {
                          context.buffer.ClearMark();
                          if (context.triggeringKey.Special == SpecialKey::None && context.triggeringKey.Codepoint != 0) {
                              context.buffer.InsertAtPoint(text::EncodeCodepointUtf8(context.triggeringKey.Codepoint));
                          }
                      }));

    // A literal-tab insert, not real indent logic (Emacs' own
    // indent-for-tab-command computes indentation; this codebase has no
    // per-mode indent rules yet) -- the v1 scope call is just "typing Tab
    // in Normal mode does something sane" rather than being silently
    // swallowed as Unbound. Global, but a mode's own keymap (e.g.
    // org-mode's org-cycle, markdown-mode's markdown-table-align) still
    // wins via KeymapStack's priority order, so this only ever fires where
    // nothing more specific claimed TAB first.
    registry.Register("indent-for-tab-command", "Insert a tab character at point.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.InsertAtPoint("\t");
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

    // external-modification-safety follow-up: the actual save body, shared
    // by save-buffer (which gates it behind a supersession check) and
    // save-buffer-force (what BufferView's overwrite confirmation invokes
    // on y -- also M-x-reachable as the deliberate "I know, write it
    // anyway" escape hatch).
    const auto saveBufferBody = [](CommandContext& context) {
        try {
            // Only attempted when a command is actually configured (format-
            // on-save follow-up; see FormatOnSave.h) -- FormatCommand() is
            // checked separately from RunFormatCommand()'s result so a
            // configured-but-failing formatter can be reported distinctly
            // from "nothing configured," rather than both looking identical.
            bool formatFailed = false;
            if (FormatCommand()) {
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

            // backup-and-recovery follow-up: preserve the file's prior
            // on-disk content before the save's rename clobbers it, and
            // drop the now-obsolete crash-recovery autosave once the save
            // has actually succeeded. Both swallow their own failures --
            // hooked here rather than inside Buffer::Save so Text/ stays
            // policy-free and scratch auto-save (which calls Buffer::Save
            // directly) never creates backup versions.
            if (context.buffer.Path()) {
                BackupFileBeforeSave(*context.buffer.Path());
            }
            context.buffer.Save(EnsureFinalNewline());
            if (context.buffer.Path()) {
                RemoveAutoSave(*context.buffer.Path());
            }
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

    registry.Register("save-buffer", "Save the current buffer to its associated file.", [saveBufferBody](CommandContext& context) {
        // Never silently overwrite a file someone else wrote underneath
        // this buffer (Emacs' supersession check): hand the decision to a
        // y/n confirmation instead of writing anything.
        if (context.buffer.ExternallyModified()) {
            context.interactiveRequest = InteractiveRequest::ConfirmOverwriteSave;
            return;
        }
        saveBufferBody(context);
    });

    registry.Register("save-buffer-force", "Save the current buffer even if its file changed on disk.", saveBufferBody);

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
                          if (!FormatCommand()) {
                              if (context.message) {
                                  *context.message = "No format command configured.";
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

    registry.Register("toggle-project-sidebar", "Show or hide the left-side project tree.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::ToggleProjectSidebar;
                      });

    registry.Register("focus-project-sidebar",
                      "Move keyboard focus into the project sidebar tree (Up/Down or C-p/C-n to move, Enter to "
                      "open/toggle, Left/Right to collapse/expand, Escape or C-g to return to the editor).",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::FocusProjectSidebar;
                      });

    // session-persistence slice 3: creates the project's .ned/ directory --
    // the strictly-opt-in marker nothing else ever creates -- so the
    // session moves to <root>/.ned/session.json and a .ned/init.janet can
    // be added. A one-shot direct action (no prompt needed), so it acts
    // here rather than via interactiveRequest, same as quit's own direct
    // logic. Also activates session persistence for the *current* run when
    // the root wasn't a marker-carrying project at startup -- without
    // this, the newly initialized project wouldn't start saving until the
    // next launch.
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
                          if (context.message) {
                              *context.message = "Created " + nedDir.string();
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
                          context.lspManager->RequestHover(context.buffer, context.buffer.Point(),
                                                           [message](std::optional<std::string> text) {
                                                               if (message) {
                                                                   *message = text.value_or("No hover information available.");
                                                               }
                                                           });
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
    registry.Register("lsp-rename", "Rename the symbol at point across every file the language server reports it in.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::LspRename;
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
    registry.Register("vcs-commit", "Commit the staged changes, prompting for a single-line message.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsCommit;
                      });
    registry.Register("vcs-stage-hunk", "Stage just the change hunk covering the line at point.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsStageHunk;
                      });
    registry.Register("vcs-unstage-hunk", "Unstage the staged hunk covering the line at point.",
                      [](CommandContext& context) {
                          context.interactiveRequest = InteractiveRequest::VcsUnstageHunk;
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
            const text::Rope& content = context.buffer.Content();
            const std::size_t line    = content.ByteOffsetToLine(context.buffer.Point());
            const auto        blocks  = codefold::FoldableBlocks(*context.mode, context.buffer.Text());
            if (!codefold::ToggleFoldAtLine(context.buffer, content, blocks, line) && context.message) {
                *context.message = "No foldable block starts here.";
            }
        });
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
                          const text::Rope& content = buffer.Content();
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
                          // later one): apply the toggle.
                          for (std::size_t line = lastLine + 1; line-- > firstLine;) {
                              const std::size_t start  = content.LineToByteOffset(line);
                              const std::size_t end    = LineContentEnd(content, start);
                              const std::string text   = content.Substring(start, end - start);
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
    registry.Register("markdown-table-align", "Realign the columns of the GFM table at point to their content width.",
                      [](CommandContext& context) {
                          if (!markdown::AlignTableAtPoint(context.buffer) && context.message) {
                              *context.message = "Not in a table.";
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
    keymap.Bind(ParseKeySequence("C-k"), "kill-line");
    keymap.Bind(ParseKeySequence("C-y"), "yank");
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
    // Same "bind both real input shapes" reasoning as M-x below -- a fast
    // Alt+w press arrives as one Meta-chord, a genuinely separate Escape-
    // then-w press arrives as two.
    keymap.Bind(ParseKeySequence("M-w"), "kill-ring-save");
    keymap.Bind(ParseKeySequence("ESC w"), "kill-ring-save");
    keymap.Bind(ParseKeySequence("RET"), "newline");
    keymap.Bind(ParseKeySequence("TAB"), "indent-for-tab-command");
    keymap.Bind(ParseKeySequence("LEFT"), "backward-char");
    keymap.Bind(ParseKeySequence("RIGHT"), "forward-char");
    // KeyTranslation.cpp already decodes Control+Arrow (ArrowLeftCtrl/
    // ArrowRightCtrl) into Control+Special::Left/Right chords -- ParseKeyChord
    // resolves "C-LEFT"/"C-RIGHT" the same way (the C- prefix strips off
    // first, then LEFT/RIGHT resolve via NamedKeys()), so this is just
    // wiring an already-decodable chord to a command, not new decoding work.
    keymap.Bind(ParseKeySequence("C-LEFT"), "backward-word");
    keymap.Bind(ParseKeySequence("C-RIGHT"), "forward-word");
    // Shift+Arrow follow-up: KeyTranslation.cpp now decodes these (built
    // directly from the raw xterm CSI sequence -- see its own comment,
    // FTXUI has no pre-built Shift+Arrow constant the way it does for
    // Ctrl+Arrow); ParseKeyChord resolves "S-LEFT" etc the same way it
    // already resolves "C-LEFT".
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
    keymap.Bind(ParseKeySequence("C-x C-x"), "exchange-point-and-mark");
    keymap.Bind(ParseKeySequence("M-<"), "beginning-of-buffer");
    keymap.Bind(ParseKeySequence("ESC <"), "beginning-of-buffer");
    keymap.Bind(ParseKeySequence("M->"), "end-of-buffer");
    keymap.Bind(ParseKeySequence("ESC >"), "end-of-buffer");
    keymap.Bind(ParseKeySequence("C-s"), "isearch-forward");
    keymap.Bind(ParseKeySequence("C-r"), "isearch-backward");
    // Emacs binds these to M-%/M-f/M-b. Both real Meta chords and the
    // ESC-prefix fallback are bound (same "cover both real input shapes"
    // reasoning as M-x/M-w/M-/ elsewhere in this function) -- the comment
    // this replaces called Alt-detection unreliable, which was stale even
    // when written (KeyTranslation.h's own header comment already
    // documents real, reliable Meta detection post-FTXUI-migration); only
    // M-x had actually gotten the dual-binding treatment until now.
    keymap.Bind(ParseKeySequence("M-%"), "query-replace-regexp");
    keymap.Bind(ParseKeySequence("ESC %"), "query-replace-regexp");
    keymap.Bind(ParseKeySequence("C-x C-f"), "find-file");
    keymap.Bind(ParseKeySequence("C-x b"), "switch-to-buffer");
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
    keymap.Bind(ParseKeySequence("C-c C-v"), "project-search-visit-result");
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
    keymap.Bind(ParseKeySequence("C-c v h"), "vcs-stage-hunk");
    keymap.Bind(ParseKeySequence("C-c v H"), "vcs-unstage-hunk");
    keymap.Bind(ParseKeySequence("C-c C-r"), "project-replace");
    keymap.Bind(ParseKeySequence("C-c C-p"), "toggle-project-sidebar");
    // sidebar-keyboard-focus follow-up: the non-control second key beside
    // the toggle's own C-c C-p, same pairing pattern the "C-c v" VCS
    // family established for an otherwise-unused plain-letter slot.
    keymap.Bind(ParseKeySequence("C-c p"), "focus-project-sidebar");
    keymap.Bind(ParseKeySequence("C-c m"), "toggle-minimap");
    keymap.Bind(ParseKeySequence("C-x k"), "kill-buffer");
    keymap.Bind(ParseKeySequence("C-c a"), "org-agenda"); // real Org's own actual binding
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
    // Multi-cursor phase. C-UP/C-DOWN were free (KeyTranslation has
    // delivered Ctrl+Arrow since the FTXUI migration; nothing ever bound
    // them) and terminal-reliable, unlike the cross-editor Ctrl+Alt+Arrow
    // or Ctrl+D conventions (C-d is delete-char here, per the keybinding
    // audit's own note that no standard chord was free). M-n mirrors
    // Emacs' multiple-cursors ecosystem living on M-prefixed keys; its
    // C-> convention itself isn't reliably decodable from a terminal.
    keymap.Bind(ParseKeySequence("C-DOWN"), "add-cursor-below");
    keymap.Bind(ParseKeySequence("C-UP"), "add-cursor-above");
    keymap.Bind(ParseKeySequence("M-n"), "select-next-occurrence");
    keymap.Bind(ParseKeySequence("ESC n"), "select-next-occurrence");
    keymap.Bind(ParseKeySequence("C-c d"), "duplicate-line");
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
    keymap.Bind(ParseKeySequence("M-f"), "forward-word");
    keymap.Bind(ParseKeySequence("ESC f"), "forward-word");
    keymap.Bind(ParseKeySequence("M-b"), "backward-word");
    keymap.Bind(ParseKeySequence("ESC b"), "backward-word");
    // Unlike the ESC-only bindings above (whose own comment is stale --
    // real Alt/Meta detection is reliable post-FTXUI-migration, see
    // Source/UI/KeyTranslation.h's own header comment -- flagged here, not
    // fixed, to keep this change focused), M-x is bound both ways
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
    keymap.Bind(ParseKeySequence("C-x r SPC"), "point-to-register");
    keymap.Bind(ParseKeySequence("C-x r j"), "jump-to-register");
    keymap.Bind(ParseKeySequence("C-x r s"), "copy-to-register");
    keymap.Bind(ParseKeySequence("C-x r i"), "insert-register");
    keymap.Bind(ParseKeySequence("C-x r k"), "kill-rectangle");
    keymap.Bind(ParseKeySequence("C-x r d"), "delete-rectangle");
    keymap.Bind(ParseKeySequence("C-x r y"), "yank-rectangle");
    keymap.Bind(ParseKeySequence("C-x r t"), "string-rectangle");
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
