#include "Commands.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>

#include "CodeFold.h"
#include "FinalNewline.h"
#include "FormatOnSave.h"
#include "Lsp/LspManager.h"
#include "Markdown.h"
#include "Org.h"
#include "TabWidth.h"
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
                      [](CommandContext& context) { context.buffer.MoveForward(); });

    registry.Register("backward-char", "Move point backward one grapheme cluster.",
                      [](CommandContext& context) { context.buffer.MoveBackward(); });

    registry.Register("next-line", "Move point down one line, preserving column across a run.",
                      [](CommandContext& context) { context.buffer.MoveToNextLine(TabWidth()); });

    registry.Register("previous-line", "Move point up one line, preserving column across a run.",
                      [](CommandContext& context) { context.buffer.MoveToPreviousLine(TabWidth()); });

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
                      [](CommandContext& context) { context.buffer.MoveForwardWord(); });

    registry.Register("backward-word", "Move point backward one word.",
                      [](CommandContext& context) { context.buffer.MoveBackwardWord(); });

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
    registry.Register("delete-char", "Delete the grapheme cluster at point.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.DeleteForwardAtPoint();
    });

    registry.Register("backward-delete-char", "Delete the grapheme cluster before point.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.DeleteBackwardAtPoint();
    });

    registry.Register("beginning-of-line", "Move point to the beginning of the current line.", [](CommandContext& context) {
        const auto&       content = context.buffer.Content();
        const std::size_t line    = content.ByteOffsetToLine(context.buffer.Point());
        context.buffer.SetPoint(content.LineToByteOffset(line));
    });

    registry.Register("end-of-line", "Move point to the end of the current line.", [](CommandContext& context) {
        const std::size_t target = LineContentEnd(context.buffer.Content(), context.buffer.Point());
        context.buffer.SetPoint(target);
    });

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

    registry.Register("set-mark-command", "Set the mark at point.", [](CommandContext& context) { context.buffer.SetMark(context.buffer.Point()); });

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
    registry.Register("keyboard-quit", "Deactivate the current selection, if any.",
                      [](CommandContext& context) { context.buffer.ClearMark(); });

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

    registry.Register("newline", "Insert a newline at point.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.InsertAtPoint("\n");
    });

    registry.Register("self-insert-command", "Insert the character that was pressed.", [](CommandContext& context) {
        context.buffer.ClearMark();
        if (context.triggeringKey.Special == SpecialKey::None && context.triggeringKey.Codepoint != 0) {
            context.buffer.InsertAtPoint(text::EncodeCodepointUtf8(context.triggeringKey.Codepoint));
        }
    });

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

    registry.Register("save-buffer", "Save the current buffer to its associated file.", [](CommandContext& context) {
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

            context.buffer.Save(EnsureFinalNewline());
            if (context.message) {
                *context.message = "Wrote " + context.buffer.Name() + (formatFailed ? " (format command failed)" : "");
            }
        }
        catch (const std::exception& e) {
            if (context.message) {
                *context.message = e.what();
            }
        }
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
    // C-_, not C-/: real Emacs' own actual undo binding, and the only one
    // that works from a real terminal -- confirmed against a live terminal
    // that C-/ (a literal Control+'/' KeyChord) never fires, since no
    // terminal sends a byte distinguishing Ctrl+/ from Ctrl+_ (both are the
    // same physical unshifted/shifted key on a US layout, and terminals
    // don't track Shift on top of a control byte); the byte they do send,
    // 0x1F, is now decoded as Control+'_' in KeyTranslation.cpp's
    // DecodeBaseKey specifically to make this binding reachable.
    keymap.Bind(ParseKeySequence("C-_"), "undo");
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
    keymap.Bind(ParseKeySequence("C-c C-v"), "project-search-visit-result");
    // VCS blame gutter follow-up: "C-c v" prefix, mirroring "C-c C-b"/
    // "C-c C-M-b" run-task/cancel-task's own choice of an otherwise-unused
    // letter+prefix combination.
    keymap.Bind(ParseKeySequence("C-c v b"), "vcs-show-blame");
    keymap.Bind(ParseKeySequence("C-c v i"), "vcs-blame-detail-at-point"); // "i" for info, next to "b"
    keymap.Bind(ParseKeySequence("C-c v l"), "vcs-show-log");
    keymap.Bind(ParseKeySequence("C-c v v"), "vcs-visit-result");
    keymap.Bind(ParseKeySequence("C-c C-r"), "project-replace");
    keymap.Bind(ParseKeySequence("C-c C-p"), "toggle-project-sidebar");
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
