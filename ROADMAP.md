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
- [ ] **No multi-root LSP workspace support** — `LspManager` spawns one server per
      language/server-key against the single process-wide `ProjectRoot()`; a monorepo
      with multiple real project roots (or a server that itself supports
      `workspaceFolders`) gets one server pinned to whichever root the process started
      against. Already a known simplification (see `CLAUDE.md`'s `Lsp/` section) that's
      never been tracked here until now.

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
- [ ] **No server/daemon mode** — no `emacsclient`-equivalent; one process per terminal,
      no way to keep a warm process (buffers, LSP connections, undo history) alive and
      attach a new terminal client to it.
- [ ] **Project root is fixed at startup** — `ProjectRoot.h` exposes `SetProjectRoot` as
      a primitive but nothing wires an `M-x`-reachable command to it; switching projects
      means restarting the process.
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
- [ ] **No `fill-paragraph`/auto-fill** — Emacs' `M-q` (wrap the paragraph at point to a
      fill column) has no equivalent anywhere in `Commands.cpp`, despite the stated
      Emacs-class-parity vision.
- [ ] **No surround editing** (vim-surround/mini.surround/evil-surround: add/change/
      delete a delimiter pair around a region or text object) — distinct from
      `AutoPair.cpp`'s type-time pairing; Vim mode's existing
      `Editor/Vim/VimTextObject.h` text-object parsing is most of what it needs to hang
      on to.

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

### Remote Development (SSH Remote Editing)

The goal: edit files on a remote host over SSH without ned itself running remotely —
comfortable enough that it doesn't feel like a degraded mode. Two shapes are on the
table and genuinely undecided: **(a) client-only**, everything shells out to `ssh`/
`sftp` per operation, no code footprint on the remote host at all; **(b) client + thin
remote agent**, a small companion binary deployed to the remote host (the JetBrains
Gateway/VS Code Remote-SSH precedent) that does what a bare SSH session is genuinely bad
at — fast recursive search, live file-change notification, and, the big one, running
the actual language server/debugger/task/VCS subprocesses *on* the remote host against
its real toolchain rather than against a locally-synced copy missing the remote
system's headers/deps/`compile_commands.json`. Likely path: build (a) first since it's
simpler and gets editing working at all; add (b) only once search latency or
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

**Connect UX**
- [ ] A connect dialog/command (`ned-connect` or similar): host, user, port, key/agent
      selection, jump-host/bastion support — ideally sourcing defaults from the user's
      own `~/.ssh/config` rather than reinventing host aliasing.
- [ ] A remembered recent-connections list — same shape as the recentf gap already on
      this roadmap, worth building on the same primitive rather than a separate one.
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
      implementation detail); a completion-popover replacement for ghost text, an M-x
      dropdown, and code-action lists are all still squeezed into the one-row
      `EchoArea`; `terminal/*` tool-call support and `elicitation/create` structured
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

- [ ] **Documentation framework** — man page(s), PDF, and a web page generated from one
      shared source (pandoc is the direct fit), mining the `ned/*` binding doc strings
      already passed to `Register<Fn>`. A build/release-time step, nothing ned does at
      runtime.
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
- [ ] **Completion UX: ghost-text vs. popup.** Current `lsp-completion`/`M-n`/`M-p`
      cycling is a deliberate Emacs-flavored choice (one inline suggestion, cycle to the
      next), not a gap in the ghost-text implementation itself. A ranked popup menu with
      inline type/doc info alongside each candidate is the VSCode/Zed norm and a real
      alternative worth naming — pulls against "Emacs-class parity," toward "terminal
      Zed." A direction to decide, not a bug to fix.
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
