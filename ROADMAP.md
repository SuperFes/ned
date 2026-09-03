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
      LSP debounce so it lands before they fire their own requests. Incremental sync
      — `TextDocumentSyncKind.Incremental`, sending only the changed span instead of
      the whole document — shipped as the complementary follow-up: `LspContent.h`'s
      `ExtractTextDocumentSyncKind` parses the server's advertised
      `textDocumentSync.change` capability, `LspManager::SyncTextToServer` diffs
      `BufferSyncState::lastSyncedText` against the new content via a common-prefix/
      common-suffix walk and sends a ranged `contentChanges[0]` for an
      Incremental-capable server, falling back to the original full-document form
      otherwise — a server that never advertises a sync kind defaults to Full, not
      the spec's technical None, to keep every already-working server's behavior
      unchanged) — one follow-up deliberately left out of both fixes:
      - **`ProtocolStallTimeoutMs()`'s 30s default** (`Transport.h`) is shared by both
        the write side and the read side (where a genuinely slow response — a large
        workspace-wide rename, say — legitimately needs tolerance). Superseded as a
        main-thread-freeze concern by the async write queue below (a stalled write no
        longer blocks the main thread at all, regardless of this value), but splitting
        it into separate write/read timeouts would still be a reasonable defense-in-
        depth hardening on top — a healthy server should never take long just to
        *accept* a notification.
- [ ] Whether Markdown fenced code blocks / Org `#+BEGIN_SRC` blocks should get the same
      real-LSP-sync treatment HTML `<script>`/`<style>` embedded documents already have
      is an open question — spawning a live language server per code fence in an
      ordinary notes file could be noisy for illustrative/incomplete snippets.
- [ ] **`semanticTokens/range` and delta requests** — the semantic-highlighting client
      only ever sends the full-document `semanticTokens/full` request; no huge-buffer
      windowing or incremental delta support yet.
- LSP edit-application gaps (`documentChanges` file create/rename/delete resource ops,
  server-pushed `workspace/applyEdit`) closed 2026-09-02 — see `git log --grep=edit-application-gaps`.
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
- [ ] **Jump-back forward/redo stack** — `jump-back` (`C-x C-SPC`) has no forward
      direction (Vim's `C-i` equivalent); Vim-mode's own separate jumplist/changelist
      ring is tracked below under Vim-mode gaps.
- [ ] Native Vim-mode `]c`/`[c` binding (gitsigns' own convention) for
      `vcs-next-hunk`/`vcs-previous-hunk` — the global `C-c v N`/`P` binding already
      works under Vim mode via the shared keymap-stack fallthrough, so this is polish,
      not a functional gap.

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

### VCS Side Panel

Shipped 2026-09-01 (`decd881`, `53b8054`) — `UI/VcsPanel.h/.cpp`, a persistent left-side
panel (`toggle-vcs-panel` on `C-c V`, mutually exclusive with `ProjectSidebar` on the
same slot) covering: tree view of staged/unstaged/untracked with multi-select batch
stage/unstage, inline diff preview with per-hunk stage/unstage, commit/branch compose,
discard-with-confirm, stash, push/pull/fetch, ahead/behind summary, and a conflict-marker
affordance. Built entirely on existing `VcsRunner`/`VcsProvider` plumbing.

- [ ] Directory-tree rows use indentation only, no box-drawing tree-connector glyphs
      (`ProjectSidebar`'s `├─└─│`) — revisit if the plain-indent tree reads as too flat.

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

Local-only slice shipped: `Editor/ProjectRegistry.h` (named-project catalog),
`switch-project`/`open-project` (`C-c P s`/`C-c P o`), and `Editor/TerminalTabLauncher.h`
(opens a picked project in a new terminal tab/window — tmux, screen, Konsole, WezTerm,
Ghostty, kitty live-verified; GNOME Terminal's handler shipped but unverified, not
installed in that environment) falling back to a configured
`ned/set-project-open-command` or an in-place `execv()` re-exec.

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

- [ ] **AI-assisted editing (ACP) gaps** (validated live 2026-08-26 against Claude Code's
      own ACP adapter — see `git log --grep=ACP` for the fix history). Still open: no
      scrollback in the panel; a real diff view (actual +/- lines, not just a line-count
      delta) has no reusable line-diff utility yet (`ThreeWayMerge.h`'s LCS diff is a
      private implementation detail); `terminal/*` tool-call support and
      `elicitation/create` structured forms are undeclared as client capabilities; no
      multiple concurrent agents/sessions (still one at a time, `Dap/`'s own precedent);
      no `session/load` history replay; `session/set_config_option`/`session/set_mode`
      aren't surfaced to the user; no MCP server passthrough (`session/new`'s
      `mcpServers` is always `[]`); no per-agent environment-variable override —
      `ChildProcess`'s `posix_spawn` always forwards the parent's global `environ`, so
      multiple registered agents needing different credentials need a shell-wrapper argv
      rather than anything first-class; no per-agent "character" (display-name/accent
      color distinguishing agents beyond the existing thought-vs-text style split).
      Separately: `Keymap::AmbiguousBindings()` is diagnostic-only (a `CommandsTest.cpp`
      regression test), not enforcement — `Keymap::Bind` still lets a caller construct an
      unreachable-by-typing binding; a real structural fix (Emacs' own `define-key`
      semantics: reject/restructure a bind that would shadow an existing command) would
      change `Bind`'s signature across every call site including `ned/define-key`.
- [ ] **ACP chat-feel UX backlog** (round 1 + round 2 shipped 2026-08-26/2026-09-01 —
      interrupt/spinner, thought/text split, streaming debounce, collapsed tool-call
      lines, composer word-motion + history, minimize/resize, auto-reconnect to last
      agent, transcript/composer word-wrap; see `git log --grep=ACP` for detail). Still
      open, roughly in order of impact:
      - **Checkpoint/rewind per turn** — Claude Code's double-Esc `/rewind`. The two
        primitives it needs already exist (`Text::UndoTree`, `Editor/Backup.h`); the open
        design question is what "restore conversation" means against ACP's still-
        unimplemented `session/load` rather than a from-scratch mechanism.
      - **Lightweight Markdown rendering** (bold, inline code, bullet markers) instead of
        showing `**`/backtick markup literally in `AgentText`/`Kind::Plan` lines.
      - **`@`-style file-mention autocomplete in the composer** — reuse
        `Editor/FuzzyMatch.h` + `Editor/ProjectTree.h` to fuzzy-complete a project-
        relative path inline while typing a prompt.
      - **Tabbed bottom-dock overlays** (floated 2026-09-01, not scoped) — unify
        `TerminalPanel`/`AcpPanel`/`DebugConsolePanel`'s three independent `OverlayHost`
        overlays behind one shared tab strip; a real architectural change versus today's
        "each panel manages its own Box via its own placement lambda" shape.
      - Known rough edge: a right-docked `AcpPanel`'s resize handle has no visually
        reserved border the way `ProjectSidebar`'s divider column does.
      - Explicitly *not* pulled from prior research: OpenCode's session-sharing (needs a
        hosted backend, out of scope for a local-first editor) and a unified command
        palette (already a stated non-goal below).
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
just fixing-and-forgetting or letting it fade from memory between sessions. Fixed entries
are removed once shipped rather than kept as a writeup here — see `git log --grep=flak`
for closed-issue history. Nothing currently open: the full suite reruns clean under
`ctest -j8`/`-j16`/`-j32 --repeat until-fail:3` and under the ASan/UBSan
(`build-sanitize`) configuration alike (verified 2026-09-02).

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
