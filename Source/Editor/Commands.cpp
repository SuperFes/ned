#include "Commands.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>

#include "FormatOnSave.h"
#include "TabWidth.h"
#include "Text/Utf8.h"

namespace ned::editor {

namespace {

    std::size_t LineContentEnd(const text::Rope& content, std::size_t point) {
        const std::size_t line = content.ByteOffsetToLine(point);
        return (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
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

} // namespace

void RegisterBuiltinCommands(CommandRegistry& registry) {
    registry.Register("forward-char", "Move point forward one grapheme cluster.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.MoveForward();
    });

    registry.Register("backward-char", "Move point backward one grapheme cluster.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.MoveBackward();
    });

    registry.Register("next-line", "Move point down one line, preserving column across a run.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.MoveToNextLine(TabWidth());
    });

    registry.Register("previous-line", "Move point up one line, preserving column across a run.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.MoveToPreviousLine(TabWidth());
    });

    registry.Register("forward-word", "Move point forward one word.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.MoveForwardWord();
    });

    registry.Register("backward-word", "Move point backward one word.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.MoveBackwardWord();
    });

    registry.Register("scroll-page-down", "Move point down by roughly a page.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.MoveDownLines(PageLineCount(context.viewportHeight), TabWidth());
    });

    registry.Register("scroll-page-up", "Move point up by roughly a page.", [](CommandContext& context) {
        context.buffer.ClearMark();
        context.buffer.MoveUpLines(PageLineCount(context.viewportHeight), TabWidth());
    });

    registry.Register("delete-char", "Delete the grapheme cluster at point.", [](CommandContext& context) {
        context.buffer.DeleteForwardAtPoint();
    });

    registry.Register("backward-delete-char", "Delete the grapheme cluster before point.", [](CommandContext& context) {
        context.buffer.DeleteBackwardAtPoint();
    });

    registry.Register("beginning-of-line", "Move point to the beginning of the current line.", [](CommandContext& context) {
        const auto&       content = context.buffer.Content();
        const std::size_t line    = content.ByteOffsetToLine(context.buffer.Point());
        context.buffer.ClearMark();
        context.buffer.SetPoint(content.LineToByteOffset(line));
    });

    registry.Register("end-of-line", "Move point to the end of the current line.", [](CommandContext& context) {
        const std::size_t target = LineContentEnd(context.buffer.Content(), context.buffer.Point());
        context.buffer.ClearMark();
        context.buffer.SetPoint(target);
    });

    registry.Register("kill-line", "Kill from point to the end of the line, or the newline if already there.", [](CommandContext& context) {
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
        if (!context.killRing.Empty()) {
            context.buffer.InsertAtPoint(context.killRing.Current());
        }
    });

    registry.Register("undo", "Undo the last change.", [](CommandContext& context) {
        context.buffer.Undo();
    });

    registry.Register("redo", "Redo the last undone change.", [](CommandContext& context) {
        context.buffer.Redo();
    });

    registry.Register("newline", "Insert a newline at point.", [](CommandContext& context) {
        context.buffer.InsertAtPoint("\n");
    });

    registry.Register("self-insert-command", "Insert the character that was pressed.", [](CommandContext& context) {
        if (context.triggeringKey.Special == SpecialKey::None && context.triggeringKey.Codepoint != 0) {
            context.buffer.InsertAtPoint(text::EncodeCodepointUtf8(context.triggeringKey.Codepoint));
        }
    });

    registry.Register("quit", "Exit the editor, or prompt for confirmation if any buffer has unsaved changes.",
                      [](CommandContext& context) {
                          const bool anyModified = std::any_of(context.bufferList.Buffers().begin(), context.bufferList.Buffers().end(),
                                                               [](const auto& buffer) { return buffer->Modified(); });
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

            context.buffer.Save();
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
}

Keymap BuildDefaultGlobalKeymap() {
    Keymap keymap;

    keymap.Bind(ParseKeySequence("C-f"), "forward-char");
    keymap.Bind(ParseKeySequence("C-b"), "backward-char");
    keymap.Bind(ParseKeySequence("C-d"), "delete-char");
    keymap.Bind(ParseKeySequence("DEL"), "backward-delete-char");
    keymap.Bind(ParseKeySequence("C-a"), "beginning-of-line");
    keymap.Bind(ParseKeySequence("C-e"), "end-of-line");
    keymap.Bind(ParseKeySequence("HOME"), "beginning-of-line");
    keymap.Bind(ParseKeySequence("END"), "end-of-line");
    keymap.Bind(ParseKeySequence("C-k"), "kill-line");
    keymap.Bind(ParseKeySequence("C-y"), "yank");
    keymap.Bind(ParseKeySequence("C-/"), "undo");
    keymap.Bind(ParseKeySequence("RET"), "newline");
    keymap.Bind(ParseKeySequence("LEFT"), "backward-char");
    keymap.Bind(ParseKeySequence("RIGHT"), "forward-char");
    keymap.Bind(ParseKeySequence("C-n"), "next-line");
    keymap.Bind(ParseKeySequence("C-p"), "previous-line");
    keymap.Bind(ParseKeySequence("DOWN"), "next-line");
    keymap.Bind(ParseKeySequence("UP"), "previous-line");
    keymap.Bind(ParseKeySequence("PAGEDOWN"), "scroll-page-down");
    keymap.Bind(ParseKeySequence("PAGEUP"), "scroll-page-up");
    keymap.Bind(ParseKeySequence("C-x C-s"), "save-buffer");
    keymap.Bind(ParseKeySequence("C-x C-c"), "quit");
    keymap.Bind(ParseKeySequence("C-s"), "isearch-forward");
    keymap.Bind(ParseKeySequence("C-r"), "isearch-backward");
    // Emacs binds these to M-%/M-f/M-b; Alt isn't reliably detectable here
    // (see Source/UI/KeyTranslation.h), so ESC-prefix is the fallback, same
    // as every other Meta binding.
    keymap.Bind(ParseKeySequence("ESC %"), "query-replace-regexp");
    keymap.Bind(ParseKeySequence("C-x C-f"), "find-file");
    keymap.Bind(ParseKeySequence("C-x b"), "switch-to-buffer");
    keymap.Bind(ParseKeySequence("C-c C-s"), "project-search");
    keymap.Bind(ParseKeySequence("C-c C-v"), "project-search-visit-result");
    keymap.Bind(ParseKeySequence("C-c C-r"), "project-replace");
    keymap.Bind(ParseKeySequence("C-c C-p"), "toggle-project-sidebar");
    keymap.Bind(ParseKeySequence("C-c C-d"), "create-directory");
    keymap.Bind(ParseKeySequence("C-c C-k"), "delete-file");
    // Not "C-c C-m": Ctrl+M and Enter are the same byte at the terminal
    // level (see KeyTranslation.cpp's Enter case), so a "C-m" chord can
    // never actually be produced by real input -- TranslateKey always
    // reports SpecialKey::Enter instead, and that binding would be dead.
    keymap.Bind(ParseKeySequence("C-c C-n"), "rename-file");
    keymap.Bind(ParseKeySequence("C-c C-o"), "find-scratch");
    keymap.Bind(ParseKeySequence("ESC f"), "forward-word");
    keymap.Bind(ParseKeySequence("ESC b"), "backward-word");

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
