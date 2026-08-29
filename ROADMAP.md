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
- [ ] **LSP request coverage is narrow** (2026-08-25 audit; `declaration`/`typeDefinition`/
      `implementation`/`signatureHelp` closed 2026-08-26; `references`/`documentSymbol`/
      `workspace/symbol` plus a capabilities-object/completion-context hygiene pass closed
      2026-08-26; `documentHighlight`, `signatureHelp` auto-trigger, and
      `documentFormatting`/`rangeFormatting` closed 2026-08-26 — see below). Only
      hover/completion/codeAction(+resolve)/definition/declaration/typeDefinition/
      implementation/references/documentSymbol/workspace-symbol/rename/switchSourceHeader/
      executeCommand/signatureHelp/documentHighlight/formatting/rangeFormatting are ever
      sent. Still missing: `semanticTokens` full/delta/range (highlighting stays
      tree-sitter-only, never server-informed — matters where tree-sitter can't
      disambiguate, e.g. C++ template vs. less-than); `inlayHint`; `codeLens`;
      `callHierarchy`/`typeHierarchy`; and `onTypeFormatting`. Pull diagnostics
      (`textDocument/diagnostic`) also unsupported — harmless while every configured server
      pushes, a real gap only if one doesn't.
      2026-08-26 (closed same day it was flagged): `documentHighlight` — live-on-
      cursor-move (debounced via the same `LspCompletionDebounceMs()` completion already
      uses) plus a manual `lsp-document-highlight` command (M-x only), both driving
      `BufferView`'s own ephemeral `documentHighlight_` state (not `Buffer::Diagnostics()`'
      persistent shape — closer to ghost-text completion's lifecycle) rendered via a new
      `Theme::documentHighlightBackground` overlay, below selection/isearch/snippet-field
      in `Paint()`'s brush-priority chain but above the execution-line/multibuffer/
      trailing-whitespace washes. No on/off toggle — read-only decoration with no editing-
      flow risk unlike auto-trigger completion/signature-help. `signatureHelp` auto-trigger
      — fires on typing `(`/`,` (same debounce, gated by a new
      `LspSignatureHelpAutoTriggerEnabled()` toggle, default true) via
      `MaybeScheduleSignatureHelp`/`RequestSignatureHelpAtPoint`, writing into the shared
      `statusMessage_` the same way `lsp-hover`/manual `lsp-signature-help` already do —
      coexists with ghost-text completion (disjoint UI surfaces, inline vs. echo area), no
      mutual suppression. `documentFormatting`/`rangeFormatting` — `LspManager::
      RequestFormatting`/`RequestRangeFormatting` (bare `TextEdit[] | null` response,
      `ExtractFormattingEdits`; `rangeFormatting` added for API symmetry but not wired into
      any command yet — `save-buffer`/`format-buffer` are whole-buffer operations already).
      `save-buffer`/`save-buffer-force` defer to a new async `BufferView::
      RequestLspFormatThenSaveBuffer` (via `CommandContext::deferSaveForLspFormat`) only
      when a new opt-in toggle (`ned/set-lsp-format-on-save`, default **false** — silently
      changing existing installations' save behavior would be a real surprise) is on, no
      external `FormatCommand()` is configured (that always wins unconditionally — the
      more specific, deliberately hand-configured choice), and a server is actually running
      for the buffer's language; `FormattingOptions` is fixed (`tabSize: TabWidth()`,
      `insertSpaces: true` — this codebase has no per-buffer tabs-vs-spaces concept to
      source them from yet). Found and fixed in the same pass: `BufferView.cpp`'s
      anonymous-namespace `ApplyWorkspaceTextEdits` (shared by `ApplyCodeAction`/
      `ApplyRename`, now also this) never wrapped its apply loop in
      `Buffer::BeginUndoGroup`/`EndUndoGroup` — invisible for rename/code-actions' usual
      handful of edits, but would have made undoing a whole-document format take one
      keypress per edit; now one `Buffer::Undo()` reverts a full format. All three
      tmux+clangd-verified live (documentHighlight's occurrence tracking across edits/
      motion; signature-help appearing after `(` with no manual invocation; a real
      badly-formatted file reformatted on save both on disk and in the buffer, one `C-_`
      restoring it, and the external-formatter-precedence case confirmed by configuring
      both and observing the external one win).
      2026-08-26 (closed same day it was flagged): the direct-subprocess
      `LspClient` path (as opposed to a broker-owned connection) previously had no
      graceful `shutdown`/`exit` handshake at editor exit at all — it just let the child
      get killed by `ChildProcess`'s destructor. `LspManager::Shutdown()` (called once from
      `main.cpp`'s post-`Run()` sequence, before local teardown) now sends real
      `"shutdown"`+`"exit"` frames to every *directly-spawned* client, mirroring
      `LspBroker::Shutdown()`'s own fire-both-frames-without-waiting-for-the-response
      pattern exactly (there is no live `EventLoop::Run()` left at that point to wait
      with — `Transport::WriteFrame`'s own bounded stall timeout is what keeps this from
      hanging, and `ChildProcess::~ChildProcess()`'s existing close-stdin/poll/SIGKILL
      escalation is still what actually bounds the wait for the process to exit, unchanged
      by this addition). A broker-backed client (`LspManager::brokerBackedLanguages_`,
      stamped in `ClientForLanguage` at the one call site that already knows the
      distinction) is correctly *never* sent shutdown/exit — that server is shared with
      other `ned` processes and the broker daemon itself, and must outlive this one.
      tmux-verified live both ways: a direct-spawned clangd was confirmed as a real child
      process (`pstree`) that exits promptly on `C-x C-c` with no hang; a broker-backed
      clangd (parented to the broker daemon) was confirmed to keep running, untouched,
      after `ned` exited normally.
      2026-08-26: `find-references`/`lsp-find-references` — `project-find-references`
      (`M-?`) now tries a real `textDocument/references` first when a language server is
      running for the buffer (`LspManager::RequestReferences`), falling back to the
      original RE2 text scan only when none is (mirrors `SwitchHeaderSource`'s own
      "LSP is a nice-to-have accelerant, not the only path" precedent — unlike
      `lsp-goto-definition`/etc., which refuse outright with no server). `lsp-goto-symbol`
      (`M-g i`, real Emacs' own `imenu` binding) sends `textDocument/documentSymbol` and
      opens a `project-find-file`-style fuzzy picker over the results.
      `lsp-workspace-symbol` (`C-c l w`) sends `workspace/symbol`, live-re-querying
      (debounced via the same `LspCompletionDebounceMs()` ghost-text completion already
      uses) as the query is typed, since the server does its own matching rather than
      handing back a full list to filter locally. Both share `LspContent.h`'s
      `ExtractSymbols`, which parses all three response shapes the spec allows
      (hierarchical `DocumentSymbol[]`, flat `SymbolInformation[]`, and 3.17's
      range-optional `WorkspaceSymbol[]`) uniformly.
      Also 2026-08-26: `lsp-goto-declaration`/`lsp-goto-type-definition`/
      `lsp-goto-implementation` gained default bindings (`C-c l d`/`C-c l t`/`C-c l i`,
      previously M-x-only); `BuildInitializeParams`' advertised capabilities object
      previously declared only `completion`/`codeAction`/`window.workDoneProgress` despite
      this client sending/handling hover/definition/declaration/typeDefinition/
      implementation/references/rename/signatureHelp/publishDiagnostics and
      `workspace/configuration`/`workspace/executeCommand` — all now declared too (harmless
      against clangd's own permissive handling either way, confirmed before and after, but
      a capability-strict server is entitled to assume otherwise); `textDocument/completion`
      requests now carry `context: {triggerKind: 1}` (every call site here is a manual/
      explicit trigger, never a tracked trigger character).
- [ ] **LSP edit-application gaps** (2026-08-25 audit) — `ApplyCodeAction`
      (`BufferView.cpp`) outright refuses any code action whose edit touches more than
      one file, unlike rename, which does apply multi-file edits correctly; rename
      itself only applies the `changes`-map response form and silently drops a
      `documentChanges`-only response (`LspContent.cpp`'s `touchesUnsupportedForm`); a
      code action that triggers a server-side `workspace/applyEdit` push (rather than
      `workspace/executeCommand`) has no client handler at all — nothing ned wires up
      needs this yet, but it'll break silently the day something does.
- [ ] `rename-project-path` never tells an open LSP server about the rename (no
      `prepareRename`, `linkedEditingRange`, or `workspace/willRenameFiles`/
      `didRenameFiles`) — import paths elsewhere go stale until the server notices on
      its own.
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

### Large files

- [ ] **Windowed/paged editing for genuinely huge files** (multi-GB). `Rope` and
      everything built on it assumes fully-resident content. Recommended v1 shape: a
      read-only mmap-backed viewer past a size threshold, lazily building a line-offset
      index only near the viewport, with an explicit "load fully to edit" step reusing
      the async loader. In-place windowed *editing* needs a disk-backed piece table —
      most of a new engine, explicitly not v1.
- [ ] Buffers restored by a project session open before the async-loader hook is wired,
      so a huge file inside a restored session still loads synchronously at startup.

### Editor ergonomics

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

### Jupyter Notebooks

Maybe it would be really cool to be a complete Jupyter Notebook project tool, could
handle and be used to interally to handle Jupyter Notebooks automatically.  Python
installs and everything.

### Remote development (SSH remote editing)

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

### Documentation & companion tooling

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

### Known test flakiness / non-critical issues (watch list)

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

### Named non-goals (leaning "won't do", kept visible so it's a conscious call)

- [ ] A plugin marketplace/package registry (VSCode extensions, MELPA/straight.el).
      Ned's model is one Janet-scriptable environment plus opt-in project-local plugins
      gated by `ProjectTrust`'s hash-based trust registry — a marketplace implies a
      supply-chain-trust problem this project has deliberately stayed out of.
- [ ] A single fuzzy command palette unifying M-x/find-file/switch-buffer into one popup
      (VSCode/Sublime's Cmd+Shift+P). Real Emacs keeps these as separate, purpose-built
      commands with their own bindings — consistent with this project's Emacs-class-
      parity vision, so this reads as a different, already-chosen philosophy.

### Native Windows port (idea, unstarted — design sketch only)

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

## Won't do (at least not soon)

- **Org Babel** — subsystem-sized, and arbitrary code execution triggered by opening a
  text file: a security surface to design around deliberately, not bolt on.
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

## Notes for whoever builds next

- Build/test: `cmake --preset default && cmake --build build`, then
  `ctest --test-dir build`. Sanitizer opt-in: `-DNED_ENABLE_SANITIZERS=ON` with
  `-DCMAKE_BUILD_TYPE=Debug` — the suite is expected clean; a finding is a real bug.
- When you finish an item above, delete it (or replace it with a one-line pointer) in
  the same commit — don't leave a `[x]` writeup behind. Keeping this file short is the
  point.
