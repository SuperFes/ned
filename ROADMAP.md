# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — the detailed
per-feature design/decision records this file used to carry were pruned 2026-08-20 and
again 2026-08-25 (re-accumulated in between); full history lives in git
(`git log --follow ROADMAP.md`, `git show <rev>:ROADMAP.md`, or just `git log` for the
feature's own commit). Current architecture is documented in `CLAUDE.md`. When an item
here ships, replace its entry with a one-line pointer to the shipping commit (or delete
it outright) rather than writing up what was done — the writeup belongs in the commit
message, this file is a todo list, not an archive.

## Vision

An Emacs-class terminal editor: buffers, windows, keymaps, modes, minibuffer,
kill-ring, undo tree, isearch — "everything is a programmable command," with Janet
filling Elisp's role. The whole editor is a Janet-scriptable environment, not a C++
app with a config file bolted on. Modern, memory-safe C++23; TUI rendered with
Notcurses.

## Guiding Constraints

- **Memory safety.** No raw owning pointers — `unique_ptr`/`shared_ptr`,
  `string`/`string_view`, `vector`/`span`. Janet's C heap is external and stays
  malloc'd internally.
- **Programmability first.** New editor capability = a named command reachable from
  keybindings, `M-x`, and Janet uniformly — not hardcoded control flow.
- **XDG Base Directory compliance.** Config → `$XDG_CONFIG_HOME/ned/`, user data →
  `$XDG_DATA_HOME/ned/`, caches → `$XDG_CACHE_HOME/ned/`, other persistent state →
  `$XDG_STATE_HOME/ned/`. Never a bare dot-file in `$HOME`.
- **Keep `Source/UI/` loosely coupled from the TUI library where it's cheap.**

## Open Items

### Embedded Language

- [ ] **Jank replaces Janet** — once possible, replace the internal scripting
      representation with [jank](https://github.com/jank-lang/jank).

### Language Intelligence

- [ ] **Go-to-file-at-point resolver gaps** (`Mode::importTarget`, the hand-rolled
      import/include resolver): LSP should be tried first where a server can answer it
      (clangd's `textDocument/documentLink` for `#include`); Python's leading-dot
      relative imports (`from . import x`) aren't handled; PHP namespace `use` needs a
      PSR-4 autoloader parse; JS dynamic `import(...)` isn't resolved; node_modules
      `package.json` main/exports resolution goes no further than `index.*` inference;
      Rust has no bundled mode yet to resolve at all.
- [ ] **Sync outgoing payload size, remainder** (sync-debounce follow-up: a real,
      gdb-confirmed live freeze — the main thread blocked inside `ChildProcess::
      WriteAll`, stuck writing a full-document `textDocument/didChange` to a server
      whose stdin pipe couldn't drain fast enough, because `SyncBuffer` sent one
      full-document sync per keystroke with no debounce at all — fixed by debouncing
      the actual send, `LspSyncDebounceMs()`, default 150ms, shorter than every other
      LSP debounce so it lands before they fire their own requests) — two follow-ups
      deliberately left out of that fix:
      - **Incremental sync** (`TextDocumentSyncKind.Incremental` — `contentChanges:
        [{range, rangeLength, text}]`, sending only the changed span instead of the
        whole document) is real LSP, gated by whatever `textDocumentSync.change` a
        server advertises during `initialize` (0=none, 1=full, 2=incremental) — this
        client never checks it and always sends the full-document form. Debouncing
        cuts *how often* a sync happens; this would cut *how much* each one sends —
        genuinely complementary, not a substitute. Needs old-vs-new content diffing,
        converting the changed byte range to LSP's UTF-16 `Position` math, respecting
        the server's advertised sync-kind capability, and a full-sync fallback for a
        server that doesn't support incremental.
      - **`ProtocolStallTimeoutMs()`'s 30s default** (`Transport.h`) is shared by both
        the write side and the read side (where a genuinely slow response — a large
        workspace-wide rename, say — legitimately needs tolerance). Superseded as a
        main-thread-freeze concern by the async write queue below (a stalled write no
        longer blocks the main thread at all, regardless of this value), but splitting
        it into separate write/read timeouts would still be a reasonable defense-in-
        depth hardening on top — a healthy server should never take long just to
        *accept* a notification.
- [ ] **Async LSP write queue for DapClient/AcpClient** (async-write-queue follow-up: a
      *second*, distinct gdb-confirmed live freeze — `LspBackgroundSync.cpp`'s periodic
      5s `SyncBackgroundBuffers` tick sent a synchronous `didOpen` for a large,
      never-before-synced background buffer to a slow server, blocking the main thread
      independently of typing speed or bursts, unrelated to the sync-debounce fix
      above — fixed by moving every `LspClient` write off the main thread entirely: a
      dedicated `writeThread_` drains a queue (`EnqueueWrite`), so
      `SendRequest`/`SendNotification`/a server-request response never block the
      caller, only that thread. Two destruction policies distinguish final process
      shutdown (`PrepareForGracefulShutdown` — drains the queue so
      `LspManager::Shutdown()`'s courtesy `shutdown`/`exit` frames still land) from
      ordinary mid-session teardown (`LspManager::ClientDisconnected`'s erase — no
      drain, since blocking the main thread to flush writes to an already-dying
      connection would reintroduce the exact bug this fixes; a residual bound of one
      in-flight write, ≤30s, remains here in the pathological case, no worse than
      today's pre-fix per-call bound). `DapClient.cpp` (1 write site) and
      `AcpClient.cpp` (4 write sites, its own `Transport::WriteMessage`) mirror
      `LspClient`'s threading shape closely enough to transplant the same fix directly,
      but weren't touched — both bugs motivating this fix were LSP-specific, and DAP/
      ACP's single-session/modal nature means a stalled write there blocks only an
      active debug/chat session the user is already looking at, not the whole editor.
      Worth doing for consistency once a similar live freeze is actually reported
      against either.
- [ ] Whether Markdown fenced code blocks / Org `#+BEGIN_SRC` blocks should get the same
      real-LSP-sync treatment HTML `<script>`/`<style>` embedded documents already have
      is an open question — spawning a live language server per code fence in an
      ordinary notes file could be noisy for illustrative/incomplete snippets.
- [ ] **Call hierarchy / type hierarchy** — `textDocument/prepareCallHierarchy` +
      `callHierarchy/incomingCalls`/`outgoingCalls`, and the analogous
      `prepareTypeHierarchy`/`typeHierarchy/supertypes`/`subtypes`, are the two LSP
      methods this client has never sent. Unlike every other LSP result view in this
      codebase (a flat list: references, symbols, diagnostics), a hierarchy is
      genuinely tree-shaped and expands on demand (each node's children are a fresh
      request) — there's no existing widget to render that. Worth building a generic
      expandable tree/list widget in `Source/UI/` for this rather than a one-off, since
      nothing here currently renders hierarchical data and a future consumer (a VCS
      log graph, a project outline pane) could reuse it instead of growing its own.
- [ ] **`semanticTokens/range` and delta requests** — the semantic-highlighting client
      only ever sends the full-document `semanticTokens/full` request; no huge-buffer
      windowing or incremental delta support yet.
- [ ] **LSP edit-application gaps, remainder** (2026-08-25 audit; project-undo follow-up
      closed the multi-file-code-action half of this on 2026-09-01 — `ApplyCodeAction`
      now applies a cross-file edit the same way rename does, both routed through
      `BufferView::ApplyProjectEdit` and `Editor/ProjectUndo.h`'s project-wide undo/redo
      transaction) — still open: rename and code actions alike only apply the
      `changes`-map `WorkspaceEdit` form and silently refuse a `documentChanges`-only
      response (`LspContent.cpp`'s `touchesUnsupportedForm`) — no file create/rename/
      delete resource ops; and a code action that triggers a server-side
      `workspace/applyEdit` push (rather than `workspace/executeCommand`) has no client
      handler at all — nothing ned wires up needs this yet, but it'll break silently the
      day something does.
- [ ] `rename-project-path` never tells an open LSP server about the rename (no
      `prepareRename`, `linkedEditingRange`, or `workspace/willRenameFiles`/
      `didRenameFiles`) — import paths elsewhere go stale until the server notices on
      its own.
- [ ] **LSP multi-root, remainder** — the per-buffer-resolved-root half (a monorepo
      subpackage's own `package.json`/`pyproject.toml`/`Cargo.toml`/
      `compile_commands.json`/... earning its own server connection distinct from the
      outer repo's single `ProjectRoot()`, via `Editor/Lsp/LspRootResolver.h` +
      `LspManager`'s `ConnectionKey`) shipped. Still open: a server that itself supports
      the LSP `workspaceFolders` protocol (one process, multiple folders) is never used
      that way — this client always spawns a separate process per resolved root instead,
      consistent with `LspManager`'s existing "one connection per key" design, but a
      genuinely heavier footprint for a server that would rather multiplex folders
      itself. Also: five connection-scoped caches (`semanticTokensLegend_`,
      `onTypeFormattingTriggers_`, `pullDiagnosticsUnsupported_`,
      `inlayHintsUnsupported_`, `codeLensUnsupported_`, `activeProgress_`, and the
      `failedCommands_`/`disconnected*` status-latch group) stay keyed by the plain
      language string rather than the per-root connection identity — two
      *simultaneously running* servers for the same language against two different
      roots can shadow each other's legend/status/progress-label; every actual request
      still routes to the correct per-root connection regardless (see `LspManager.h`'s
      own header comment).
- [ ] **`ListPopup` mouse support, remainder** (mouse-support follow-up: click-to-
      select-and-activate shipped for `BufferListPanel` — free, its existing
      `SetOnHighlightChange`/`SetOnActivate` wiring just started receiving mouse events
      too — and for the completion popup, via a new `BufferView::
      AcceptActiveCompletionAt`/`WindowManager::ActivateCompletionAt` pair) — still open:
      the shared M-x/find-file/find-recent-file/bookmark-jump/select-theme/document-
      symbol/workspace-symbol/code-action-select/definition-select candidate popup
      (`candidatePopup` in `main.cpp`) has no click support at all. Confirmed by grepping
      `BufferView.cpp` directly: only `HandleCodeActionSelectKey`/
      `HandleDefinitionSelectKey` (fixed short lists) special-case a plain `'1'`-`'9'`
      keystroke as jump-select; every free-text-filtered session (M-x and the rest)
      treats a digit as literal query text, so a "synthesize a digit chord on click"
      shortcut isn't safely generalizable across all ~9 of its driving sessions — it
      would misbehave (insert a digit into the filter) for most of them. Wiring this
      properly needs a real per-session "activate index N" entry point generalized
      across every `Handle*Key` method that drives this popup, a materially bigger
      effort than the two consumers above. which-key's own popup stays intentionally
      mouse-free (read-only hint, no row is a sensible click target). Also out of scope
      for either popup: hover-highlight-on-mouse-move (bare motion events reaching
      `ListPopup::OnEvent` isn't confirmed for this terminal backend) and wheel-scroll
      (a driving session's `rows` is already a pre-truncated window with synthetic "N
      more above/below" rows baked in — scrolling it is session-level, not something
      `ListPopup` itself does).

### Navigation & Search

- [ ] **Multibuffer gaps**: no full-commit diff view (browsing one commit's whole diff
      from `*vcs log*`, not just the working tree); no result cap/warning on
      `project-find-references` for a very common identifier (a huge match set builds a
      proportionally huge composite buffer) — still true for its RE2 text-scan fallback
      path (no LSP server running for the buffer); the real `textDocument/references` path
      added 2026-08-26 has the same gap in principle but is bounded by whatever the server
      itself returns, not a raw project-wide regex sweep. `VisitResultUnderPoint`'s jump-to-source
      stays line-granularity even though `Buffer::ExcerptRange` already carries the
      exact source byte range that would let it preserve the intra-line column.
- [ ] A real visual side-by-side 3-way merge/diff view. `AutoMerge` auto-resolves the
      common case and drops real `<<<<<<<`/`=======`/`>>>>>>>` conflict markers into the
      buffer for a genuine divergence, but a real conflict is still hand-edited text,
      not a visual diff.
- [ ] **No jump-back stack** — every location-jumping command
      (`lsp-goto-definition`/`-declaration`/`-type-definition`/`-implementation`,
      `lsp-goto-symbol`, `goto-line`, `bookmark-jump`) has no way back: no Emacs
      `mark-ring`/`xref-pop-marker-stack`, no Vim `C-o`/`C-i`, no VSCode `Ctrl--`. Needs a
      per-window stack of saved (buffer, offset) positions, pushed before any jump and
      popped by a new `pop-mark`/`jump-back` command — `Buffer`'s point/mark primitives
      already cover the storage half. Distinct from, but a natural shared foundation for,
      Editor Ergonomics' Vim-mode jumplist/changelist gap below, which is Vim's own
      separate `C-o`/`C-i` ring convention.
- [ ] **No generic `next-error`/`previous-error`** — Emacs' unifying "walk the last set
      of located things" primitive, working uniformly across compile output, grep
      results, and diagnostics. Ned already has the individual result buffers (`*vcs
      status*`, project-search results, the stitched `*diagnostics*` buffer) but nothing
      walks them as a cursor motion outside clicking a line directly.
- [ ] **No hunk-navigation motion** — `vcs-stage-hunk`/`vcs-unstage-hunk` operate on
      whatever hunk covers point, but nothing jumps point to the next/previous changed
      hunk in the buffer (gitsigns' `]c`/`[c`); today that means scanning the diff gutter
      by eye first.

### Editor Ergonomics

- [ ] **Terminal panel gaps**: no drag-resize of the drawer height (needs overlay
      mouse-capture semantics; height is Janet-configurable instead via
      `ned/set-terminal-height-percent`), no scrollback search/selection/copy, no
      multiple terminals/tabs, no terminal-side mouse forwarding to the shell (clicks
      focus the panel, wheel scrolls the ring — TUI apps inside don't receive mouse
      events), no OSC 52/title integration.
- [ ] **Vim-mode gaps**: no jumplist/changelist ring beyond the single `` ` ``/`''`
      toggle (no `C-o`/`C-i` ring); mark letters limited to `a`-`z`/`A`-`Z`/`'<`/`'>`
      (`A`-`Z` are buffer-local here, not vim's cross-file global marks); macros record
      raw keystrokes rather than vim's editable-register-text form; `.` replays
      verbatim (a count typed before it doesn't override the recorded one);
      search/`:s`/`:g` patterns pass straight to PCRE2 rather than translating vim's
      default "magic" escaping convention; Insert-mode `C-o`'s one-shot dot-repeat/
      register bookkeeping isn't unified with the interrupted session's own.
- [ ] **DAP gaps**: attach mode; remapping a breakpoint to the adapter's snapped line
      (only verified/dimming is tracked today); hit-count/`hitCondition` breakpoints;
      cross-restart persistence of conditions/logMessage/watches/thread focus (session
      storage deliberately stayed the old line-only shape); debug console has no
      scrollback/search/history-recall; no exception breakpoints
      (`setExceptionBreakpoints`/`exceptionInfo` — can't break on throw/uncaught); no
      function or data breakpoints; no `restartFrame`; no disassembly/memory view; every
      stop sends `disconnect`+`terminateDebuggee:true` rather than distinguishing a real
      `terminate` from a detach, which some adapters expect (2026-08-25 audit).
- [ ] **No buffer-list/ibuffer-style management** — `switch-to-buffer` is a
      name-completion prompt only; no dedicated buffer-list buffer with mark/save/kill
      batch operations.
- [ ] **No smart/positional indentation** (fill-paragraph follow-up, 2026-09-01 audit) —
      `indent-for-tab-command`'s own comment in `Commands.cpp` already says it plainly:
      "not real indent logic ... this codebase has no per-mode indent rules yet." TAB
      today either hits a mode's own keymap override (`org-cycle`'s fold-or-table-align,
      `markdown-table-align`'s table-align-or-fallback) or, globally, just inserts a
      literal `\t` — there's no "compute the correct indentation column for the line at
      point from surrounding syntax" anywhere, and `newline` (`Commands.cpp`) is a bare
      `InsertAtPoint("\n")` with no indent-carry to the new line either (no
      electric-indent). A genuinely new subsystem, not a small follow-up: nothing in
      `Mode` (`HighlightFunction`/`FoldFunction`/`ExpandSelectionFunction` are its only
      function-pointer fields today) or `TreeSitter/` (no `indents.scm` query-embedding
      convention alongside the existing `highlights.scm`/`*-folds.scm`/`*-tags.scm`
      ones) has anything to build this on top of. Likely shape, mirroring the
      nvim-treesitter/Helix convention rather than inventing one: a per-language
      `indents.scm` (`@indent`/`@dedent`/`@aligned`-style captures) embedded the same way
      `ned_embed_treesitter_query` already embeds every other bundled query, a new
      `Mode::indentColumn` function-pointer field alongside the other three, and an
      algorithm that walks the syntax tree from the target line up through indent/dedent
      markers to a column (real Emacs' and Helix's approach, not naive "copy the line
      above's indent" — that's `[Performance]`-cheap but wrong the moment nesting
      changes). Both `indent-for-tab-command` and a new `newline-and-indent` (or making
      plain `newline` electric) would consume it. Scope is per-language: JSON/YAML/Python
      "just" need indent-after-`:`/after-open-bracket; C/C++/JS/TS/PHP need brace-depth
      plus continuation-line rules; Markdown/Org need list-item/heading-relative
      indentation entirely outside a code-syntax model. A real feature, not a quick
      follow-up — scope it per-language incrementally rather than attempting full parity
      in one pass.
- [ ] **No server/daemon mode** — no `emacsclient`-equivalent; one process per terminal,
      no way to keep a warm process (buffers, LSP connections, undo history) alive and
      attach a new terminal client to it.
- [ ] **Test-runner gaps**: no gutter-click run-this-test; no Go/Rust test discovery (no
      bundled modes — their output still parses); pytest needs `-v` or junit-xml for
      per-test pass marks; Go's basename-only `file:line` can miss jump-to-source in
      multi-directory modules.
- [ ] **Snippet expansion gaps**: `$TM_*` variables, choices, transforms, nested
      placeholders' inner stops, bundled default snippets, macro-replay continuation
      through a session.
- [ ] Hunk unstage matches point against the *cached* staged diff, which drifts when
      unstaged edits exist earlier in the file — exact in the common stage-then-undo
      flow; revisit only if it bites.
- [ ] **`libned` as a real shared library** — `ned_lib` (static today) exists solely so
      `ned_tests` can link real editor code without pulling in `main()`; a static lib
      already does that job. Worth revisiting only if a second real consumer shows up
      (an embedding use case, a separate CLI tool) — would need symbol-visibility
      curation and SONAME/ABI-versioning discipline that don't pay for themselves yet.
- [ ] A friendlier, possibly visual surface for browsing/editing ned's own settings
      beyond hand-writing `init.janet` — real live-editing already exists for themes
      specifically (`save-theme`/`ned/theme-set`); a general settings surface would
      generalize that. Vague, unscoped.
- [ ] **No surround editing** (vim-surround/mini.surround/evil-surround: add/change/
      delete a delimiter pair around a region or text object) — distinct from
      `AutoPair.cpp`'s type-time pairing; Vim mode's existing
      `Editor/Vim/VimTextObject.h` text-object parsing is most of what it needs to hang
      on to.

### VCS Side Panel

Core slice shipped 2026-09-01: a persistent left-side panel for working-tree changes,
`UI/VcsPanel.h/.cpp`, physically shaped like `ProjectSidebar` (rounded border,
collapsible to a 1-column strip, drag-resize divider, keyboard-focusable with arrow-key
navigation) and docked in the same left slot, swappable with it — `toggle-vcs-panel`
(`C-c V`)/`focus-vcs-panel` (`C-c v p`) mirror `toggle-project-sidebar`/
`focus-project-sidebar`'s own pair, and `BufferView` keeps the two mutually exclusive on
that shared slot (expanding one collapses the other via plain `SetCollapsed`, not the
persisting `CommitCollapsed`, so an automatic swap never overwrites the other widget's
own remembered visibility). Starts hidden by default (unlike `ProjectSidebar`) since it's
a new opt-in surface. `Editor/Vcs/VcsRowStatus.h` hoists the row-severity classification
(`VcsRowStatus`/`ClassifyPorcelainStatus`) that used to be `ProjectSidebar.cpp`-local so
both widgets share it, and adds `PartitionVcsStatus` (staged/unstaged/untracked bucketing
from git's porcelain `XY` code) for this panel's own section grouping. Built entirely on
the `VcsRunner`/`VcsProvider` plumbing that already existed — no backend changes.

Two deliberate v1 cuts from the original design sketch below: no sticky-scroll for
section headers while scrolled into deep content (`ProjectSidebar`'s own affordance,
skipped here as scope, not an oversight), and directory-tree rows use indentation only,
no box-drawing tree-connector glyphs (`ProjectSidebar`'s `├─└─│`) — both easy follow-ups
if the plain-indent tree reads as too flat in practice.

- [x] **Tree view of working-tree state** — staged / unstaged / untracked as three
      collapsible sections, each file grouped into a directory tree (`VcsPanel.cpp`'s own
      `BuildStatusTree`, built from the known status-entry path list rather than a disk
      walk, reusing `ProjectTreeEntry`'s dir/file/depth shape) rather than a flat list.
      Real ☐/☑ ballot-box glyphs for the multi-select checkbox (see below), not bracketed
      ASCII.
- [x] **Multi-select and batch actions** — Space marks/unmarks the focused row (dired-
      style), and a click squarely on the ☐/☑ glyph does the same from the mouse (a click
      anywhere else on a file row opens it instead). `a`/`u` stage/unstage the whole
      marked selection if non-empty, else just the focused row — `VcsRunner::
      RequestStage`/`RequestUnstage` already take one path at a time, so this loops one
      call per target rather than needing a new batch primitive.
- [x] **Inline diff preview on selection** — `UI/VcsDiffPreview.h/.cpp`, a non-focusable
      `OverlayHost` bottom drawer (`TerminalPanel`/`AcpPanel`'s own geometry, shown/hidden
      automatically by `VcsPanel::SetOnSelectionChanged` rather than a toggle keybinding).
      Uses the new `VcsRunner::RequestFileDiffText` (raw diff text, scoped to one file --
      `VcsDiffHunk`/`ParseDiff` is deliberately header-only, so the *raw* text is what
      `DiffPatch.h`'s `ParseDiffHunks` needs) + a path-based `VcsRunner::RequestHunkApply`
      overload (shares one core with the existing point-driven `Buffer`-taking overload).
      Each hunk header renders a `[stage]`/`[unstage]` click affordance; clicking it calls
      `RequestHunkApply` directly and re-fetches the same file's diff to stay live.
      tmux-verified end to end (real SGR mouse click, confirmed against real `git diff`).
- [x] **Commit compose inline** — `c` fires `BufferView::RequestVcsAction(Commit)`
      (routed via `WindowManager::RequestVcsPanelAction` to whichever pane has focus),
      which just calls the existing `BeginVcsCommitMessage()` unchanged — no new commit
      primitive, the panel's border title shows "N staged" for context before it's fired.
- [x] **Branch switcher/creator inline** — `w`/`n` fire the same `RequestVcsAction`
      routing into the existing `BeginVcsSwitchBranchPrompt()`/(newly extracted)
      `BeginVcsCreateBranchPrompt()`; the checked-out branch (from `RequestBranchList`'s
      `current` entry) renders in the panel's border title rather than a dedicated content
      row, `ProjectSidebar`'s own header-row precedent.
- [x] **Discard/revert changes** — `VcsProvider::RevertArgv` (`git checkout HEAD -- path`,
      explicitly targeting HEAD rather than the index so a staged-and-further-edited file
      discards both halves at once). `x` on a file row enters a self-contained confirm
      state (this widget has no `MinibufferPrompt` to borrow) rendered in place of the
      border title; only a bare `y`/`Y` confirms, everything else — including Enter —
      cancels, the one destructive action in the panel getting real "are you sure"
      friction unlike stage/unstage.
- [x] **Stash support** — `VcsProvider::StashListArgv`/`ParseStashList`/`StashPushArgv`/
      `StashPopArgv`/`StashDropArgv` (+ `vcs-git.janet`'s own argv/parse implementations)
      back a fourth panel section, "Stashes (N)" — unlike the three working-tree
      sections, shown only when non-empty. `z` (Magit's own real-world stash mnemonic)
      pushes the whole working tree from anywhere in the panel; Enter/click on a stash
      row pops it, `d` drops it.
- [x] **Push/pull/fetch** — `VcsProvider::PushArgv`/`PullArgv`/`FetchArgv` (bare, relying
      on an already-configured upstream tracking branch, no `--set-upstream` vocabulary)
      + `vcs-git.janet`'s own implementations. `f`/`F`/`P` (Magit's own real-world
      mnemonics) fire fetch/pull/push from anywhere in the panel, no confirm friction.
- [x] **Ahead/behind + dirty-count summary** — `VcsProvider::AheadBehindArgv`/
      `ParseAheadBehind` (`git rev-list --left-right --count HEAD...@{u}`) refreshed on
      the same throttled cycle as branch/status; the panel's border title gains
      `↑N ↓M` when known and non-zero (nothing shown for "up to date" or "no upstream
      configured", to keep the common case uncluttered) alongside the existing branch/
      staged-count text.
- [x] **Conflict-file affordance** — `VcsPanel::RefreshConflictedPaths` (piggybacked on
      the same throttled `RefreshStatus` cycle) reads each staged/unstaged entry's
      on-disk content and checks `Text/ThreeWayMerge.h`'s `HasConflictMarkers` (already
      used by `save-buffer`'s guard); a hit appends a `⚠` glyph to the row (not prefixed,
      to keep the checkbox-click column math undisturbed) and Enter/click jumps point to
      the first line-start `<<<<<<<` marker.

All items shipped 2026-09-01. One live bug found and fixed along the way, worth noting
for anyone adding a third `VcsRunner::RequestStatus` poller in the future:
`ProjectSidebar` and `VcsPanel` each independently poll status on their own timer (500ms/
1000ms) against the identical `"status:"+root` key `VcsRunner` single-flights: since both
paint from the same keypress-triggered frame and `ProjectSidebar` paints first in
`main.cpp`'s composition, it won the race almost every time, permanently starving
`VcsPanel`'s own throttled refresh (only succeeding via a `force=true` call from another
operation's `onSuccess`). Fixed by having `VcsPanel::RefreshStatus` advance its own
throttle timestamp only on real success, not preemptively — a lost race now retries on
the very next frame instead of waiting out a whole new window.

### Jupyter Notebooks

Feasible, but subsystem-sized — closer in total scope to the LSP and DAP builds
combined than to any single feature shipped so far. Three genuinely separable pieces,
each with its own verdict:

**The kernel protocol client** (the hard, unavoidable part) — real interactive
notebooks need the actual Jupyter messaging protocol: 5 ZeroMQ sockets (shell/iopub/
stdin/control/heartbeat) carrying HMAC-signed multipart JSON, addressed via a
connection file (ports + key) written after the kernel process is spawned. There's no
shortcut around this — `jupyter console --simple-prompt`'s text-only REPL loses rich
output entirely (no `image/png`, no structured tables), and shelling out to `nbconvert
--execute` per run loses the interactive "run one cell, keep kernel state" loop that's
the entire point.
- [ ] ZeroMQ becomes a new dependency (libzmq C library + cppzmq's header-only C++
      wrapper) — pullable via `FetchContent` like everything else, but a heavier build
      dependency than anything currently vendored.
- [ ] HMAC-SHA256 message signing has no existing primitive in this tree. Hand-rolling
      SHA256+HMAC (~150 lines, a well-specified algorithm, low stakes since it's
      same-machine IPC integrity rather than a real security boundary) fits this
      codebase's own precedent (hand-rolled LSP/DAP/ACP framing) better than pulling in
      a full crypto library for one function.
- [ ] The client itself is the same shape already proven three times over
      (`Lsp/LspClient.h`, `Dap/DapClient.h`, `Acp/AcpClient.h`): background `jthread`
      read loop, `EventLoop::Post` marshaling every frame onto the main thread, a
      manager above it owning lifecycle/handshake/in-flight-request bookkeeping — the
      wire format differs (ZMQ multipart + HMAC vs. `Content-Length` or
      newline-delimited JSON), the architecture doesn't.
- [ ] Kernel discovery should walk `share/jupyter/kernels/*/kernel.json` directly
      rather than shelling out to `jupyter kernelspec list --json`, so this doesn't
      hard-depend on the `jupyter` CLI being on `$PATH` at all.

**Rich output rendering** (two real synergies with existing subsystems, one real gap)
- [ ] Tables (`text/html` DataFrame reprs) parse into a grid and align via the
      *existing* `Editor/Table.h` toolkit (`SplitRow`/`ComputeColumnWidths`/`PadCell`
      are already format-agnostic) — a small `<table>`-extraction layer on top, not a
      new engine.
- [ ] Images (`image/png`, the matplotlib case) reuse the *existing* pixel-graphics
      path already proven live in `Minimap.cpp`: `ncvisual_from_rgba` +
      `ncvisual_blit(..., NCBLIT_PIXEL)` already renders arbitrary RGBA buffers as real
      terminal pixels where the terminal supports it, falling back gracefully where it
      doesn't, exactly as Minimap does today. The one missing piece is a PNG decoder —
      nothing in this tree parses PNG; a single-header vendor (stb_image-style) is the
      pragmatic addition, not a hand-rolled zlib+PNG implementation.
- [ ] `image/svg+xml` (also common from plotting libraries) has no rendering path
      anywhere in this codebase and no terminal protocol renders vector graphics
      directly — v1 would skip it, or later shell out to `rsvg-convert`/similar as an
      optional external tool.
- [ ] Error tracebacks arrive ANSI-colored — needs stripping or a small ANSI-to-`Cell`
      translator, not the full `libvterm` emulator `TerminalPanel` uses (that's built
      for a live interactive shell; this is a static blob of text).
- [ ] `text/plain` (every rich mimetype's required fallback in nbformat) needs nothing
      new.

**The editing/UI model** (the real structural departure) — ned's whole editing surface
is built on `Buffer` = one Rope of text; a notebook's natural unit is a *sequence of
cells*, each with its own source text, type (code/markdown/raw), and non-text output
data. Nothing today models "many independently-editable text regions plus non-text
data, composed as one document." `Editor/EmbeddedDocuments.h` is the nearest existing
precedent and isn't that close — it builds *virtual, non-editable, width-preserving*
documents for LSP sync only, not real independently-editable cells.
- [ ] Two shapes are open: **(a)** a `NotebookView` widget (parallel to `BufferView`)
      directly composing several real per-cell `Buffer`s + a per-cell `Mode` (a code
      cell in a Python kernel gets full `PythonMode` highlighting, potentially real LSP
      sync too) plus non-editable output panels between them; **(b)** something closer
      to Org's outline-over-flat-text trick — one `Buffer` in a synthetic linear
      representation with cell boundaries as markers, translating to/from `.ipynb` JSON
      only at load/save. (a) is more work but composes cleanly with everything
      `Buffer`/`Mode`/LSP already assume; (b) is a smaller structural add but forces
      outputs (images, rich tables) into a `Buffer`'s text model, which they
      fundamentally aren't. (a) is the likely right call precisely because it reuses
      more of the existing machinery, not less.
- [ ] File identity gets murkier under (a): does each open notebook register its cell
      `Buffer`s in `BufferList` (so they'd leak into `switch-to-buffer`/the tab bar), or
      does `NotebookView` own them privately outside `BufferList` entirely? The latter
      is probably right but is a real departure from "everything is a buffer."

**Explicit constraint carried over from the Org Babel "won't do" entry below**: a
`.ipynb` *is* a code-execution artifact by definition — unlike a `.org` file, which is
ostensibly prose that Babel would silently turn into one — so building this at all is a
different call than Org Babel was. The same principle still applies inside it, though:
opening a notebook file must never execute anything; every cell run is one explicit
user action, mirroring how `Dap/`'s launch step and `Tasks/`'s run command already
require an explicit trigger rather than firing on buffer-open.

**Suggested phasing** (each phase independently shippable/demoable):
1. Parse/serialize nbformat v4 JSON into an in-memory `Notebook` struct — pure,
   unit-testable, no kernel/UI involved; round-trip fidelity is the only bar.
2. Read-only `NotebookView`: render existing cells (source + already-saved outputs)
   with the rendering pieces above — proves the rendering story before any protocol
   work starts.
3. Kernel protocol client + manager (ZMQ, HMAC, spawn/handshake), no UI — testable
   headlessly against a real `ipykernel`, the same way `LspClient`'s tests run against
   real `clangd`.
4. Wire "run cell" into `NotebookView`, one cell at a time, no kernel-state UI polish.
5. Everything else (interrupt/restart kernel, kernel-status indicator, variable
   inspector, notebook-wide "run all") is incremental once 1-4 exist.

Won't do in v1 regardless of the above: ipywidgets (a separate protocol layered on top
of the base one), collaborative/real-time editing, any kernel-specific special-casing
beyond whatever `kernel.json` advertises.

**Reuse candidates once this exists**: the rich-output-cell renderer (table via
`Table.h`, image via the pixel-blit path, ANSI-stripped text) is generic over "a block
of structured content below some source," not Jupyter-specific — `AcpPanel`'s
transcript has the same open gap (lightweight markdown rendering, better tool-call/
table output display, both listed in its own ROADMAP entry above) and is a stronger,
nearer-term reuse target than Jupyter itself. If Org Babel is ever revisited despite
the "won't do" below, this same renderer (and possibly `NotebookView`'s cell-execution
UI) is the natural substrate for displaying a `#+BEGIN_SRC` block's results, rather than
a third bespoke implementation — worth designing the renderer as its own reusable piece
rather than embedding it directly in `NotebookView` for exactly this reason.

### Named Projects & Multi-Project Sidebar (New Feature)

Local-only slice shipped: `Editor/ProjectRegistry.h` (a named-project catalog,
`$XDG_STATE_HOME/ned/projects.json`, `Session.h`'s `FilePlaceStore` precedent),
`switch-project`/`open-project` (`C-c P s`/`C-c P o`, plus a click on the sidebar's
title row for switch-project), and `Editor/ProjectSwitch.h`'s activate-root chain —
`Editor/TerminalTabLauncher.h` opens the picked project as a sibling process in a new
tab/window when a known terminal/multiplexer is detected (tmux, screen, Konsole,
GNOME Terminal, WezTerm, Ghostty, kitty — all live-verified except GNOME Terminal, not
installed in the environment this shipped in), else a configured
`ned/set-project-open-command`, else replaces the current process in place
(`main.cpp`'s `RunInteractiveEditor`/`PendingReExec`/`execv()`). Two real settings
adjustments some terminals need before the new-tab path actually launches anything —
Konsole's `konsolerc` `[KonsoleWindow]`/`EnableSecuritySensitiveDBusAPI`, kitty's
`allow_remote_control`/`listen_on` — are covered in `ned/set-project-open-command`'s
own doc string; ned never sets either itself, both are real "let a running terminal
accept typed-in commands over IPC" security toggles.

Still open, all genuinely gated on Remote Development below (a registry entry's root
staying local-only for now is a storage-shape choice, not a hole in what shipped):

- [ ] **Remote project URIs (`user@host:/path`)** — a registry entry's root can be a
      remote URI using the same syntax `scp`/`ssh` already accept (no bespoke URI
      grammar to invent). Actually opening one is gated on Remote Development's own
      file-I/O seam (`Buffer::FromFile`'s remote path) existing first — this bullet is a
      UI/storage change on top of that, not a substitute for it.
- [ ] **`~/.ssh/config` awareness** — parse (not shell out to `ssh -G`, which resolves
      one host at a time and is awkward to batch-query for autocomplete) the user's own
      `~/.ssh/config` — `Host`/`HostName`/`User`/`Port`/`IdentityFile`/`ProxyJump`,
      wildcard `Host` patterns included — to default the user/port/key when a typed
      `host:/path` URI doesn't repeat what SSH config already knows, and to drive
      host-name autocomplete on the open/connect prompt. A small self-contained parser
      (the format is simple and line-oriented) rather than a new dependency.
- [ ] **Autocomplete on the open/connect prompt** — project name (registry) first, then
      host (parsed `~/.ssh/config` `Host` entries) for an unregistered target, then
      remote path (once connected, via lazy per-directory SFTP `readdir` — the same
      expand-on-demand shape `ProjectSidebar` already uses locally). `Editor/
      FuzzyMatch.h` is the natural filter for all three, matching every other
      fuzzy-completed prompt in this codebase.
- [ ] **SSH transport: shell out vs. libssh** — either is fine; leaning shell-out first,
      consistent with Remote Development's own phase-(a) plan below. Shelling out to
      real `ssh`/`sftp` (`Editor/Process/ChildProcess.h`'s existing subprocess-wrapping
      precedent) means zero new build dependency and automatic, free reuse of the
      user's own config/agent/`known_hosts` handling — the cost is a process-spawn
      round trip per operation and no persistent multiplexed connection without also
      managing `ssh -M`/`ControlMaster` sockets by hand (worth trying before reaching
      for `libssh` — it may close most of the multiplexing gap with no new dependency
      at all). Direct `libssh` linkage buys one auth handshake + many channels and
      programmatic SFTP with no per-call subprocess spawn, at the cost of a new
      `FetchContent` dependency and reimplementing config/agent/known-hosts handling
      the real `ssh` binary already gives away for free.
- [ ] **`TerminalTabLauncher` remainder** — Terminator, Tilix, and any other emulator
      with a real CLI/IPC way to join a running instance are a documented remainder,
      not v1; add on the same table-driven pattern once someone actually needs one. GNOME
      Terminal's own handler shipped but was never live-verified (not installed in the
      environment this shipped in) — worth a real check the first time it's reachable.

### Remote Development (SSH Remote Editing)

The goal: edit files on a remote host over SSH without ned itself running remotely —
comfortable enough that it doesn't feel like a degraded mode. See "Named Projects &
Multi-Project Sidebar" above for how a remote root is named/stored/opened/switched to;
this section is the actual file-I/O/search/agent mechanics underneath one. Two shapes
are on the table and genuinely undecided: **(a) client-only**, everything shells out to
`ssh`/`sftp` per operation, no code footprint on the remote host at all; **(b) client +
thin remote agent**, a small companion binary deployed to the remote host (the
JetBrains Gateway/VS Code Remote-SSH precedent) that does what a bare SSH session is
genuinely bad at — fast recursive search, live file-change notification, and, the big
one, running the actual language server/debugger/task/VCS subprocesses *on* the remote
host against its real toolchain rather than against a locally-synced copy missing the
remote system's headers/deps/`compile_commands.json`. Likely path: build (a) first
since it's simpler and gets editing working at all; add (b) only once search latency or
LSP-against-the-wrong-toolchain prove it's needed in practice, not speculatively.

**File I/O & the editing model**
- [ ] A remote-file I/O seam behind `Buffer::FromFile`/the save path (an interface, not
      a hardcoded `std::filesystem` call) so a remote buffer's `Rope`/`UndoTree`/
      multi-cursor/etc. stay completely unchanged — read the whole file once (`sftp`/
      `ssh host cat`), edit it exactly like a local buffer, write it back on save
      (matching the instinct: edit locally, sync on save, not a live remote-authoritative
      buffer). This is most of "make it work at all" and touches no core editing code.
- [ ] `ExternallyModified()`/`AutoRevert`/`AutoMerge`/`FileWatch` all assume a cheap
      local `stat` — decide what they mean for a remote buffer: probably a `stat`-only
      round trip on the existing poll tick (no live inotify without a remote agent),
      likely with a longer poll interval over a slow link.
- [ ] Remote path display throughout the UI — tab bar, mode line, sidebar title, buffer
      names — needs a clear `user@host:/path` presentation distinct from local paths.
- [ ] Save/revert/external-modification error handling for a connection that can drop
      mid-operation — a local-disk write basically can't fail; a remote one can,
      constantly, and needs real user-facing recovery (`Editor/ProcessTimeouts.h`'s
      existing settings-module shape is the right precedent for configurable timeouts).

**Project tree & search**
- [ ] Remote directory listing for the sidebar — plain SFTP `readdir`, lazily per
      expanded directory (matches `ProjectSidebar`'s existing expand-on-demand model, no
      new mechanism needed), just slower per round trip; needs an async/spinner
      treatment so a slow link doesn't freeze the UI (`AsyncFileLoader`'s own precedent).
- [ ] **Remote project search is the one place a bare SSH session is genuinely
      insufficient** — shipping every file across the wire to search locally would be
      very slow past a LAN. Two real options: (a) shell out to a remote `rg`/`grep` if
      present on the remote host — zero deployment, but an extra dependency assumption
      and search semantics that won't exactly match ned's own `.gitignore`/binary-
      detection rules; (b) a remote agent reusing ned's actual `ProjectSearch`/
      `GitIgnoreMatcher`/RE2 engine, guaranteeing identical results locally and remotely.
      This is the strongest concrete argument for building the agent at all.
- [ ] Project-wide replace, find-references, and the agenda/TODO scan all sit on top of
      `ProjectSearch` today — decide whether they degrade gracefully in client-only mode
      or need the same remote-agent path search does.

**The remote agent/service** (explicitly OK to defer — scoped here, not started)
- [ ] Define what it actually needs to do beyond search: fast directory listings without
      N round trips; cheap change notification (a remote `inotify` watcher relayed back,
      closing the same gap `FileWatch.h` solves locally); optionally hosting the real
      LSP/DAP/task-runner/VCS subprocesses on the remote machine against its own
      toolchain and proxying their stdio back — likely the single biggest design lift in
      this whole feature, bigger than local file editing itself, since it means
      `LspManager`/`DapManager`/`TaskRunner`/`VcsRunner` would all need a "spawn this
      subprocess remotely instead of via `ChildProcess::posix_spawn`" seam.
- [ ] Transport/protocol for talking to the agent: an SSH-forwarded Unix socket or a
      `ssh -L`-forwarded local TCP port, framed the same way `Lsp/Transport.h`/
      `Acp/Transport.h` already are — the codebase has built this exact client/manager/
      transport shape three times now (LSP, DAP, ACP); a fourth following the same
      precedent is low-risk relative to inventing something new.
- [ ] Auto-deployment: detect the agent is missing or stale on the remote host and
      self-install it (`scp`/`sftp` push + `chmod +x`), the VS Code Remote-SSH/JetBrains
      Gateway precedent, with a version handshake so a stale cached copy gets replaced
      rather than silently mismatching.
- [ ] **Language/toolchain choice for the agent is genuinely open.** Reusing `ned_lib`'s
      own C++ `ProjectSearch`/`GitIgnoreMatcher`/RE2/`ChildProcess` code as a headless,
      UI-free build gives identical search/gitignore semantics locally and remotely for
      free and avoids a second implementation to keep in sync — but `ned_lib` doesn't
      currently separate cleanly from Notcurses/Janet, so this needs `ned_lib` split
      into a real UI/scripting-free "core" library first (a legitimate, if mechanical,
      restructuring — also exactly what a future `libned`-as-shared-library consumer
      would need, see that item above). Rust or Go remain reasonable fallbacks if that
      split turns out costlier than a from-scratch rewrite — genuinely easier static
      linking/cross-compilation for "any x64 host" at the cost of reimplementing and
      hand-syncing gitignore/search-matching semantics a second time. Decide once the
      C++ split's real cost is known, not before.
- [ ] Repo/build story: a standalone repo (this is a genuinely separable concern from
      the core editor), pulled back into ned's own build via CMake `FetchContent` like
      every other bundled dependency. The agent binary itself needs to target the
      *remote* host's architecture, not the build machine's — either cross-compiled at
      ned's build time or fetched prebuilt per-arch (GitHub-releases-style), since a
      build toolchain isn't guaranteed to exist on an arbitrary remote host.
- [ ] Perhaps we could have a way to enable and execute a remote debug session,
      particularly useful for things like PHP, and the like, where we could remotely
      debug something that's happening live in production.


**Connect UX**
- [ ] A connect dialog/command (`ned-connect` or similar): host, user, port, key/agent
      selection, jump-host/bastion support. Sourcing defaults from `~/.ssh/config` and
      the remembered-projects list are covered by "Named Projects & Multi-Project
      Sidebar" above rather than a separate mechanism here.
- [ ] Host-key verification / first-connection trust prompt — `ProjectTrust.h`'s
      existing hash-based, disuse-expiring trust registry (built for `.ned/init.janet`)
      is a good precedent for this UX rather than silently trusting or reimplementing
      OpenSSH's own `known_hosts` handling from scratch.
- [ ] Reconnection/resilience story for a dropped connection mid-session: unsaved local
      buffer content already survives (it's just memory), but in-flight remote
      operations (search, save, LSP requests) need a clear timeout/retry/failure surface
      rather than hanging silently.
- [ ] Confirm mixing local and remote buffers in the same window layout genuinely "just
      works" once the I/O seam exists (`WindowManager`'s panes are already
      buffer-agnostic) rather than assuming it without checking.
- [ ] Port-forwarding scope check: is this only for reaching the remote agent's own
      socket, or also user-facing forwarding of a remote dev-server port back to the
      local machine (JetBrains Gateway does the latter) — worth naming as a distinct,
      smaller feature rather than silently bundling it in.

### Collaboration & AI

- [ ] **AI-assisted editing (ACP) gaps** (2026-08-26: round 1 validated live against
      Claude Code's own ACP adapter, `@agentclientprotocol/claude-agent-acp` — first
      real agent exercised end to end, not just crafted-JSON tests). Fixed this round:
      `HandleSessionUpdate`/`PushOrUpdateToolCall` no longer clobber a tool call's
      title/status back to a generic fallback when a later `tool_call_update` omits
      them (confirmed live — Claude's adapter routinely does); a tool call's `"diff"`
      content item (`{path, oldText, newText}`) is now captured
      (`TranscriptEntry::diffOldText`/`diffNewText`) and rendered as a line-count
      summary in the panel (a real unified-diff view is still a follow-up — see below);
      permission-prompt *resolution* now happens directly in `AcpPanel` when it has
      focus (`WindowManager::SetAcpPanelFocusChecker`), not only `BufferView`'s
      echo-area flow; a pending permission prompt (or any live agent chatter) no longer
      trips `ExpireStaleRequests`' generic 30s timeout out from under a human still
      deciding — confirmed live, this previously hard-failed any permission decision
      that took a bit over 30 seconds; the "ACP agent:" prompt now Tab-completes
      against `AcpConfig::AcpAgentNames()` instead of being pure free text (useful for
      registering several accounts, e.g. `claude-personal`/`claude-work`/
      `claude-consulting` — see below). Still open: no scrollback in the panel; a real
      diff view (actual +/- lines, not just a line-count delta) has no reusable
      line-diff utility to build on yet (`ThreeWayMerge.h`'s LCS diff is a private
      implementation detail); `terminal/*` tool-call support and `elicitation/create` structured
      forms are undeclared as client capabilities; no multiple concurrent agents/
      sessions (still one at a time, `Dap/`'s own precedent — revisit once a second
      agent, e.g. OpenCode, is wired the same way); no `session/load` history replay;
      `session/set_config_option`/`session/set_mode` aren't surfaced to the user; no
      MCP server passthrough (`session/new`'s `mcpServers` is always `[]`); no
      per-agent environment-variable override — `ChildProcess`'s `posix_spawn` always
      forwards the parent's global `environ` (`ChildProcess.cpp:140`), so multiple
      registered agents needing different credentials (e.g. separate Claude accounts)
      need a shell-wrapper argv (`sh -c '<set the right env> exec ...'`) rather than
      anything first-class; no per-agent "character" (a per-agent display-name/accent
      color, distinguishing `agent_thought_chunk` from `agent_message_chunk`) — v1 only
      widened `AcpPanel`'s own `DisplayStyle` (agent text/plan steps no longer share
      the exact same style as a plain user-message echo), deliberately not a full
      theming pass. Composer/echo-area input field's append-only editing (no cursor
      position, no forward-delete, no arrow-key movement) closed 2026-08-26 — see
      `git show da0a7c9`. Separately: `Keymap::AmbiguousBindings()` is diagnostic-only (a
      `CommandsTest.cpp` regression test), not enforcement — `Keymap::Bind` still lets a
      caller construct an unreachable-by-typing binding; a real structural fix (Emacs'
      own `define-key` semantics: reject/restructure a bind that would shadow an
      existing command) would change `Bind`'s signature across every call site
      including `ned/define-key`.
- [ ] **ACP chat-feel UX backlog** (2026-08-26 research pass — surveyed what Claude Code's
      own CLI and OpenCode's TUI do that users specifically call out as good, cross-checked
      against what `AcpPanel`/`AcpManager` actually do today; none of this is implemented
      yet, just scoped). Roughly in order of how much a single session of chatting with an
      agent would actually feel it:
      - Interrupt an in-flight prompt, and a visible "agent is working" spinner between
        sending a prompt and the first token, both closed 2026-08-26 — see
        `AcpManager::CancelPrompt`/`PromptInFlight` and `AcpPanel::OnEvent`'s Escape
        handling. Also fixed same day: a redundant "session ready" line no longer
        duplicates the panel's own `[Active]` title-bar state in the transcript (the raw
        `*acp: <agent>*` log buffer still gets it verbatim).
      - **ACP chat-feel round 2**, closed 2026-09-01. `agent_thought_chunk` now lands in
        its own `TranscriptEntry::Kind::AgentThought` (Dim style) instead of being
        coalesced into the same entry as `agent_message_chunk` text (Accent) — a reply
        used to read as one undifferentiated stream of tokens with no seam between an
        agent's private reasoning and its actual answer. Rapid-fire streaming repaints are
        now debounced (~40ms, `AcpManager::agentTextRepaintDebounce_`, `Editor/EventLoop.h`'s
        `DeadlineTimer` — the same primitive the LSP-sync-debounce fix uses) so a
        several-times-a-second token stream doesn't visibly reflow the trailing lines of
        the panel on every single chunk; a brand-new entry (a fresh thought/answer block
        starting, a tool call, a plan update) still notifies synchronously. Resolved tool
        calls superseded by a later one collapse to a single right-aligned-marker line
        (`[done]`/`[fail]`/`[cancel]`) instead of permanently holding 1-2 lines each,
        reclaiming space given the panel's own no-scrollback constraint. The composer
        (`MinibufferPrompt`) gained word-wise cursor motion (`MoveCursorWordLeft/Right`,
        Control-Left/Right — the shared primitive, so find-file/M-x/DAP console could pick
        this up too, though only `AcpPanel` is wired to it yet) and shell-style prompt
        history (Up/Down, re-derived from `Transcript()`'s own `Kind::UserMessage` entries
        rather than a separate ring — editing a recalled entry and paging away from it
        discards those local edits, a deliberate v1 simplification). The panel can now
        minimize to a thin title-only strip (`[-]` button or `M-m`, `AcpPanel::Collapsed()`
        — ProjectSidebar's own convention; the session keeps running in the background) and
        resize via border-drag (mirroring `ProjectSidebar::BeginResize`/`UpdateResize`) or
        Control-Up/Down, applied live through `AcpPanelSizePercent()`. Opening the panel
        now reconnects to the project's last-used agent automatically
        (`ProjectSessionData::lastAcpAgent`, seeded/saved by `WindowManager`) instead of
        requiring the "ACP agent:" prompt every time — a no-op if a session is already
        running or the remembered agent is no longer configured. A real bug caught live
        (screenshot from the user, not found by the unit tests above): `SetCollapsed`
        alone left a stale, wrongly-sized Box in place — `OverlayHost` only recomputes a
        panel's Box from its placement lambda on `Show()`/`Reflow()` (`Overlay.h`'s own
        header comment: "re-derived on every Reflow/Show"), never on every `Paint()`, so
        minimizing repainted the *old, full-size* Box with the collapsed strip's blank
        fill — a wash over most of the screen with the title line at the wrong spot,
        despite the strip logic itself being correct. Fixed via
        `AcpPanel::SetOnCollapseChanged`, firing only on an actual state change, with
        `main.cpp` re-invoking `overlays.Show(*panel)` to force the Box recompute
        immediately; confirmed live in tmux (a content row within the panel's true bounds
        blanks while open and reappears once minimized, matching the Box's real size at
        each step) since headless `Screen::PixelAt` assertions alone hadn't caught the
        stale-Box case. Two more caught live in the same pass, both same-day fixes: the
        minimized strip's title text was painted with `theme_.border`/`borderAccent` (a
        color tuned for a thin decorative line, not a full row of text) — reported as
        nearly unreadable, fixed by switching to `theme_.echoArea`, the composer's own
        proven-legible brush; and the `[-]` minimize button used a plain ASCII "-" where
        `TerminalPanel`'s own title-row buttons use a real glyph (`▼`/`▲`/`×`) — reported
        as visually inconsistent, fixed by adopting `TerminalPanel`'s own `▼` verbatim
        (`kMinimizeIcon`). Separately, an unrelated but adjacent false-alarm bug: every
        line an ACP agent process writes to its own stderr was unconditionally logged at
        `LogSeverity::Warning` (`AcpClient::StartStderrReadLoop`, mirroring
        `LspClient`'s identical pattern) — for `Lsp` this is silent by default (that
        category defaults hidden, specifically because LSP servers are stderr-noisy), but
        `Acp` defaults *visible*, so any benign startup banner line an agent printed (a
        literal "session started" was reported live) tripped `BufferView`'s unsolicited
        "New warning -- see *Messages*" echo message for something that was never actually
        a warning. Fixed by downgrading only that one call site to `LogSeverity::Info`;
        the four genuine-problem call sites in the same file (agent exited, malformed
        frame, request timeout, a real disconnect) are untouched and still warn correctly.
        Known rough edges: a
        right-docked panel's resize handle (its left edge column) has no visually reserved
        border the way `ProjectSidebar`'s divider column does, so it's a click target with
        no on-screen affordance; the user separately floated a bigger idea — a shared
        tabbed bottom dock across `TerminalPanel`/`AcpPanel`/`DebugConsolePanel` instead of
        three independent overlays — deliberately not attempted here, scoped as its own
        follow-up below.
      - **Tabbed bottom-dock overlays** (floated 2026-09-01, not scoped in detail). Right
        now `TerminalPanel`/`AcpPanel`/`DebugConsolePanel` are three independent
        `OverlayHost` overlays, each toggled and placed separately (`AcpPanel` can also
        dock right). Unifying the bottom-docked ones behind one shared tab strip (so
        minimizing/switching one is a tab click, not a separate toggle per panel) would be
        a real architectural change — a new shared container widget owning the tab bar
        plus whichever child panel is active, versus the current "each panel manages its
        own Box via its own placement lambda" shape every overlay in `main.cpp` uses today.
      - **Checkpoint/rewind per turn.** Claude Code's double-Esc `/rewind` (checkpoint
        auto-created per prompt, restore code/conversation/both) is the single most-cited
        "saved me" feature in the research above — and this codebase already has the two
        primitives it needs: `Text::UndoTree` (real tree, cheap `Rope` snapshots) and
        `Editor/Backup.h`. A per-turn checkpoint could be "snapshot every buffer touched by
        `fs/write_text_file` since the last user prompt" rather than a new storage format;
        the open design question is what "restore conversation" even means against ACP's
        `session/load` (still itself unimplemented — see the gaps entry above) rather than
        a from-scratch mechanism.
      - **Word-wrap in the transcript.** `AcpPanel::FormatTranscript`'s `Kind::AgentText`
        case is explicitly "No word-wrap in v1 -- split only on literal newlines the agent
        itself sent" (its own header comment) — a long unwrapped reply line just runs off
        the panel's fixed width with no way to read the rest, unlike `BufferView`'s own
        `Mode::wrapLines`.
      - **Lightweight Markdown rendering** (bold, inline code, bullet markers) in
        `AgentText`/`Kind::Plan` lines instead of showing `**`/backtick markup literally —
        agents write Markdown by default and every popular chat surface (Claude Code
        included) renders it rather than showing raw asterisks.
      - **`@`-style file-mention autocomplete in the composer** — reuse
        `Editor/FuzzyMatch.h` (already backs find-file/M-x/theme-picker) plus
        `Editor/ProjectTree.h` to fuzzy-complete a project-relative path inline while
        typing a prompt, the same affordance Claude Code/OpenCode both use so a prompt
        references an exact file instead of a name the agent has to go search for.
      - Explicitly *not* pulled from the research: OpenCode's session-sharing (`/share`,
        needs a hosted backend — out of scope for a local-first editor) and a unified
        command palette (already a stated non-goal below, for the same
        keep-purpose-built-commands-separate reasoning).
- [ ] **Real-time collaborative editing** (CRDT-based) — the biggest lift in this file;
      last.
- [ ] VCS: "generalize the two-callback plugin shape past version control" (cloud CLIs,
      Terraform, Docker) remains an open idea, not a plan.

### Documentation & Companion Tooling

- [ ] **Internal/developer docs: Doxygen.** `/** */` doc-tags on the C++ side
      (namespace/class-hierarchy aware, matching this codebase's `ned::text`/`editor`/
      `janet`/`ui` layering), themed with Doxygen Awesome. Wired via CMake's
      `find_package(Doxygen)` + `doxygen_add_docs()` as an opt-in `docs` target, not part
      of `ALL` — a build/release-time step, nothing ned does at runtime. clang-doc was
      considered and rejected as still too early-stage (LLVM's own docs warn of bugs/
      crashes on real codebases).
- [ ] **End-user docs: mdBook + pandoc, one Markdown source.** A `book/src/` tree (the
      `Docs/*.md` files migrate in near-verbatim) renders via mdBook into a
      navigable/searchable site, deployed to GitHub Pages via its first-party
      `actions/starter-workflows/pages/mdbook.yml` template — self-hostable later too,
      it's plain static output. Man pages come from pandoc (`pandoc -s -t man`) run over
      a curated subset of the same source files (CLI invocation, the settings/variables
      reference, the scripting API reference — not the whole book; prose guides don't
      map to man's NAME/SYNOPSIS/DESCRIPTION shape). The scripting API reference page is
      generated, not hand-written: a small `Tools/` binary linking `ned_lib`, walking
      `CommandRegistry`/the `ned/*` Janet binding table, dumping the doc strings already
      passed to `Register<Fn>` into a `.md` file the mdBook build consumes (mirroring
      Helix's `cargo xtask docgen` pattern for its own keymap/command reference pages).
      Sphinx+MyST was considered — it natively builds man+PDF+HTML from one source too —
      but rejected in favor of mdBook to avoid adding a Python toolchain to a project
      that currently has none, and for mdBook's first-party GitHub Pages support.
- [ ] **Environment setup tool** (`ned-setup` or similar) — first-run detection: shell
      integration, plus scanning the system for installed tree-sitter grammars and
      *generating an editable Janet file* loaded from `init.janet`. Deliberately a
      standalone, inspectable generator — silent runtime auto-detection was considered
      and rejected (system grammar layouts aren't portable).
- [ ] **Tree-sitter-assisted formatter** with JetBrains-level per-rule configurability
      ("a dprint clone that is actually awesome") — a substantial project per language,
      not a utility. Scope it once concrete gaps left by external formatters are known.

### Known Test Flakiness / Non-Critical Issues (Watch List)

Real, reproduced, non-urgent — each is safe to leave as-is for now, but worth fixing
opportunistically rather than re-discovering from scratch. Add to this list instead of
just fixing-and-forgetting or letting it fade from memory between sessions.

The LSP-broker-hermeticity flake, 2026-08-26 (`LspManagerTest.cpp`'s
two spawn-failure tests, plus the same shape in `ModeLineTest.cpp`'s spawn-failure-glyph
test — the latter caught by re-running the full suite against a real broker daemon left
warm from a live tmux session, exactly the scenario this flake needed to reproduce), was
fixed 2026-08-26 by adding `LspManager::SetBrokerSocketPathOverrideForTesting` and
threading it into `ClientForLanguage`'s `TryConnectToBroker` call; all three affected
tests now point at a guaranteed-nonexistent socket path instead of the real broker
socket.

`EchoArea::Paint`/`ModeLine::Paint` being byte-by-byte, not UTF-8-aware (found 2026-08-26,
live tmux+clangd testing `lsp-signature-help` — a multi-byte guillemet marker rendered as
blank padding, one blank cell per byte, silently wrong despite every unit test passing
since those compared plain byte strings) was fixed 2026-08-26: both now walk by codepoint
span (`Text/Utf8.h`'s `NextCodepointBoundary`, matching `BufferView`'s own
one-codepoint-per-cell content rendering) instead of by raw byte — `EchoArea`'s six
sentinel bytes (always exactly one byte) are still recognized via a one-byte-span check
before the general case; `ModeLine` gained a shared `AppendUtf8Columns` helper replacing
six duplicated byte-loop call sites. Double-width CJK/emoji columns are still one cell
each, matching `BufferView`'s own accepted limitation there — no `wcwidth`-equivalent
exists anywhere in this codebase, and fixing that only in these two widgets would be
inconsistent. `lsp-signature-help`'s active-parameter marker still uses ASCII `**...**`
rather than switching back to guillemets — a cosmetic choice either way now that the
underlying bug is gone.

Normal-mode self-insert silently dropping every non-ASCII keystroke (found 2026-08-26,
reported live as "pasting an emoji does nothing" — any terminal paste/keystroke of a
character outside 0x20-0x7E, not just emoji: accented Latin, CJK, ...) was fixed
2026-08-26. `BuildDefaultGlobalKeymap` only ever gave printable ASCII its own real
self-insert-command keymap entry (deliberately, per its own comment — enumerating a
literal entry per Unicode codepoint isn't feasible), and `Dispatcher::Feed` had no
fallback at all for a `NoMatch` on anything else, so it reported "<char> is undefined"
and dropped the keystroke instead of inserting it. `Dispatcher::Feed`'s `NoMatch` case now
falls through to `self-insert-command` for a *bare* (not mid-prefix-sequence) plain chord
(no Control/Meta/Special) whose codepoint isn't a C0/DEL/C1 control character — a real
rebind of any individual character, ASCII or not, still wins via `Match` before ever
reaching this fallback. `C-y`/`PasteFromSystemClipboard` was never affected (it inserts
the whole pasted string directly via `Buffer::InsertAtPoint`, bypassing per-character
dispatch) — only a terminal-native paste or direct keystroke of a non-ASCII character hit
this.

**Open, not yet root-caused:** `VimEngineTest.cpp:903`'s "Dot-repeat replays an Insert
session that used C-o" — passes cleanly every time run standalone
(`./ned_tests "Dot-repeat replays an Insert session that used C-o"`), but has failed
intermittently when the full suite runs (`ctest --test-dir build`), seen at least
2026-08-30. No obvious static/global mutable state in `VimEngine.h/.cpp` itself on a
first pass (grepped for `static`/`thread_local`, nothing suspicious) — order-dependence
likely comes from somewhere else the test touches (a shared `Buffer`/`Dispatcher`
fixture, register/clipboard global state, or similar to the `DiagnosticsLog`
global-state leak already fixed once in this codebase, `ChildProcess hang-protection
round 2`). Not yet worth a deep dive on its own; next time it reproduces, capture which
other test(s) ran immediately before it in that run — that's the fastest path to an
actual repro.

A second, previously undocumented order-dependent flake, seen twice in the same
session (2026-09-01) while running the full suite repeatedly during unrelated work:
"A textDocument/inlayHint response renders virtual text mid-line without disturbing
real content" — same signature as the VimEngine one above (fails only under
`ctest --test-dir build`, passes cleanly every time run standalone). Not yet
investigated at all; noting it here so it doesn't need rediscovering from scratch next
time it reproduces.

A third instance, different symptom (seen 2026-09-01 while adding the VCS side panel's
own tests, entirely unrelated to LSP): `ctest --test-dir build` reports one `ned_tests`
entry FAILED — `LspContentTest.cpp`'s `ExtractSignatureHelp resolves a [start, end)
offset-pair parameter label` — but running that exact test name directly against the
binary (`./ned_tests "ExtractSignatureHelp resolves a *"`) passes cleanly. The failing
ctest entry's registered name is actually dozens of semicolon-joined `TEST_CASE` names
concatenated into one (`catch_discover_tests`' own registration, visible via
`ctest --test-dir build -N`) — a real suspect given CMake treats `;` as its list
separator, so this may be a test-*discovery* artifact (a mis-split ctest invocation)
rather than the test itself failing. Not yet root-caused; the individual Catch2 test
is confirmed correct.

### Named Non-Goals (Leaning "Won't Do", Kept Visible So It's a Conscious Call)

- [ ] A plugin marketplace/package registry (VSCode extensions, MELPA/straight.el).
      Ned's model is one Janet-scriptable environment plus opt-in project-local plugins
      gated by `ProjectTrust`'s hash-based trust registry — a marketplace implies a
      supply-chain-trust problem this project has deliberately stayed out of.
- [ ] A single fuzzy command palette unifying M-x/find-file/switch-buffer into one popup
      (VSCode/Sublime's Cmd+Shift+P). Real Emacs keeps these as separate, purpose-built
      commands with their own bindings — consistent with this project's Emacs-class-
      parity vision, so this reads as a different, already-chosen philosophy.

### Native Windows Port (Idea, Unstarted — Design Sketch Only)

Raised alongside the system-clipboard work (`Editor/Clipboard.h`'s WSL detection covers
running ned as a Linux binary under WSL today, shelling out to `clip.exe`/
`powershell.exe` over WSL's own interop — already shipped). A *native* Windows build
(running directly under Windows Terminal or a raw console host, no WSL layer) is a
distinct, much larger effort: this codebase is POSIX throughout, not just at one or two
call sites, so "port" means replacing the platform layer wholesale:

- **Process spawning** (`Editor/Process/ChildProcess.h`, `posix_spawn` + `pipe`) needs a
  `CreateProcess`/anonymous-pipe implementation behind the same `WriteAll`/`ReadSome`/
  `WaitForExit`/`Kill` surface — every LSP/DAP/ACP/task/VCS subprocess integration in
  `Editor/` is built on this one class.
- **The embedded terminal panel** (`Editor/Terminal/PtyProcess.h`, `forkpty`) needs
  ConPTY (`CreatePseudoConsole`) instead — a real API, but a different threading/
  handle-lifetime shape than a POSIX pty fd pair.
- **Raw terminal I/O** (`UI/TerminalColorProbe.h`'s `termios`/`poll` raw-mode probe, and
  OSC 52) needs the Win32 console API or, on a recent-enough Windows Terminal, VT
  passthrough.
- **Notcurses itself** would need to build and run against the Win32 console/Windows
  Terminal target — worth checking Notcurses' own upstream platform support before
  committing, since ned's `UI/` layer sits directly on it with no abstraction gap.
- A PowerShell-flavored bundled theme would be a small addition once the port exists —
  `UI/ThemeRegistry.h`'s fixed name→factory table is exactly the extension point.

Unscoped beyond this sketch — process spawning is the obvious dependency root; nothing
else works without it.

## Maybelist (Speculative — Neither Committed nor Rejected)

Ideas worth remembering but not worth scoping yet — too undecided for "Open Items",
not disliked enough for "Won't do". Promote or delete on revisit rather than letting
these accumulate detail in place.

- [ ] **Merge-aware cross-session undo** — persistent undo (`Editor/PersistentUndo.h`,
      shipped 2026-08-25) content-gates: on reopen, restores the full tree only if the
      file's current on-disk content exactly matches some node already in the persisted
      tree (any node, not just the tip -- covers "quit without saving" for free); no
      match at all just discards the persisted history outright and the buffer starts
      fresh. The fancier version would three-way-merge a genuinely novel external change
      into the persisted history instead of discarding it (base = last-persisted content,
      ours = tree tip, theirs = fresh disk content), reusing `Text/ThreeWayMerge.h`/
      `Editor/AutoMerge.h`'s existing machinery to splice one merge node onto the old tip.
      Deferred because it means synthesizing an undo node for content the user never
      actually typed — a real risk of `undo` doing something surprising later — worth it
      only if the content-gate default proves too lossy in practice (i.e. people
      habitually edit files outside ned in ways that touch none of a session's own undo
      states and lose history often enough to complain).
- [ ] **Sticky scroll** — a pinned enclosing-function/class header row while scrolling a
      long body, Zed/VSCode's readability feature. No Emacs precedent; a conscious yes/no
      against the Emacs-class-parity vision rather than an obvious extension of it.
- [ ] **Peek definition** — a floating inline preview of a definition's source without
      leaving the buffer (Zed/VSCode's "peek"), rather than `lsp-goto-definition`'s real
      buffer switch. `Overlay.h`'s `OverlayHost` (already generic, used by
      `TerminalPanel`/`AcpPanel`) is a plausible cheap substrate if this is ever pursued.
- [ ] **AI edit-prediction** (Zed's Zeta: predicting the next multi-line edit from
      cursor/edit history, distinct from LSP-driven completion or ACP's chat) has no
      equivalent here. A different feature from everything `Acp/` already provides, and
      probably needs some model-serving backend of its own — worth naming as a conscious
      gap rather than assuming ACP already covers "AI in the editor."

## Won't Do (at Least Not Soon)

- **Org Babel** — subsystem-sized, and arbitrary code execution triggered by opening a
  text file: a security surface to design around deliberately, not bolt on. If this
  ever gets revisited, the rich-output-rendering half of "Jupyter Notebooks" above
  (table/image/text cell rendering) is the natural substrate to reuse rather than a
  second bespoke renderer — see that section's own note.
- **Org table formula/spreadsheet engine** — a small programming language of its own.
- **Org export backends / publishing pipeline** — each a standalone tool-sized effort.
- **Alt+Click cursor creation** — mouse-dependent in a terminal app where SSH/tmux
  mouse passthrough is unreliable; keyboard multi-cursor commands cover it.
- **X11 primary-selection support for middle-click paste** — `Editor/Clipboard.h`'s
  `PasteFromPrimarySelection`/`ResolvedPrimarySelectionPasteCommand` are deliberately
  Wayland-only (`wl-paste --primary`), a stated user preference given X11's declining
  share; no xclip/xsel `-selection primary` fallback.
- **Bundling or requiring a JVM** for anything (heavy checkers like `ltex-ls` stay
  opt-in user configuration).
- **Accessibility (screen-reader support)** — a Notcurses raw-cell-grid TUI has no
  accessibility tree of any kind; not evaluated or pursued. Named here so it's a
  conscious gap rather than an oversight (2026-08-25 audit).

## Notes for Whoever Builds Next

- Build/test: `cmake --preset default && cmake --build build`, then
  `ctest --test-dir build`. Sanitizer opt-in: `-DNED_ENABLE_SANITIZERS=ON` with
  `-DCMAKE_BUILD_TYPE=Debug` — the suite is expected clean; a finding is a real bug.
- When you finish an item above, delete it (or replace it with a one-line pointer) in
  the same commit — don't leave a `[x]` writeup behind. Keeping this file short is the
  point.
