# Scripting reference

Every `ned/*` function available to `init.janet`, a project's `.ned/init.janet`,
or a plugin. **Generated** from the live binding table -- edit the docstring at
the `Register<Fn>` call site, not this file.

Regenerate with `NED_BLESS_COMMAND_DOCS=1 ./build/ned_tests "[CommandDocs]"`.
Held against the binding table on every build, so it cannot drift.

159 bindings.

## `ned/backward-char`

Move point backward one grapheme cluster.

## `ned/backward-delete-char`

Delete the grapheme cluster before point.

## `ned/buffer-text`

Return the full text of the current buffer.

## `ned/capture-background`

The capture name's own overridden background color, or nil if unset.

## `ned/capture-bold`

The capture name's own overridden bold trait, or nil if unset.

## `ned/capture-class`

The capture name's remapped syntax class name, or nil if using the built-in mapping.

## `ned/capture-foreground`

The capture name's own overridden foreground color, or nil if unset (no inheritance walk).

## `ned/capture-italic`

The capture name's own overridden italic trait, or nil if unset.

## `ned/capture-names`

Every known tree-sitter capture name, sorted: the built-in defaults table merged with every name seen from a loaded grammar's query or configured via ned/set-capture-*.

## `ned/capture-strikethrough`

The capture name's own overridden strikethrough trait, or nil if unset.

## `ned/capture-underlined`

The capture name's own overridden underlined trait, or nil if unset.

## `ned/define-key`

Bind a key sequence (e.g. "C-c C-j") to a command name.

## `ned/delete-char`

Delete the grapheme cluster at point.

## `ned/forward-char`

Move point forward one grapheme cluster.

## `ned/insert`

Insert text at point.

## `ned/list-backups`

Backup snapshots recoverable for the current buffer, as an array of absolute paths -- the crash-recovery autosave first if one exists, then saved versions newest-first. Empty for a pathless buffer or when nothing was backed up. Index into it with ned/recover-backup.

## `ned/message`

Show a status/echo-area message.

## `ned/org-capture-register-template`

Register an org-capture template: (key name target-file template headline), e.g. (ned/org-capture-register-template "t" "Todo" "~/org/todo.org" "* TODO %?\n" ""). key is exactly one character (org-capture, C-c k, reads it to pick this template); template's first "%?" marks where point lands after capture (omit it to land at the end of the inserted text). headline, if non-empty, files the capture as the last child of the exactly-titled headline in target-file; empty files at the end of target-file instead. Re-registering an existing key overwrites it.

## `ned/point`

Return the current point as a byte offset.

## `ned/recover-backup`

Restore the current buffer's content from backup snapshot `index` (0 = the autosave if present, else the newest version -- ned/list-backups' order). One undoable step; the buffer is left modified, so save to keep the recovery. Panics on a bad index or unreadable snapshot.

## `ned/register-command`

Register a Janet function as a named, bindable command.

## `ned/register-language-grammar`

Load a tree-sitter grammar at runtime: (name library-path queries-dir). library-path is a shared library exporting tree_sitter_<name>; queries-dir is a directory scanned for every conventional query-kind basename -- highlights, folds, imports, tags, tests, indents, locals and injections, as either <kind>.janet (ned's own spelling: '#' comments, (:eq? ...) predicates) or <kind>.scm (tree-sitter's, what a system install under /usr/share/tree-sitter/queries/<lang>/ ships) -- whichever aren't present are simply skipped (a grammar with only a highlights file is fine), so a file added to queries-dir later (e.g. by a system package update) takes effect on the next registration with no init.janet change needed. Pass "" for queries-dir to register the grammar for its parser alone. Re-registering the same name replaces it. The registered name can then be used as the mode-name argument to ned/set-mode-for-extension or ned/set-mode-for-filename.

## `ned/register-snippet`

Register a snippet: (language-key trigger body), e.g. (ned/register-snippet "cpp" "for" "for (int ${1:i} = 0; $1 < ${2:n}; ++$1) {\n    $0\n}"). Typing the trigger word then TAB (or M-x expand-snippet in modes whose keymap claims TAB) expands the body: ${n:placeholder}/$n are tabstop fields TAB/S-TAB hop between (a repeated index mirrors typing live), $0 is where point lands at the end, \$ escapes a literal dollar. language-key matches ned/set-lsp-command's ("cpp", "python", ...); "" registers for every mode. An empty body clears the trigger; re-registering overwrites it.

## `ned/register-test-parser`

Register a Janet function as the parser for a test output format: (name fn). fn receives the run's raw combined output (or the results file's contents, see ned/set-test-results-file) as one string and returns an array of result tables {:name "..." :status :passed|:failed|:skipped :file "..." :line n :message "..."} -- only :name and :status are required -- or a table {:results [...] :failures-only true :passed n} when the format only names failures. The name is then usable as ned/set-test-command's format; registering a built-in format's name deliberately overrides it. A nil fn clears the registration.

## `ned/set-acp-agent`

Set the command used to launch an Agent Client Protocol (ACP) coding agent: (name argv), e.g. (ned/set-acp-agent "claude-code" ["claude-code-acp"]). Same argv shape and $PATH resolution as ned/set-lsp-command; an empty argv clears the configured command for name. acp-send-prompt (C-c a p) is the entry point that spawns and talks to whichever agent name it's given.

## `ned/set-acp-mcp-bridge`

