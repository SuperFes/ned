% NED-COMMANDS(7)

# NAME

ned-commands - every command ned(1) can run

# DESCRIPTION

Every command reachable from `M-x`, from a keybinding, or from Janet via
`ned/run-command`. The key shown is the shortest sequence bound in the default global
keymap; a command with none is reachable from `M-x` and Janet alone. Major-mode and
`init.janet` bindings are not listed -- run `describe-bindings` (`C-c ?`) inside ned
for the live keymap stack.

# COMMANDS

`acp-rewind` (`C-c A r`)

:   Rewind the ACP conversation and its file edits to before an earlier turn.

`acp-send-prompt` (`C-c A p`)

:   Send a message to the active ACP session.

`acp-start-session` (`C-c A s`)

:   Start an Agent Client Protocol (ACP) session with a configured agent, streaming into a buffer.

`acp-stop-session` (`C-c A k`)

:   Stop the active ACP session.

`acp-toggle-panel` (`S-SUPER`)

:   Show, focus, or hide the ACP chat panel.

`add-cursor-above` (`C-UP`)

:   Add a cursor one line above the top-most cursor.

`add-cursor-below` (`C-DOWN`)

:   Add a cursor one line below the bottom-most cursor.

`ask-agent-about-line`

:   Ask the active ACP agent about the diagnostic/test-failure line at point.

`back-to-indentation` (`M-m`)

:   Move point to this line's first non-whitespace character.

`backward-char` (`C-b`)

:   Move point backward one grapheme cluster.

`backward-delete-char` (`DEL`)

:   Delete the grapheme cluster before point.

`backward-kill-word` (`M-DEL`)

:   Kill from the start of the previous word to point.

`backward-sentence` (`M-a`)

:   Move point backward to the start of the current/previous sentence.

`backward-sexp` (`C-M-b`)

:   Move point backward over one balanced expression, using the active mode's syntax tree.

`backward-word` (`C-LEFT`)

:   Move point backward one word.

`beginning-of-buffer` (`C-HOME`)

:   Move point to the start of the buffer.

`beginning-of-line` (`C-a`)

:   Move point to the beginning of the current line.

`bookmark-delete`

:   Delete a saved bookmark (prompts for its name, narrowed by fuzzy matching).

`bookmark-jump` (`C-x r b`)

:   Jump to a saved bookmark (prompts for its name, narrowed by fuzzy matching).

`bookmark-set` (`C-x r m`)

:   Save a named bookmark at point in the current file (prompts for a name, pre-filled with the filename).

`cancel-task` (`C-c C-M-b`)

:   Cancel a running task started by run-task.

`cancel-tests` (`C-c T k`)

:   Cancel the test run started by run-tests.

`capitalize-word` (`M-c`)

:   Capitalize from point to the end of the next word, moving over it.

`change-signature`

:   Add, remove or reorder the parameters of the function at point, rewriting every call site and other same-name definition found in the project through a reviewable multibuffer (C-c C-c to apply). C++ only; declines rather than guesses at a variadic parameter, a new parameter with no default, or a same-named definition with a different parameter count.

`check-format-conventions`

:   Scan the project for names that don't conform to their configured case convention (see ned/set-format-case-convention), into *case violations*.

`clear-coverage-report`

:   Clear the loaded coverage report and its gutter marks.

`code-fold-toggle`

:   Toggle folding the code block (or, in a multibuffer, the excerpt) starting on the line at point.

`color-at-point`

:   Rewrite the colour literal under point in another notation (hex, rgb(), hsl(), hwb(), a CSS colour name), chosen from a list.

`convert-line-endings-to-cr`

:   Save this buffer with CR (classic Mac) line endings.

`convert-line-endings-to-crlf`

:   Save this buffer with CRLF (Windows) line endings.

`convert-line-endings-to-lf`

:   Save this buffer with LF (Unix) line endings.

`copy-to-register` (`C-x r s`)

:   Copy the region into a register (prompts for the register name).

`create-directory` (`C-c C-d`)

:   Create a new directory (prompts for its path).

`dap-add-watch`

:   Add a watch expression, re-evaluated every time the *debug* buffer is rebuilt.

`dap-ask-agent`

:   Send the stopped debug session's stack and variables to the active ACP agent as a prompt.

`dap-attach`

:   Attach a debug session to a running process for the active language (ned/set-dap-attach).

`dap-continue` (`F5`)

:   Start a debug session for the active language, or continue a stopped one.

`dap-evaluate`

:   Evaluate an expression in the stopped debug session's top frame.

`dap-expand-variable`

:   Expand the composite variable on the current *debug* buffer line.

`dap-jump-to-line`

