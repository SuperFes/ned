#include "Commands.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>

#include "FinalNewline.h"
#include "FormatOnSave.h"
#include "Org.h"
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
    keymap.Bind(ParseKeySequence("C-x 2"), "split-window-below");
    keymap.Bind(ParseKeySequence("C-x 3"), "split-window-right");
    keymap.Bind(ParseKeySequence("C-x 0"), "delete-window");
    keymap.Bind(ParseKeySequence("C-x 1"), "delete-other-windows");
    keymap.Bind(ParseKeySequence("C-x o"), "other-window");
    keymap.Bind(ParseKeySequence("ESC f"), "forward-word");
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