Enable/disable advertising ned's own MCP tool-server bridge (get_diagnostics/hover/goto_definition/find_references/git_status/git_diff/search_project/run_tests/get_test_results) to an ACP agent on session start (default true). Off falls back to sending an empty mcpServers list, as if no bridge were wired at all.

## `ned/set-acp-panel-dock`

Dock the ACP chat panel at the "bottom" (default) or "right" edge. Any other value is ignored. Takes effect on the next resize or panel show.

## `ned/set-acp-panel-size-percent`

Set how much of the screen the ACP chat panel covers, as a percentage (default 30, clamped to 15-70) -- height when docked at the bottom, width when docked at the right.

## `ned/set-async-load-threshold`

File size in bytes above which files load asynchronously in the background instead of blocking (default 16 MiB, i.e. (* 16 1024 1024)). 0 loads every file asynchronously.

## `ned/set-auto-detect-project-root`

Enable/disable walking upward from an opened file for a VCS marker directory to find the project root (default true).

## `ned/set-auto-merge`

Enable/disable automatically three-way merging a buffer's local edits with a file that also changed on disk (default true). A clean merge applies silently; a genuine conflict inserts <<<<<<< markers instead of guessing. Always one undoable step. A separate toggle from ned/set-auto-revert.

## `ned/set-auto-pair-enabled`

Enable/disable auto-closing matching brackets/quotes as you type -- typing an opener inserts its matching closer, typing a redundant closer skips over it, backspace between an empty pair removes both (default true). Which characters pair is per-mode (Mode's own autoPairs); this only turns the whole feature on or off.

## `ned/set-auto-revert`

Enable/disable automatically reloading an open, unmodified buffer when its file changes on disk (default true). A buffer with local edits is never auto-reverted; saving it instead asks before overwriting.

## `ned/set-backup-max-age-days`

Days a backup version is kept before pruning (default 14; <= 0 keeps versions regardless of age).

## `ned/set-backup-max-size-mb`

Buffers past this size, in MiB, are skipped by the periodic crash-recovery autosave writer (default 64; non-positive values are clamped to 1). Deliberately conservative -- this write runs on a 5-second timer.

## `ned/set-backup-max-versions`

Backup versions kept per file, oldest pruned first (default 20; <= 0 keeps unlimited versions).

## `ned/set-backup-version-max-size-mb`

Files past this size, in MiB, are skipped by the pre-save version-backup copy (default 65536, 64 GiB; non-positive values are clamped to 1). Far more generous than set-backup-max-size-mb since this is a one-time disk copy done on save, not a periodic in-editor write.

## `ned/set-capture-background`

Override one capture name's background color as "#rrggbb" -- empty string clears; inherits like ned/set-capture-foreground.

## `ned/set-capture-bold`

Override one capture name's bold trait (true/false) -- nil clears.

## `ned/set-capture-class`

Remap a capture name to a syntax class (e.g. (ned/set-capture-class "tag.error" "control-keyword")) -- the capture then inherits that class's whole built-in style. Applies at every dotted level, so remapping "keyword" also re-bases unlisted specific names that fall back to it. Empty class name restores the built-in mapping.

## `ned/set-capture-foreground`

Override one tree-sitter capture name's foreground color as "#rrggbb" (e.g. "function.builtin", no leading @) -- empty string clears. More specific dotted names inherit from less specific ones ("function.builtin.static" falls back through "function.builtin" to "function"), then from the capture's syntax class (ned/set-syntax-*). See ned/capture-names for every known name.

## `ned/set-capture-italic`

Override one capture name's italic trait (true/false) -- nil clears.

## `ned/set-capture-strikethrough`

Override one capture name's strikethrough trait (true/false) -- nil clears.

## `ned/set-capture-underlined`

Override one capture name's underlined trait (true/false) -- nil clears.

## `ned/set-class-file-sync`

Enable/disable offering, unprompted, to keep a file's name and the single type declared inside it in agreement (default true) -- after renaming a class/enum/struct/record, a y/n to rename the file after it; after renaming the file, a y/n to rename the type. Both fire only when the file was demonstrably named after that type a moment ago and no longer is, and never when the file holds more than one top-level type -- whether a file *should* be named after its type is a per-project question this does not try to answer. Turning it off stops the offers only: rename-file-to-match-type and rename-type-to-match-file keep working when you ask for them, and are more permissive than the offers are.

## `ned/set-clean-blank-line-on-newline`

Enable/disable clearing a whitespace-only line's own leading run before splitting it when the newline command is invoked with point on one (default true) -- a second Enter on a line that only ever got auto-indented and never actually typed into removes that dangling whitespace instead of leaving it behind, while the new line's own indent is still computed fresh.

## `ned/set-clipboard-copy-command`

Set the command kill-line/kill-region/kill-ring-save/kill-word/yank pipe killed text into to reach the system clipboard: (argv), e.g. (ned/set-clipboard-copy-command ["wl-copy"]). With no override configured, ned auto-detects wl-copy (Wayland), xclip/xsel (X11), pbcopy (macOS), or clip.exe (WSL) on $PATH, in that order -- an empty argv clears an explicit override and reverts to that auto-detection. ned also always writes an OSC 52 escape sequence directly to the terminal regardless of this setting, since it's the only thing that reaches a *local* clipboard over an SSH session with no tool installed on the remote host; use ned/set-clipboard-enabled false to turn off both mechanisms at once.

## `ned/set-clipboard-enabled`

Enable or disable system-clipboard integration as a whole (default true) -- both the shelled-out CLI tool and the OSC 52 write/read paths.

## `ned/set-clipboard-paste-command`

Set the command yank reads the system clipboard from when it differs from the kill ring's own most recent entry: (argv), e.g. (ned/set-clipboard-paste-command ["wl-paste" "-n"]). Same auto-detection/empty-clears convention as ned/set-clipboard-copy-command, resolved independently of it. There is no OSC 52 read-back fallback for paste -- see Editor/Clipboard.h's own comment for why.

## `ned/set-code-folding-enabled`

Enable/disable the gutter code-folding affordance for modes with a fold query (default true).

## `ned/set-coverage-file`

Set the path load-coverage-report (C-c T c) reads: an lcov .info file, the common export target for lcov itself, `llvm-cov export -format=lcov`, and `gcovr --lcov` -- (ned/set-coverage-file "coverage.info"). Parses into the per-line covered/uncovered/partial-branch gutter marks, and (when a VCS diff is available) flags an uncovered line that's also newly added/modified with its own "untested new code" mark. An empty string clears the configured path.

## `ned/set-dap-adapter`

Set the command used to launch a language's DAP debug adapter: (language argv), e.g. (ned/set-dap-adapter "cpp" ["lldb-dap"]) or (ned/set-dap-adapter "python" ["python" "-m" "debugpy.adapter"]). Same argv shape and $PATH resolution as ned/set-lsp-command; an empty argv clears it.

## `ned/set-dap-attach`

Set the DAP attach configuration for a language: (language json), ned/set-dap-launch's own shape but for the dap-attach command's `attach` request instead of `launch` -- see your adapter's documentation for its attach-specific keys (commonly a process id or a connection host/port). An empty string clears it. Both this and ned/set-dap-adapter must be configured before dap-attach can start a session.

## `ned/set-dap-launch`

Set the DAP launch configuration for a language: (language json), e.g. (ned/set-dap-launch "cpp" `{"program": "./build/ned"}`). The string is passed verbatim as the launch request's own adapter-specific arguments object -- see your adapter's documentation for its keys. An empty string clears it. Both this and ned/set-dap-adapter must be configured before dap-continue (F5) can start a session.

## `ned/set-diff-refresh-debounce-ms`

Set how long, in milliseconds, the VCS diff gutter waits after the last edit before refreshing (default 1200; non-positive values are clamped to 1).

## `ned/set-ensure-final-newline`

Enable/disable appending a trailing newline to a file's written content on save if it's missing one (default true).

## `ned/set-file-auto-save`

Enable/disable periodic crash-recovery snapshots of modified file buffers into the backup store (default true). Snapshots never touch the file itself and are dropped by a real save.

## `ned/set-file-watch`

Enable/disable the inotify file watcher that triggers auto-revert/auto-merge near-instantly when an open buffer's file changes on disk (default true). The periodic 5s sweep keeps running either way (the safety net for filesystems inotify can't see, e.g. NFS); disabling this just falls back to that sweep alone. A flip takes effect at the next sweep tick (within ~5s).