:   Move the stopped thread's execution point directly to the current line, without running through the skipped code (adapter support required, DAP's gotoTargets/goto requests).

`dap-line-inspect`

:   Evaluate every sub-expression on the current line in the stopped debug session at once.

`dap-pause`

:   Pause the running debuggee.

`dap-remove-watch`

:   Remove the watch expression on the current *debug* buffer line.

`dap-restart-frame`

:   Restart execution from the top of the stack frame on the current *debug* buffer line.

`dap-reverse-continue`

:   Continue the stopped debug session backwards (adapter support required).

`dap-run-to-cursor`

:   Run the stopped debug session until it reaches the current line (a temporary breakpoint, cleared again on the next stop).

`dap-select-exception-breakpoints`

:   Toggle which of the adapter's advertised exception filters halt the debuggee.

`dap-select-thread`

:   Pick which thread inspection/stepping/continue target in the stopped debug session.

`dap-set-breakpoint-condition`

:   Set or clear a condition on the breakpoint at the current line.

`dap-set-breakpoint-hit-condition`

:   Set or clear a hit-count condition on the breakpoint at the current line (e.g. "> 5" -- only stops once satisfied).

`dap-set-breakpoint-log-message`

:   Set or clear a log message on the breakpoint at the current line (a logpoint never halts the debuggee).

`dap-set-variable`

:   Edit the variable's value on the current *debug* buffer line.

`dap-show-debug`

:   Show the stopped debug session's stack and variables in a *debug* buffer.

`dap-show-disassembly`

:   Show instructions around the stopped frame's program counter in a *disassembly* buffer.

`dap-show-memory-at-point`

:   Show a hex dump of memory for the variable on the current *debug* buffer line in a *memory* buffer.

`dap-show-memory-image-at-point`