## `ned/set-fill-column`

Set the target line width (in codepoints) fill-paragraph (M-q) wraps prose/comments to (default 70).

## `ned/set-follow-symlinks-on-save`

Whether saving a file reached through a symlink writes the file the link points at (default true) or replaces the link itself with a regular file (false). Following also means the temporary file a save writes is created beside the real target, so a link pointing to another filesystem still saves.

## `ned/set-format-command`

Set the shell command save-buffer pipes buffer content through before writing (empty string clears it).

## `ned/set-huge-file-disk-space-check-enabled`

Enable/disable the free-disk-space safety check for huge (piece-table-backed) buffers entirely (default true). Off skips both the open-time read-only downgrade and the save-time refusal.

## `ned/set-huge-file-min-free-space-multiplier`

Safety margin (default 2.0) a huge file's save must clear: available free disk space must be at least this many times the content's byte length, since the atomic save pattern needs the new content's full size in free space and a copy-on-write filesystem (Btrfs, ZFS) can transiently need close to double that. Below this, a newly opened huge buffer downgrades to read-only (toggle-read-only overrides); a save attempt on a huge buffer always refuses regardless of that override -- see set-huge-file-disk-space-check-enabled to turn the check off entirely instead.

## `ned/set-huge-file-threshold`

File size in bytes above which a file opens via the piece-table storage engine -- never fully read into memory, edits/undo/save all work, but LSP sync and whole-buffer regex search stay limited for now (default 1 GiB, i.e. (* 1024 1024 1024)). Checked ahead of set-async-load-threshold, so a file clearing both always takes this path. 0 routes every file through it. Does not yet support CRLF/CR line endings (Buffer::FromHugeFile throws) -- such a file still opens fine via the normal loader.

## `ned/set-huge-structural-window-bytes`

For a huge (piece-table-backed) buffer only, the byte margin the code-folding/symbol-kind/test-discovery gutters expand the visible viewport by on each side when deciding how much of the buffer to parse (default 4 MiB) -- a fold region, symbol definition, or test whose start/end falls outside that window won't show until scrolling brings it closer. No effect on an ordinary (non-huge) buffer, which is never windowed.

## `ned/set-import-fixup`

Enable/disable rewriting imports when a file is renamed or moved (default true) -- both the imports in other files that named it and the relative imports the moved file wrote itself. Resolved with the same tree-sitter import queries go-to-file-at-point uses, and handed to the same editable review multibuffer a rename is (C-c C-c to commit, M-r to drop an excerpt), so nothing lands unseen. A language server that answered workspace/willRenameFiles with edits of its own wins outright -- this is the no-server path. An import whose own style cannot express the new location (an angle-form include, a PHP namespace, a Rust mod declaration, a target that left the root its specifier counts from) is reported as declined rather than guessed at.

## `ned/set-import-fixup-max-files`

How many project files one rename's import scan will consider (default 20000; 0 means unlimited). Only files whose language has an import query are counted at all. Raise it for a very large repository, lower it if a rename feels slow.

## `ned/set-include-path-cache-ttl-seconds`

Set how long (in seconds) a compiler-derived default include-path result stays cached before open-link-at-point/LSP resolution re-probes the real toolchain (default 86400, i.e. 24h). 0 or negative disables caching outright -- every lookup re-probes. See also refresh-toolchain-include-paths for a manual, immediate cache clear.

## `ned/set-indent-guide-depth-colors-enabled`

Enable/disable cycling each indent guide's color by its own nesting level (Theme's indent-guide-depth-palette) instead of one flat color. Only visible when indent guides themselves are on. Default true.

## `ned/set-indent-guides-enabled`

Enable/disable vertical indentation guide glyphs at each indent-width column within a line's own leading whitespace. Default false, same opt-in reasoning as set-trailing-whitespace-highlight-enabled.

## `ned/set-indent-style`

Set the indent style smart-indentation (indent-for-tab-command/newline/indent-region/indent-buffer) writes: (mode-name-or-empty use-tabs? width). An empty mode-name sets the process-wide default (spaces, width 4); a Mode name (e.g. "python-mode") sets a per-mode override, checked first.

## `ned/set-inline-diagnostic-style`

How an inline diagnostic is drawn: "end-of-line" (default) puts the message after the line's own text, on a row the line already occupies, so a diagnostic appearing or clearing never shifts anything else on screen; "callout" is the original block below the line with carets under the flagged span, which points at exact columns but costs a screen row that comes and goes as you type.

## `ned/set-inline-diagnostics`

Enable/disable inline diagnostics (the LSP message shown against the line it flags; default true).

## `ned/set-line-ending-policy`

"preserve" (default) keeps each buffer's own detected/converted line ending on save; "lf"/"crlf"/"cr" force every save to that ending regardless of what was detected. Per-buffer convert-line-endings-to-lf/-crlf/-cr override a single buffer's own ending independent of this process-wide policy.

## `ned/set-log-category-visible`

Show/hide one category ("general"/"janet"/"lsp"/"dap"/"acp"/"vcs"/"task"/"subprocess") in the *Messages* buffer -- "lsp" defaults hidden, everything else visible.

## `ned/set-log-max-entries`

Set how many recent *Messages* entries are kept in memory (default 5000) -- infinity doesn't exist in RAM.

## `ned/set-lsp-auto-complete`

Enable or disable the automatic LSP completion popup while typing (default true). Manual completion (lsp-complete, bound to C-M-i) works regardless of this setting.

## `ned/set-lsp-code-lens`

Enable or disable code lens annotations (textDocument/codeLens), rendered as a dim line above the code they annotate (default true). Run the lens at point with lsp-run-code-lens-at-point (M-x, unbound by default). A server that proves it doesn't support the method is never asked again for that connection's lifetime.

## `ned/set-lsp-command`

Set the command used to launch a language's LSP server: (language argv), e.g. (ned/set-lsp-command "c" ["clangd"]). argv is an array or tuple of strings -- argv[0] the executable (resolved against $PATH), the rest its arguments. ned never installs or updates a language server itself; this only configures which already-installed one to run. An empty argv clears the configured command for language.

## `ned/set-lsp-commit-characters`

Enable or disable accepting the selected completion when a character the server declared as one of that item's commitCharacters is typed (default true; the character itself is still inserted afterwards). Inert against a server that declares none -- no default set is ever assumed. Turn it off if typing ';' or ',' to end a statement keeps accepting the suggestion that happened to be showing.

## `ned/set-lsp-completion-debounce`

Set the delay, in milliseconds, after the last relevant keystroke before an automatic completion request is sent (default 500). Non-positive values are clamped to 1.

## `ned/set-lsp-diagnostics-debounce`

Set the delay, in milliseconds, after the LSP server's most recently received diagnostics publish for a buffer before it's actually applied (default 500) -- keeps inline diagnostics from repainting on nearly every keystroke while typing, settling in only once the server goes quiet for this long. Non-positive values are clamped to 1.

## `ned/set-lsp-format-on-save`

Enable or disable formatting the buffer via the language server on save (default false). Ignored whenever ned/set-format-command has an external formatter configured -- that always takes precedence.

## `ned/set-lsp-hover-on-mouse-move`

Enable or disable showing an lsp-hover tooltip when the mouse rests over a symbol (default true). Disabling this skips the debounce/request entirely, not just the popup. Manual invocation (lsp-hover, C-c C-j) works regardless of this setting.

## `ned/set-lsp-inlay-hints`

Enable or disable inline parameter-name/type hints (textDocument/inlayHint), rendered as dim virtual text the language server supplies (default true). A server that proves it doesn't support the method is never asked again for that connection's lifetime.