:   Show a grayscale image of memory for the variable on the current *debug* buffer line (repeating structures/zero-fill/embedded text visible at a glance, gf's Data tab).

`dap-show-pointer-graph`

:   Browse the composite variable on the current *debug* buffer line as an expandable pointer/field graph.

`dap-step-back`

:   Step the stopped debug session backwards one line (adapter support required).

`dap-step-into` (`F11`)

:   Step into the call on the current line in the stopped debug session.

`dap-step-out` (`S-F11`)

:   Step out of the current function in the stopped debug session.

`dap-step-over` (`F10`)

:   Step over the current line in the stopped debug session.

`dap-stop` (`S-F5`)

:   Stop the running debug session.

`dap-toggle-breakpoint` (`F9`)

:   Toggle a breakpoint on the current line.

`dap-toggle-console` (`C-c D`)

:   Show or hide the debug console (REPL) panel.

`dap-toggle-data-breakpoint`

:   Watch the *debug* buffer variable at point for changes, or remove the data breakpoint on point's own row (adapter support required).

`dap-toggle-function-breakpoint`

:   Toggle a breakpoint on a named function, by prompted name.

`dap-toggle-hex-format`

:   Toggle hex display for the watch or variable value on the current *debug* buffer line.

`dap-toggle-threads`

:   Show or hide the live threads panel (refreshes on every stop, unlike dap-select-thread's one-shot picker).

`dap-toggle-watch-graph`

:   Toggle a sparkline (scalar watch history) or bar chart (numeric array watch) on the current *debug* buffer watch line.

`delete-blank-lines` (`C-x C-o`)

:   On a blank line, delete surrounding blank lines (leaving one); otherwise delete any blank lines following this one.

`delete-char` (`C-d`)

:   Delete the grapheme cluster at point.

`delete-file` (`C-c C-k`)

:   Delete a file or directory (prompts for its path, then confirms -- recursive for a directory).

`delete-indentation` (`M-^`)

:   Join this line to the previous one, with one space at the join.

`delete-other-windows` (`C-x 1`)

:   Close every window except the current one.

`delete-rectangle` (`C-x r d`)

:   Delete the rectangle defined by point and mark, without saving it.

`delete-window` (`C-x 0`)

:   Close the current window (does nothing if it's the only one).

`describe-bindings` (`C-c ?`)

:   List every reachable key binding, grouped by keymap layer and annotated with each command's docstring, into *bindings*.

`downcase-word` (`M-l`)

:   Lowercase from point to the end of the next word, moving over it.

`duplicate-line` (`C-c d`)

:   Duplicate the current line, moving point into the copy.

`end-of-buffer` (`C-END`)

:   Move point to the end of the buffer.

`end-of-line` (`C-e`)

:   Move point to the end of the current line.

`enlarge-window` (`C-x ^`)

:   Grow the current window taller against its nearest horizontal split.

`enlarge-window-horizontally` (`C-x }`)

:   Grow the current window wider against its nearest vertical split.

`exchange-point-and-mark` (`C-x C-x`)

:   Swap point and mark.

`execute-extended-command` (`M-x`)

:   Run a command by name (M-x), narrowed by fuzzy matching as you type.

`expand-selection` (`M-=`)

:   Grow the selection to the next enclosing syntax node (word, expression, statement, ...).

`expand-snippet`

:   Expand the registered snippet whose trigger word ends at point.

`fill-paragraph` (`M-q`)

:   Reflow the paragraph at point to fill-column, preserving indentation and (if uniform) a per-line comment prefix.

`find-file` (`C-x C-f`)

:   Open a file in a new buffer (or create one for a path that doesn't exist yet).

`find-recent-file` (`C-c f r`)

:   Open a recently-opened file (any project), narrowed by fuzzy matching as you type.

`find-scratch` (`C-c C-o`)

:   Open or create a named scratch note (prompts for its name; not tied to any project, auto-saved).

`fix-case-violation-at-point`

:   Rename the case-convention violation at point to Editor/FormatCase.h's suggested conforming name, via rename-symbol's own prompt (pre-filled, not applied silently).

`focus-debug-panel`

:   Move keyboard focus into the debug panel (Up/Down to move, Left/Right to collapse/expand, Enter to visit a breakpoint's line, Space to enable/disable it, 'd' to remove it, 'c'/'h'/'l' to set a condition/hit condition/log message, 'a' to add a function breakpoint, 'X' to clear the section, 'g' to refresh, Escape or C-g to return to the editor).

`focus-project-sidebar` (`C-c p`)

:   Move keyboard focus into the project sidebar tree (Up/Down or C-p/C-n to move, Enter to open/toggle, Left/Right to collapse/expand, Escape or C-g to return to the editor).

`focus-vcs-panel` (`C-c v p`)

:   Move keyboard focus into the VCS status panel (Up/Down to move, Space to mark, Enter to open/toggle, 'a'/'u' to stage/unstage the marked (or focused) file, 'c' to compose a commit, 'w'/'n' to switch/create a branch, Escape or C-g to return to the editor).

`format-buffer`

:   Format the whole buffer: your configured external formatter if one's set, falling back to ned's own per-language reindent and hygiene cleanup.

`forward-char` (`C-f`)

:   Move point forward one grapheme cluster.

`forward-sentence` (`M-e`)

:   Move point forward to the end of the current/next sentence.

`forward-sexp` (`C-M-f`)

:   Move point forward over one balanced expression, using the active mode's syntax tree.

`forward-word` (`C-RIGHT`)

:   Move point forward one word.

`goto-line` (`M-g M-g`)

:   Jump to a line by number (prompts for it).

`goto-matching-bracket`

:   Jump between a bracket under point and its partner, using the active mode's syntax tree.

`indent-buffer`

:   Reindent the whole buffer to its computed indentation.

`indent-for-tab-command` (`TAB`)

:   Reindent the current line to its computed indentation, or -- with an active region -- rigidly indent every line the region spans by one indent width; otherwise expand the snippet trigger before point, or insert a tab character.

`indent-region`

:   Reindent every line the region between point and mark spans.

`insert-literal-tab` (`C-q`)

:   Insert a literal tab character at point, unconditionally -- bypassing indent-for-tab-command's reindent/snippet-expansion logic and the buffer's own useTabs=false space substitution.

`insert-register` (`C-x r i`)

:   Insert the text saved in a register (prompts for the register name).

`isearch-backward` (`C-r`)

:   Incrementally search backward.

`isearch-forward` (`C-s`)

:   Incrementally search forward.

`jump-back` (`C-x C-SPC`)

:   Jump back to the position before the last location-jumping command (goto-definition, goto-line, bookmark-jump, jump-to-register, ...).

`jump-forward` (`C-x C-n`)

:   Jump forward again after jump-back -- the redo direction of jump-back.

`jump-to-register` (`C-x r j`)

:   Jump to the point saved in a register (prompts for the register name).

`just-one-space` (`M-SPC`)

:   Replace the whitespace around point with a single space.

`keyboard-quit` (`C-g`)

:   Deactivate the current selection and collapse to one cursor.

`kill-buffer` (`C-x k`)

:   Close the current buffer, prompting to save first if it has unsaved changes.

`kill-line` (`C-k`)

:   Kill from point to the end of the line, or the newline if already there.

`kill-rectangle` (`C-x r k`)

:   Kill the rectangle defined by point and mark, saving it for yank-rectangle.

`kill-region` (`C-w`)

:   Kill (cut) the region between point and mark into the kill ring.

`kill-ring-save` (`M-w`)

:   Copy the region between point and mark into the kill ring, without deleting it.

`kill-word` (`M-DELETE`)

:   Kill from point to the end of the next word.

`kmacro-end-or-call-macro` (`F4`)

:   Stop recording a keyboard macro, or replay the last recorded one if not currently recording.

`kmacro-insert-macro-definition`

:   Insert a named macro's (ned/register-macro ...) form at point, for pasting into init.janet.

`kmacro-name-last-macro`

:   Give the most recently recorded keyboard macro a name.

`kmacro-start-macro` (`F3`)

:   Begin recording a keyboard macro.

`list-buffers` (`C-x C-b`)

:   Open a keyboard-navigable buffer list panel (mark/kill, switch).

`load-coverage-report` (`C-c T c`)

:   Load and parse the configured coverage report (see ned/set-coverage-file) -- an lcov .info file -- into the per-line covered/uncovered/partial-branch gutter marks.

`lsp-call-hierarchy-incoming` (`C-c l c`)

:   Show callers of the symbol at point, via the language server (callHierarchy/incomingCalls).

`lsp-call-hierarchy-outgoing` (`C-c l C`)

:   Show what the symbol at point calls, via the language server (callHierarchy/outgoingCalls).

`lsp-code-action` (`C-c C-a`)

:   Show LSP code actions (quick fixes) available at point.

`lsp-complete` (`C-M-i`)

:   Request completion candidates from the language server at point.

`lsp-diagnostics-buffer`

:   Show every open buffer's LSP diagnostics, stitched into one *diagnostics* buffer.

`lsp-document-highlight`

:   Highlight all occurrences of the symbol at point in this buffer.

`lsp-goto-declaration` (`C-c l d`)

:   Jump to the declaration of the symbol at point, via the language server.

`lsp-goto-definition` (`M-.`)

:   Jump to the definition of the symbol at point, via the language server.

`lsp-goto-implementation` (`C-c l i`)

:   Jump to the implementation of the symbol at point, via the language server.

`lsp-goto-symbol` (`M-g i`)

:   Jump to a symbol in the current buffer, via the language server (textDocument/documentSymbol).

`lsp-goto-type-definition` (`C-c l t`)

:   Jump to the type definition of the symbol at point, via the language server.

`lsp-hover` (`C-c C-j`)

:   Show hover information from the language server at point.

`lsp-linked-editing-range` (`C-c l r`)

:   Start live-mirrored editing across every range the language server reports as linked to the one at point (e.g. a markup element's matching opening/closing tag name).

`lsp-peek-definition` (`C-c l p`)

:   Preview the definition of the symbol at point without leaving the buffer, via the language server.

`lsp-quick-fix` (`C-c C-q`)

:   Apply the LSP quick fix at point immediately, no confirmation.

`lsp-rename`

:   Rename the symbol at point across every file the language server reports it in.

`lsp-run-code-lens-at-point`

:   Run the code lens at point, resolving it first if needed.

`lsp-show-diagnostic` (`C-c C-e`)

:   Show the LSP diagnostic message at point (or on point's line), if any.

`lsp-show-log`

:   Switch to the *lsp log* buffer of LSP errors/disconnects.

`lsp-signature-help`

:   Show parameter/signature information from the language server at point.

`lsp-type-hierarchy-subtypes` (`C-c l S`)

:   Show subtypes of the symbol at point, via the language server (typeHierarchy/subtypes).

`lsp-type-hierarchy-supertypes` (`C-c l s`)

:   Show supertypes of the symbol at point, via the language server (typeHierarchy/supertypes).

`lsp-workspace-symbol` (`C-c l w`)

:   Search for a symbol across the whole project, via the language server (workspace/symbol).

`mark-whole-buffer` (`C-x h`)

:   Put point at the beginning and mark at the end of the buffer.

`markdown-metadown`

:   Move the table row at point down, or the current line otherwise.

`markdown-metaup`

:   Move the table row at point up, or the current line otherwise.

`markdown-table-align`

:   Realign the columns of the GFM table at point to their content width, or otherwise do whatever plain TAB would have done.

`markdown-table-delete-column`

:   Delete the current table column.

`markdown-table-insert-column`

:   Insert an empty table column to the right of the current one.

`markdown-table-insert-row`

:   Insert an empty table row above the current one.

`markdown-table-kill-row`

:   Remove the current table row.

`markdown-table-move-column-left`

:   Swap the current table column with the one to its left.

`markdown-table-move-column-right`

:   Swap the current table column with the one to its right.

`markdown-table-previous-cell`

:   Realign the table at point and move to the previous cell.

`merge-keep-base` (`C-c x k`)

:   Resolve the conflict hunk at point by taking the diff3 base section.

`merge-take-all-ours` (`C-c x O`)

:   Resolve every conflict hunk in the buffer by taking "ours".

`merge-take-all-theirs` (`C-c x T`)

:   Resolve every conflict hunk in the buffer by taking "theirs".

`merge-take-both` (`C-c x b`)

:   Resolve the conflict hunk at point by taking both sides (ours then theirs).

`merge-take-neither` (`C-c x d`)

:   Resolve the conflict hunk at point by deleting it entirely.

`merge-take-ours` (`C-c x o`)

:   Resolve the conflict hunk at point by taking "ours".

`merge-take-theirs` (`C-c x t`)

:   Resolve the conflict hunk at point by taking "theirs".

`modern-copy`

:   Copy the region into the kill ring; with no active region, copy the current line (including its trailing newline) instead. KeymapStyle::Modern's C-c.

`modern-cut`

:   Cut the region into the kill ring; with no active region, cut the current line (including its trailing newline) instead. KeymapStyle::Modern's C-x.

`modern-paste`

:   Replace the active region (if any) with the most recent kill-ring entry, then paste. KeymapStyle::Modern's C-v.

`move-line-down` (`M-DOWN`)

:   Move the current line down, swapping it with the line below.

`move-line-up` (`M-UP`)

:   Move the current line up, swapping it with the line above.

`multibuffer-apply-changes` (`C-c C-c`)

:   Apply every edited excerpt in this multibuffer, asking first whether to write into the open source buffers (reviewable, undoable) or straight to the files.

`multibuffer-commit-changes`

:   Write every edited excerpt in the current multibuffer (e.g. *diagnostics*, *references: ...*) back to its real source buffer. Does not save to disk.

`multibuffer-commit-file`

:   Write back only the edited excerpts belonging to the source file under point, into that file's own buffer, leaving every other file in this multibuffer pending.

`multibuffer-commit-file-to-disk`

:   Write back only the edited excerpts belonging to the source file under point, straight to that file, leaving every other file in this multibuffer pending.

`multibuffer-commit-to-disk`

:   Write every edited excerpt in the current multibuffer straight to its source file, without opening a buffer for it. A file whose buffer is open with unsaved edits is applied into that buffer instead.

`multibuffer-revert-excerpt`

:   Put the excerpt under point back to the text it was built with -- the "not this one" gesture in a *project replace*/*references*/*diagnostics* review.

`multibuffer-revert-file`

:   Put every excerpt from the same source file as the one under point back to the text it was built with.

`narrow-to-region` (`C-x n n`)

:   Restrict editing/display to the region defined by point and mark.

`ned-init-project`

:   Create the project's .ned/ directory (opt-in home for session data and a project init.janet).

`new-terminal` (`C-c C-t`)

:   Open one more terminal tab alongside any already open, and switch to it.

`newline` (`RET`)

:   Insert a newline at point, electric-indenting the new line when the mode supports it.

`next-conflict-hunk` (`C-c x n`)

:   Move point to the next unresolved merge-conflict hunk, wrapping.

`next-error` (`M-g n`)

:   Jump to the next location in the last results buffer built (search, VCS status, diagnostics, ...).

`next-excerpt`

:   Move point to the next excerpt's body in a multibuffer.

`next-line` (`C-n`)

:   Move point down one line -- one WRAPPED row, when wrapping is on -- preserving column across a run.

`open-line` (`C-o`)

:   Insert a newline after point, leaving point in place.

`open-link-at-point` (`C-c C-l`)

:   Follow the link (Org bracket link, URL, or file path) at point.

`open-project` (`C-c P o`)

:   Open a project by path, registering it under a name (prompted, defaulting to the directory's own basename) if it isn't already known, then switch to it.

`org-agenda` (`C-c a`)

:   List active (non-DONE) TODO headlines across every .org file in the project.

`org-capture` (`C-c k`)

:   Capture a note into a registered org-capture template.

`org-clock-in`

:   Clock in to the Org headline at point.

`org-clock-out`

:   Clock out of whichever headline currently has a running clock.

`org-clock-report`

:   List every headline in the current buffer with clocked time, including subtree totals.

`org-cycle`

:   Cycle the fold state of the subtree at point, or realign the table at point.

`org-cycle-priority`

:   Cycle the [#A]/[#B]/[#C] priority cookie of the headline at point.

`org-cycle-todo`

:   Cycle the TODO keyword of the headline at point.

`org-deadline`

:   Set the DEADLINE: timestamp of the headline at point.

`org-delete-property`

:   Delete a property from the headline at point.

`org-metadown`

:   Move the table row at point down, or the current line otherwise.

`org-metaup`

:   Move the table row at point up, or the current line otherwise.

`org-schedule`

:   Set the SCHEDULED: timestamp of the headline at point.

`org-set-property`

:   Set a property of the headline at point (prompts for name, then value).

`org-set-tags`

:   Set the tags of the headline at point (colon-separated, e.g. "work:urgent").

`org-table-align`

:   Realign the columns of the table at point to their content width.

`org-table-delete-column`

:   Delete the current table column.

`org-table-insert-column`

:   Insert an empty table column to the right of the current one.

`org-table-insert-hline`

:   Insert a separator hrule below the current table row.

`org-table-insert-row`

:   Insert an empty table row above the current one.

`org-table-kill-row`

:   Remove the current table row.

`org-table-move-column-left`

:   Swap the current table column with the one to its left.

`org-table-move-column-right`

:   Swap the current table column with the one to its right.

`org-table-previous-cell`

:   Realign the table at point and move to the previous cell.

`org-toggle-checkbox`

:   Toggle the checkbox at point, reflecting the change up into any parent.

`other-window` (`C-x o`)

:   Move focus to the next window.

`pick-color` (`C-c #`)

:   Adjust the colour literal under point interactively (R/G/B, H/S/L, alpha), or insert a new one where point is.

`point-to-register` (`C-x r SPC`)

:   Save point in a register (prompts for the register name).

`previous-conflict-hunk` (`C-c x p`)

:   Move point to the previous unresolved merge-conflict hunk, wrapping.

`previous-error` (`M-g p`)

:   Jump to the previous location in the last results buffer built.

`previous-excerpt`

:   Move point to the previous excerpt's body in a multibuffer.

`previous-line` (`C-p`)

:   Move point up one line -- one WRAPPED row, when wrapping is on -- preserving column across a run.

`project-find-file` (`C-c C-f`)

:   Open a file under the project root, narrowed by fuzzy matching as you type.

`project-find-references` (`M-?`)

:   Find every whole-word match for the identifier at point across the project, in a *references* multibuffer.

`project-replace` (`C-c C-r`)

:   Search-and-replace a regex pattern across all files under the current directory, with one whole-batch confirmation (not per-match) after previewing every affected file/line.

`project-search` (`C-c C-s`)

:   Search all files under the current directory (recursively) for a regex pattern, in a new results buffer.

`project-search-visit-result` (`C-c C-v`)

:   Jump to the file:line under point in a project-search results buffer.

`query-replace-regexp` (`M-%`)

:   Interactively replace regexp matches, confirming each one.

`quit` (`C-x C-c`)

:   Exit the editor, or prompt for confirmation if any buffer has unsaved changes.

`recenter` (`C-l`)

:   Scroll so the line at point is centered in the window.

`recover-file`

:   Restore the current buffer's content from a recent backup or crash-recovery autosave (prompts for the version; the restore is one undoable step and must be saved to keep).

`redo` (`M-/`)

:   Redo the last undone change.

`refresh-toolchain-include-paths`

:   Clear the cached compiler-derived default include paths, so the next lookup re-probes the real toolchain.

`reload-format-config`

:   Re-read format.janet (personal, then project) without restarting ned.

`remove-last-cursor` (`C-S-DOWN`)

:   Remove the most recently added secondary cursor.

`rename-file` (`C-c C-n`)

:   Rename/move a file or directory (prompts for its current path, then the new one).

`rename-file-to-match-type`

:   Rename this file after the type declared in it (class/interface/enum/struct/record), keeping any compound suffix -- Thing.class.php stays *.class.php. Confirms first; takes the outermost type if the file holds several.

`rename-symbol` (`C-c C-M-r`)

:   Rename the symbol at point -- scope-aware and in this buffer alone when it is a local binding, otherwise across every file the language server reports it in.

`rename-type-to-match-file`

:   Rename the type declared in this file after the file's own name -- the inverse of rename-file-to-match-type. Confirms first, then renames through the usual review.

`rerun-failed-tests` (`C-c T f`)

:   Re-run every currently-failed test, one filtered run per test, merging the results.

`run-repl` (`C-c r`)

:   Open (or switch to) a Janet-configured REPL's own interactive session (see ned/set-repl-command).

`run-task` (`C-c C-b`)

:   Run a Janet-configured task (see ned/set-task-command), streaming its output into a buffer.

`run-test-at-point` (`C-c T .`)

:   Run only the test definition containing point (Mode::testDiscovery), through the configured filter template (see ned/set-test-filter-command).

`run-tests` (`C-c T t`)

:   Run the project's tests (see ned/set-test-command), streaming output into *test output* and parsing results into *test results* and the per-test gutter marks.

`save-buffer` (`C-x C-s`)

:   Save the current buffer to its associated file.

`save-buffer-force`

:   Save the current buffer even if its file changed on disk.

`save-some-buffers` (`C-x s`)

:   Save every modified file-backed buffer.

`scroll-page-down` (`C-v`)

:   Move point down by roughly a page.

`scroll-page-up` (`M-v`)

:   Move point up by roughly a page.

`search-everywhere` (`M-s`)

:   Fuzzy-search commands (with their keybindings), named macros, project files, open buffers, symbols, file text, a language server's own commands, themes and registered projects in one merged, ranked list; TAB narrows to one kind.

`search-in-results` (`C-c s`)

:   Search only the files the current results/multibuffer references, in a new results buffer.

`select-all-occurrences`

:   Add a cursor selecting every occurrence of the current selection (or the word at point).

`select-next-occurrence` (`M-n`)

:   Select the word at point, or add a cursor at the next occurrence of the selection.

`select-theme`

:   Switch the color theme, narrowed by fuzzy matching, previewing the highlighted candidate live.

`self-insert-command` (`!`)

:   Insert the character that was pressed.

`set-mark-command` (`C-SPC`)

:   Set the mark at point, or deactivate it when pressed again in place.

`shift-select-backward-char` (`S-LEFT`)

:   Move point backward one grapheme cluster, extending the selection.

`shift-select-forward-char` (`S-RIGHT`)

:   Move point forward one grapheme cluster, extending the selection.

`shift-select-next-line` (`S-DOWN`)

:   Move point down one line -- one wrapped row, when wrapping is on -- extending the selection.

`shift-select-previous-line` (`S-UP`)

:   Move point up one line -- one wrapped row, when wrapping is on -- extending the selection.

`show-massif-graph`

:   Parse a Valgrind massif.out file and show a heap-usage-over-time graph.

`show-messages`

:   Show the *Messages* buffer -- a filterable, on-disk-backed log of ned's own errors/diagnostics.

`show-test-results` (`C-c T r`)

:   Show the parsed failures from the last test run (*test results*).

`shrink-selection` (`M--`)

:   Shrink the selection back to the node it was expanded from.

`shrink-window`

:   Shrink the current window against its nearest horizontal split.

`shrink-window-horizontally` (`C-x {`)

:   Shrink the current window against its nearest vertical split.

`split-window-below` (`C-x 2`)

:   Split the current window into two, one above the other.

`split-window-right` (`C-x 3`)

:   Split the current window into two, side by side.

`string-rectangle` (`C-x r t`)

:   Replace the rectangle defined by point and mark with a typed string on every line.

`suspend-frame` (`C-z`)

:   Suspend ned and return to the shell (job control), like Emacs' C-z.

`switch-header-source` (`M-o`)

:   Switch between a C/C++ header and its implementation file.

`switch-project` (`C-c P s`)

:   Switch to a registered project, narrowed by fuzzy matching as you type.

`switch-to-buffer` (`C-x b`)

:   Switch to another open buffer by name.

`tab-move-left` (`C-c <`)

:   Move the current buffer's tab one position left in the tab bar.

`tab-move-right` (`C-c >`)

:   Move the current buffer's tab one position right in the tab bar.

`tab-next` (`C-c .`)

:   Switch to the next tab in the tab bar, wrapping at the end.

`tab-previous` (`C-c ,`)

:   Switch to the previous tab in the tab bar, wrapping at the start.

`theme-gallery`

:   Show every themed surface and named paint as a live swatch, with a contrast readout.

`toggle-binary-safeguards`

:   Toggle whether a binary-detected buffer's format/line-ending/final-newline safeguards apply.

`toggle-debug-panel` (`S-F9`)

:   Show or hide the left-side debug panel (source, function, data and exception breakpoints).

`toggle-indent-guides`

:   Show or hide vertical indentation guide glyphs within each line's own leading whitespace.

`toggle-inline-debug-values`

:   Show or hide the debugger's inline values (the stopped frame's locals, shown after the lines that mention them).

`toggle-inline-diagnostics`

:   Show or hide inline diagnostic annotation rows (carets + message under a line with a diagnostic).

`toggle-janet-repl` (`C-c j`)

:   Show or hide the built-in Janet REPL panel.

`toggle-line-comment` (`M-;`)

:   Comment or uncomment the current line, or every line the region spans.

`toggle-minimap` (`C-c m`)

:   Show or hide the minimap (replacing/restoring the plain scrollbar).

`toggle-project-sidebar` (`C-c C-p`)

:   Show or hide the left-side project tree.

`toggle-read-only`

:   Toggle whether the current buffer accepts edits.

`toggle-tab-glyphs`

:   Show or hide a glyph marking the first cell of every real (literal) tab byte.

`toggle-terminal` (`C-``)

:   Show and focus the built-in terminal drawer; hide it if it is already focused.

`toggle-trailing-whitespace-highlight`

:   Show or hide a background highlight on trailing whitespace (spaces/tabs after the last non-whitespace character on a line).

`toggle-vcs-panel` (`C-c V`)

:   Show or hide the left-side VCS status panel (staged/unstaged/untracked files); collapses the project sidebar if it's currently shown in the same slot.

`transpose-chars` (`C-t`)

:   Interchange the graphemes around point, moving forward.

`transpose-words` (`M-t`)

:   Interchange the words around point, moving forward.

`undo` (`C-/`)

:   Undo the last change.

`undo-buffer-only`

:   Undo the last change in this buffer alone, even when it was part of a multi-file edit that plain undo would roll back in full.

`unfold-all`

:   Remove every fold in the current buffer, revealing all hidden lines.

`unindent` (`S-TAB`)

:   Rigidly remove one indent width from every line the active region spans, or from the current line if no region is active.

`universal-argument` (`C-u`)

:   Begin reading a numeric prefix argument for the next command.

`upcase-word` (`M-u`)

:   Uppercase from point to the end of the next word, moving over it.

`vcs-blame-buffer`

:   Show per-line commit attribution for the current file in a *vcs blame* buffer.

`vcs-blame-detail-at-point` (`C-c v i`)

:   Show full commit info (author/date/summary) for the blamed line at point.

`vcs-branches`

:   List branches in a *vcs branches* buffer.

`vcs-commit` (`C-c v c`)

:   Commit the staged changes -- opens a *vcs commit message* buffer to compose in.

`vcs-commit-abort`

:   Discard the in-progress commit message (bound C-c C-k in *vcs commit message*).

`vcs-commit-amend` (`C-c v C`)

:   Amend the previous commit -- opens a *vcs commit message* buffer pre-filled with its message.

`vcs-commit-finish`

:   Finish composing and commit (bound C-c C-c in *vcs commit message*).

`vcs-create-branch` (`C-c v n`)

:   Create and switch to a new branch, prompting for its name.

`vcs-extend-commit` (`C-c v e`)

:   Fold the staged changes into the previous commit, keeping its message unchanged.

`vcs-full-diff-buffer` (`C-c v d`)

:   Show every changed file's real diff, stitched into one *vcs diff* buffer.

`vcs-next-hunk` (`C-c v N`)

:   Move point to the next changed hunk in this buffer.

`vcs-previous-hunk` (`C-c v P`)

:   Move point to the previous changed hunk in this buffer.

`vcs-revert-hunk` (`C-c v x`)

:   Discard the change hunk covering the line at point from the working tree.

`vcs-reword-commit` (`C-c v r`)

:   Reword the previous commit's message, leaving its tree exactly as-is -- opens a *vcs commit message* buffer pre-filled with its message.

`vcs-sequence-abort` (`C-c x A`)

:   Abort the in-progress rebase/merge/cherry-pick, restoring the state before it began.

`vcs-sequence-continue` (`C-c x c`)

:   Continue the in-progress rebase/merge/cherry-pick once every conflict is resolved and saved.

`vcs-sequence-skip` (`C-c x s`)

:   Skip the commit the in-progress rebase/cherry-pick stopped on.

`vcs-show-blame` (`C-c v b`)

:   Show per-line commit attribution for the current file, inline in the gutter.

`vcs-show-log` (`C-c v l`)

:   Show commit history for the current file in a *vcs log* buffer.

`vcs-stage-file` (`C-c v a`)

:   Stage the file on the *vcs status* line at point, or the current file.

`vcs-stage-hunk` (`C-c v h`)

:   Stage just the change hunk covering the line at point.

`vcs-status` (`C-c v s`)

:   Show the working tree's changed/untracked files in a *vcs status* buffer.

`vcs-switch-branch` (`C-c v w`)

:   Switch to another branch, with Tab completion over the branch list.

`vcs-unstage-file` (`C-c v u`)

:   Unstage the file on the *vcs status* line at point, or the current file.

`vcs-unstage-hunk` (`C-c v H`)

:   Unstage the staged hunk covering the line at point.

`vcs-visit-result` (`C-c v v`)

:   Jump to the file:line under point in a *vcs blame* buffer.

`widen` (`C-x n w`)

:   Remove any narrowing, restoring the full buffer.

`yank` (`C-y`)

:   Insert the most recent kill-ring entry at point.

`yank-pop` (`M-y`)

:   Replace a just-yanked entry with the next-older kill-ring entry.

`yank-rectangle` (`C-x r y`)

:   Insert the last killed rectangle at point.

`zap-to-char` (`M-z`)

:   Kill forward from point up to and including the next occurrence of a character.

# SEE ALSO

**ned**(1), **ned-janet**(7)