## `ned/set-lsp-on-type-formatting`

Enable or disable automatically formatting via the language server after typing one of its declared trigger characters (e.g. a closing brace or newline; default false). A server that doesn't advertise documentOnTypeFormattingProvider never triggers regardless of this setting.

## `ned/set-lsp-pull-diagnostics`

Enable or disable requesting diagnostics via textDocument/diagnostic on every content sync (default false). Only useful for a server that never sends its own publishDiagnostics notifications -- a server that proves it doesn't support pull either is never asked again for that connection's lifetime.

## `ned/set-lsp-root-markers`

Override the root-marker filenames ned looks for when resolving which directory to initialize a language's LSP server against: (language markers), e.g. (ned/set-lsp-root-markers "rust" ["Cargo.toml"]). Walks upward from an opened buffer's own directory for the nearest ancestor containing one of these as an immediate child; falls back to editor::ProjectRoot() when none match (or markers is empty and language has no compiled-in default) -- this is what lets a monorepo subpackage (its own package.json/pyproject.toml/Cargo.toml/compile_commands.json, ...) get its own LSP root distinct from the outer repo's single .git. An empty markers list clears the override, reverting to the compiled-in default (bundled for c/cpp/python/javascript/typescript/tsx/php) rather than to no markers at all.

## `ned/set-lsp-semantic-highlighting`

Enable or disable server-informed syntax highlighting (textDocument/semanticTokens/full), layered on top of tree-sitter's own highlighting rather than replacing it (default true). A server with no semanticTokensProvider legend never sends a request regardless of this setting.

## `ned/set-lsp-signature-help-auto-trigger`

Enable or disable automatically requesting signature help after typing ( or , inside a call (default true). Manual invocation (lsp-signature-help) works regardless of this setting.

## `ned/set-lsp-sync-background-buffers`

Enable/disable syncing every open buffer to its configured LSP server(s), not just the pane-active one (default true). Runs on the same periodic background tick as auto-save, not per-frame, so a background tab's diagnostics/completions stay current without interrupting the buffer you're actually editing.

## `ned/set-lsp-sync-debounce`

Set the delay, in milliseconds, after an edit before ned actually sends textDocument/didChange to a buffer's LSP servers (default 150) -- prevents a full-document sync on every single keystroke, which can block the UI if a server can't drain its input fast enough. Keep this shorter than set-lsp-completion-debounce (default 500) or completion/hover/etc. requests may race ahead of a server that doesn't have the latest content yet. Non-positive values are clamped to 1.

## `ned/set-lsp-workspace-folders`

Enable or disable letting a buffer whose LSP root differs from an already-running same-language server join that server as an extra workspace folder instead of spawning its own process (default true) -- one server for a whole monorepo rather than one per subpackage. A server that doesn't advertise workspaceFolders support is never asked, and falls back to a separate process per root. Turn this off when isolation matters more than footprint: joined roots share one process, so one crash takes them all down together.

## `ned/set-max-highlight-bytes`

Buffer size in bytes above which syntax highlighting is skipped entirely (default 8 MiB). 0 disables highlighting for every buffer.

## `ned/set-minimap-chars-per-dot`

Set how many real buffer columns one minimap dot represents (default 8, applies uniformly in both glyph and real-pixel rendering). Fractional values (e.g. 8.5) are accepted for finer-grained tuning. A line longer than minimap-width * chars-per-dot * 2 columns simply isn't rendered past that point -- not compressed.

## `ned/set-minimap-enabled`

Enable/disable the minimap (replaces the plain scrollbar) as the default starting state for newly-opened panes (default true). See also toggle-minimap for flipping it at runtime.

## `ned/set-minimap-width`

Set the minimap's width in columns (default 5). Each column packs 2 braille sub-columns of resolution.

## `ned/set-mode-for-extension`

Map a file extension (with or without a leading '.') to a mode name -- either one registered via ned/register-language-grammar, or one of ned's own built-in mode names (e.g. "php-mode", "python-mode"). Checked before ned's own built-in extension table, so this can override a bundled mapping too, not just add a new one.

## `ned/set-mode-for-filename`

Map an exact, full filename (e.g. "CMakeLists.txt", not a pattern/glob) to a mode name, the same way ned/set-mode-for-extension does for an extension -- checked first, before any extension mapping, for files identified by name rather than by a distinguishing extension.

## `ned/set-multibuffer-auto-collapse-byte-threshold`

Same as set-multibuffer-auto-collapse-line-threshold, but measured in bytes (default 2000) -- catches a single huge/minified line a line-count threshold alone would miss.

## `ned/set-multibuffer-auto-collapse-excerpt-cap`

Once a multibuffer's own excerpt count passes this (default 100), every remaining excerpt collapses by default regardless of its own size -- catches a plain-large result set (e.g. project-find-references on a very common identifier) rather than dumping hundreds of expanded excerpts into view at once.

## `ned/set-multibuffer-auto-collapse-line-threshold`

An excerpt (diff hunk, reference, ...) in a *vcs diff*/*vcs commit*/*references*/*diagnostics*/... multibuffer whose own body has more lines than this collapses by default when the multibuffer is built (default 40) -- code-fold-toggle/unfold-all still work on it from there like any other fold.

## `ned/set-multibuffer-max-excerpts`

Hard cap on how many excerpts a multibuffer stitches at all (default 500; 0 = unlimited) -- unlike set-multibuffer-auto-collapse-excerpt-cap, which only collapses excerpts that were all still built, this stops the work. Anything past the cap is dropped and named in a trailing "N more not shown" line of the buffer itself; project-find-references also stops reading source lines off disk at this point.

## `ned/set-multibuffer-scoped-search`

Enable/disable confining isearch and query-replace to a multibuffer's excerpt bodies (default true) -- header paths, rule lines and the blanks between excerpts stop matching, and query-replace stops offering a replacement inside chrome the buffer would then silently refuse. Turn it off to search a review buffer's whole composite text, e.g. to find the excerpt whose header names a particular file. No effect on an ordinary buffer.

## `ned/set-page-scroll-fraction`

Set the fraction of the viewport height a page up/down moves (default 0.65, clamped to (0, 1]).

## `ned/set-persistent-undo`

Enable/disable persisting each file buffer's full undo tree to $XDG_STATE_HOME/ned/undo/ across editor restarts (default true). On reopen, restored only if the file's current content still matches some node in the persisted tree (not necessarily its tip -- e.g. quitting without saving); otherwise the persisted history is discarded and the buffer starts fresh, same as a normal first-time open. No merging.

## `ned/set-persistent-undo-max-size-mb`

Buffers past this content size, in MiB, are skipped by persistent-undo saving (default 16; non-positive values are clamped to 1). A large file's undo tree multiplies this cutoff by however many undo steps it has, unlike a single backup version.

## `ned/set-preserve-hard-links-on-save`

Whether saving a file that has more than one hard link keeps every link pointing at the same content (default true). Doing so requires rewriting the file in place instead of the usual write-a-temp-file-then-rename, which means a crash mid-save can leave that file truncated -- recoverable from a backup version. False keeps the atomic save and lets the save break the link, leaving the other names on the old content.

## `ned/set-project-open-command`

Set the command switch-project/open-project run to open another project in a new tab/window when no built-in terminal/multiplexer is auto-detected (or to override auto-detection with your own preferred invocation): (argv), e.g. (ned/set-project-open-command ["tmux" "new-window" "-c" "{root}" "ned" "{root}"]). argv is an array or tuple of strings; every "{root}" occurrence in every element is replaced with the project's own path -- never through a shell. An empty argv clears it. Auto-detection already covers tmux, GNU screen, Konsole, GNOME Terminal, WezTerm, Ghostty, and kitty -- this is for anything else, or a different invocation than the built-in one (e.g. a tmux pane split instead of a new window). Two of the auto-detected terminals need a one-time setting change of their own before they'll actually run anything (opening the tab still works either way, but the command inside it won't launch until this is done): Konsole requires `EnableSecuritySensitiveDBusAPI=true` under the `[KonsoleWindow]` section of konsolerc, plus restarting Konsole; kitty requires `allow_remote_control` (and usually `listen_on`) set in kitty.conf. Neither is ever changed by ned itself -- both hand a running terminal instance the ability to type arbitrary commands into it via IPC, a real security-relevant choice that's the user's own to make.

## `ned/set-project-search-threads`

Set the worker-thread cap for project-wide search/replace's internal file scan (default 4).

## `ned/set-project-trust-expiry-days`

Days of disuse before an "always"-trusted project init.janet must be re-approved (default 30; 0 or negative = never expire). Trust decays from last use, not from when it was granted; a changed init file always re-prompts regardless.

## `ned/set-prose-checker-command`

Set the command used to launch the prose/spell/grammar checker: (argv), e.g. (ned/set-prose-checker-command ["ltex-ls"]) to use something other than the default. Same argv shape as ned/set-lsp-command. With no override configured, ned auto-wires harper-ls if it's found on $PATH -- an empty argv clears an explicit override and reverts to that auto-detection rather than disabling the checker; use ned/set-prose-checker-enabled false to actually turn it off.

## `ned/set-prose-checker-enabled`

Enable or disable prose/spell/grammar checking as a whole (default true). Diagnostics from it merge alongside the buffer's primary language server's own diagnostics rather than replacing them.

## `ned/set-protocol-read-stall-timeout-ms`

Set how long, in milliseconds, silence after an LSP/DAP/ACP frame or message has started arriving is tolerated before the connection is treated as stalled and disconnected -- idle time between messages stays unbounded regardless (default 30000; non-positive values are clamped to 1). See also set-protocol-write-stall-timeout-ms for the write-side twin.

## `ned/set-protocol-request-timeout-ms`

Set how long, in milliseconds, a sent LSP/DAP/ACP request is kept pending before it's resolved with a synthetic timeout failure (default 30000; non-positive values are clamped to 1).

## `ned/set-protocol-write-stall-timeout-ms`

Set how long, in milliseconds, a write of an LSP/DAP/ACP frame or message to a server's stdin waits for it to keep draining before the connection is treated as stalled and disconnected (default 30000; non-positive values are clamped to 1). See also set-protocol-read-stall-timeout-ms for the read-side twin.

## `ned/set-recency-glow`

Enable/disable the recency glow -- a brief accent wash over text that was just edited, fading out over ~200ms (default true). Tints the edited characters rather than washing the line behind them, what keeps it cheap -- see Editor/RecencyGlow.h. Retune its colour with (ned/theme-surface "buffer.recency" "fill" ...).

## `ned/set-relative-line-numbers`

Enable/disable relative line numbers in the gutter (default false): the current line keeps its real number, every other visible line shows its distance from it, Vim's 'relativenumber' convention.

## `ned/set-rename-review`

Enable/disable handing a rename's edits to an editable review multibuffer before they land (default true) -- one excerpt per occurrence, M-n/M-p to step, M-a to include an excerpt, M-r to exclude it, C-c C-c to commit the lot as one undo transaction. The review is also the only place the comment and string occurrences a rename deliberately skipped are visible, each excluded until opted into. Turn it off to apply a rename immediately, as rename-symbol and lsp-rename did before. A rename that also creates, deletes or renames files is applied directly either way -- a multibuffer cannot represent that.

## `ned/set-repl-command`

Set the command run-repl spawns (on a real pty, its own interactive CLI REPL shown as-is) for a REPL name: (name argv), e.g. (ned/set-repl-command "python" ["python3" "-i"]) or (ned/set-repl-command "php" ["php" "-a"]). Same argv shape as ned/set-task-command; an empty argv clears the configured command for name. The built-in Janet REPL (toggle-janet-repl, C-c j) needs no configuration -- it evaluates in-process against the running editor's own environment, not a subprocess.

## `ned/set-save-place`

Enable/disable remembering each file's last point and scroll position across editor runs (default true). Off disables both restoring and recording.

## `ned/set-scratch-auto-save`

Enable/disable automatically saving modified scratch notes (find-scratch) on a periodic timer (default true).

## `ned/set-session-restore`

Enable/disable per-project session persistence -- open buffers, active file, sidebar state, and DAP breakpoints, restored when ned starts inside a project (default true). Off disables both restoring and saving; --no-restore does the same for a single launch.

## `ned/set-sticky-scroll-enabled`

Enable/disable pinned namespace/class/method breadcrumb rows at the top of a pane while scrolled into their body (default true). Only meaningful for a mode with a tags query (symbolKind) -- see set-sticky-scroll-max-rows for capping how many rows it can reserve.

## `ned/set-sticky-scroll-max-rows`

How many pinned breadcrumb rows a pane will ever reserve, regardless of how deep the actual enclosing namespace/class/method chain is (default 3). 0 effectively disables the rows without touching set-sticky-scroll-enabled.

## `ned/set-subprocess-read-timeout-ms`

Set how long, in milliseconds, a main-thread blocking subprocess read (system-clipboard paste, first toolchain-include-path query for a language) waits before killing the child and failing gracefully (default 5000; non-positive values are clamped to 1).

## `ned/set-subprocess-write-timeout-ms`

Set how long, in milliseconds, a blocking write to a subprocess's stdin (system-clipboard copy, an LSP/ACP frame, a terminal keystroke) waits for the child to keep draining before giving up (default 5000; non-positive values are clamped to 1).

## `ned/set-syntax-background`

Override a syntax class's background color as "#rrggbb" -- empty string clears the override.

## `ned/set-syntax-bold`

Override a syntax class's bold trait (true/false) -- nil clears the override.

## `ned/set-syntax-foreground`

Override a syntax class's foreground color (e.g. "comment") as "#rrggbb" -- empty string clears the override. See ned/syntax-classes for every valid class name.

## `ned/set-syntax-italic`

Override a syntax class's italic trait (true/false) -- nil clears the override.

## `ned/set-syntax-strikethrough`

Override a syntax class's strikethrough trait (true/false) -- nil clears the override.

## `ned/set-syntax-underlined`

Override a syntax class's underlined trait (true/false) -- nil clears the override.

## `ned/set-tab-width`

Set the display width (in columns) a tab character expands to (default 4).

## `ned/set-task-command`

Set the command run by run-task for a task name: (name argv), e.g. (ned/set-task-command "build" ["cmake" "--build" "."]). argv is an array or tuple of strings -- argv[0] the executable (resolved against $PATH), the rest its arguments. An empty argv clears the configured command for name.

## `ned/set-terminal-height-percent`

Set how much of the screen the terminal drawer covers, as a percentage (default 40, clamped to 10-90).

## `ned/set-test-command`

Set the project's test command and output format: (argv format), e.g. (ned/set-test-command ["ctest" "--test-dir" "build"] "ctest"). argv is an array or tuple of strings -- argv[0] the executable (resolved against $PATH). format names a built-in parser ("ctest", "catch2", "pytest", "go-json", "cargo", "junit-xml", "phpunit") or one registered via ned/register-test-parser (a registered name wins over a built-in). run-tests (C-c T t) streams raw output into *test output* and, on exit, parses it into the *test results* buffer and the per-test gutter marks. An empty argv clears the configured command.

## `ned/set-test-filter-command`

Set the argv template run-test-at-point and rerun-failed-tests use to run a single test: (argv-template), where each element may contain {test} and/or {file} placeholders substituted per element (never through a shell), e.g. (ned/set-test-filter-command ["ctest" "--test-dir" "build" "-R" "^{test}$"]) or (ned/set-test-filter-command ["pytest" "-v" "-k" "{test}"]). Output parses with the same format ned/set-test-command configured. An empty argv clears it.

## `ned/set-test-results-file`

Parse this file's contents after a test run exits instead of the run's own stdout/stderr -- for formats written to a file, e.g. JUnit XML: (ned/set-test-results-file "/tmp/results.xml") paired with (ned/set-test-command ["pytest" "--junitxml" "/tmp/results.xml"] "junit-xml"). An empty string clears it.

## `ned/set-theme`

Select the startup theme by name (e.g. "dark", "light", "gruvbox-dark"). Beats the desktop probe; an unknown name is reported at startup and falls back. Empty string clears the preference.

## `ned/set-trailing-whitespace-highlight-enabled`

Enable/disable a subtle background highlight on trailing whitespace (spaces/tabs after the last non-whitespace character on a line). Default false, matching Emacs' own opt-in show-trailing-whitespace precedent rather than VSCode/Sublime's forced-on default.

## `ned/set-trim-trailing-whitespace-on-save`

Enable/disable stripping trailing spaces/tabs from every line and collapsing trailing blank lines at end-of-file, applied to a file's written content on save (default true). Disk-only, same as set-ensure-final-newline -- the buffer's own live content is never touched.

## `ned/set-url-open-command`

Set the command open-link-at-point launches (as its own argument, never a shell string) to open a URL -- defaults to "xdg-open"; empty string clears it entirely, disabling URL-following.

## `ned/set-vim-mode`

Enable or disable Vim-style modal editing (Normal/Insert/Visual/Replace/command-line, default false). Insert mode still runs through ned's own Emacs-bound keymap underneath (self-insert-command, auto-pair, snippets, LSP completion all keep working) -- only Normal/Visual/Replace/command-line dispatch is Vim's own.

## `ned/set-which-key-enabled`

Enable/disable the which-key popup listing possible next chords while a prefix key (C-x, C-c, ...) is pending (default true). The echo area's own "C-x-" pending-sequence text is unaffected either way.

## `ned/set-wrap-for-extension`

Map a file extension (with or without a leading '.') to whether BufferView should soft-wrap long lines at word boundaries instead of scrolling horizontally, overriding whichever Mode::wrapLines default would otherwise apply (e.g. (ned/set-wrap-for-extension "md" false) to opt markdown-mode's own wrap-on default back out).

## `ned/set-wrap-for-filename`

Map an exact, full filename to a wrap-lines override, the same way ned/set-wrap-for-extension does for an extension -- checked first, before any extension mapping.

## `ned/snippet-triggers`

Return the snippet trigger words visible to a language key -- its own registrations merged with the ""-global tier, sorted. (ned/snippet-triggers "cpp")

## `ned/syntax-background`

The syntax class's overridden background color, or nil if unset.

## `ned/syntax-bold`

The syntax class's overridden bold trait, or nil if unset.

## `ned/syntax-classes`

Every valid syntax class name, sorted.

## `ned/syntax-foreground`

The syntax class's overridden foreground color, or nil if unset.

## `ned/syntax-italic`

The syntax class's overridden italic trait, or nil if unset.

## `ned/syntax-strikethrough`

The syntax class's overridden strikethrough trait, or nil if unset.

## `ned/syntax-underlined`

The syntax class's overridden underlined trait, or nil if unset.

## `ned/theme-gradient`

Register a named paint usable anywhere a paint is (e.g. (ned/theme-gradient "brand" "diag $accent 2 $keyword")). The spec is an optional axis or pattern keyword, then stops, with numbers between them as relative weights: stops are "#rrggbb"/"#rrggbbaa"/"default", a percentage like "60%" (making it a fade of whatever colour is already there), or "$slot" with optional +lighten/-darken//alpha adjustments. A stop naming another paint expands to that paint's own stops.

## `ned/theme-set`

Override one theme color or Brush trait by key (e.g. (ned/theme-set "keyword_foreground" "#f042d6") or (ned/theme-set "active_tab_bold" "false")) on top of the startup theme -- keys match the theme file's own, trait values are "true"/"false"; Docs/Themes.md lists every key for hand-editing, loaded via (dofile ...) from init.janet.

## `ned/theme-surface`

Set one part of one themed surface: (ned/theme-surface "popup" "fill" "y $bg/78 3 $bg/52"). Parts are "fill", "border" and "text"; the spec is the same paint grammar ned/theme-gradient takes. Surface names come from the UI layer ("buffer", "buffer.current_line", "modeline", "tab.active", "panel", "popup", ...); an unknown name or part is reported at startup.

## `ned/vcs-register-provider`

Register a VCS-agnostic plugin: (name callbacks), where callbacks is a struct/table keyed by keyword. :detect (required) takes a root path and returns true if it's a repository this plugin handles. The *-argv callbacks each return an argv array/tuple of strings for the external command to run; the parse-* callbacks each take that command's captured stdout and return an array of tables. Optional keys, by operation: :blame-argv/:parse-blame and :log-argv/:parse-log (entries have :hash :author :date :summary), :diff-argv/:parse-diff (:old-start :old-count :new-start :new-count per hunk), :status-argv/:parse-status (:state :path per changed file, path relative to the root), :stage-argv/:unstage-argv (take the file's path; success is exit code 0, no parse half), :staged-diff-argv (the index-vs-comparison-point diff, for selecting a hunk to unstage), :stage-patch-argv/:unstage-patch-argv (take root and a patch file's path, applying it to the staging area forward/reverse), :commit-argv (takes root and the commit message), :branch-list-argv/:parse-branch-list (:name :current per branch), and :branch-switch-argv/:branch-create-argv (take root and the branch name). An operation whose callbacks are absent reports 'not supported by this provider' when invoked. The actual subprocess is run by ned itself, never by the plugin -- these callbacks only build argv and parse already-captured output. Re-registering name replaces the previous provider.

