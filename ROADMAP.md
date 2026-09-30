# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — full history lives in
git (`git log --follow ROADMAP.md`, `git show <rev>:ROADMAP.md`, or `git log --grep=<slug>`
for a specific feature). Current architecture is documented in `CLAUDE.md`.

How this file is kept:
- **Open Items** are work we intend to do. When one ships, delete it; the writeup belongs
  in the commit message.
- **Maybelist** is neither committed nor rejected. Every entry names the trigger that
  would promote it. Promote or delete on revisit rather than letting detail accumulate.
- **Won't Do** records a rejection and its reason, so it stays a conscious call.
- A deliberate design cut with no trigger to reopen it is not a todo. It goes in the
  commit message or a source comment, not here.

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
- **The mouse is an accelerant, never the only path.** Over SSH/tmux, mouse support is
  unreliable, so every mouse action needs a keyboard equivalent that already exists or
  lands alongside it.

## Open Items

### Releases

1.0 is a promise about the *Janet surface* (`Register<>` bindings and command names), not
about features. Declaring it makes breaking any of them a major-version event. Criteria:
- [ ] Janet API frozen.
- [ ] No known data-loss paths.
- [ ] Release artifacts.
- [ ] The mdBook site (`Docs/book.toml`) published.

### Correctness

- [ ] **`PathToUri` doesn't percent-encode** (`Lsp/Manager.cpp`), but `UriToPath` decodes.
      A project path containing a space, `#` or `?` sends an invalid URI in every
      `didOpen`. Encode on the way out, with a round-trip test.
- [ ] **Snippet variables inside a placeholder's default** (`${1:$TM_FILENAME}`) don't
      resolve. Only a top-level `$`/`${` reference does.

### Unverified Against Real Environments

- [ ] The six capability-gated DAP requests added with the debug panel (`modules`,
      `loadedSources`, `breakpointLocations`, `stepInTargets`, `setExpression`,
      `completions`) are unit-tested against a fake adapter only. Exercise them against a
      real lldb-dap and debugpy session. `breakpointLocations` must read an empty list as
      "no opinion".
- [ ] The GNOME Terminal handler in `TerminalTabLauncher` has never run live.
- [ ] `C-Tab`/`C-S-Tab` in the modern keymap are untested on a legacy (non-kitty-protocol)
      terminal.

### Editor & UI

- [ ] **Setting for terminal application titles.** An OSC 0/2 title replaces the terminal
      tab's label unconditionally (truncated to 24 columns). Add a `ned/set-*` toggle.
- [ ] **Merge view: index-stage source.** Sides come from conflict markers today, so git's
      clean auto-merges aren't shown. Diff the `:1:`/`:2:`/`:3:` stages (a new Provider
      verb) against the merged buffer into the same `AlignedChunk` list. `DiffLines` is an
      O(m*n) LCS, so this needs a linear-space diff first.
- [ ] **Right-docked `AcpPanel` has no reserved resize border**, unlike
      `ProjectSidebar`'s divider column.

### Refactoring

- [ ] **Consolidate the background-loop + `EventLoop::Post` pattern.**
      `Mcp/BridgeServer` is a full copy of `FramedConnection`'s machinery (jthread read
      loop, `shared_ptr<bool> alive_`, destruction-order comment). `Terminal/PtyProcess`,
      `Tasks/TaskProcess`, `FileWatch` and `ModePrewarm` carry thinner versions.
      `FramedConnection` can't be reused as-is, because it owns one spawned transport by
      value and the bridge accepts N sockets. Extract the per-connection half into
      something the bridge can hold per accepted connection.

### Janet Widget Surface

The tracker panels, buffer list and DAP thread panel now share `UI/TableView.h`. That
table is the evidence this stage was waiting for.
- [ ] **A declarative Janet panel API.** Janet returns a model and C++ paints it, the
      same split `ListPopupModel` and `JanetVcsProvider` already use. Never hand Janet a
      `Canvas`, because per-cell granularity is the wrong place for the scripting
      boundary. The deliverable is a model vocabulary: rows, columns, actions, key
      bindings, and a refresh callback. It falls under the 1.0 freeze the moment it ships
      as `ned/*`.

### Remote Development

Goal: edit files on a remote host over SSH without ned running remotely, comfortably
enough that it doesn't feel like a degraded mode.

**Approach.** Build **(a) client-only** first: shell out to `ssh`/`sftp` per operation,
with no footprint on the remote host. Add **(b) a thin remote agent** only once search
latency, or LSP running against the wrong toolchain, proves it necessary. The agent
would do fast search, change notification, and host LSP/DAP/task/VCS processes against
the remote toolchain. Edit locally and sync on save; don't build a remote-authoritative
buffer.

**File I/O and editing**
- [ ] A remote file I/O seam behind `Buffer::FromFile` and the save path. Rope, undo and
      multi-cursor stay unchanged.
- [ ] Remote semantics for `ExternallyModified`/`AutoRevert`/`AutoMerge`/`FileWatch`:
      a `stat` round trip on the poll tick, with a longer interval over a slow link.
- [ ] `user@host:/path` display everywhere (tab bar, mode line, sidebar, buffer names).
      Project-registry roots accept the same syntax.
- [ ] Failure handling for a connection that drops mid-save/revert/search. Use
      configurable timeouts in the `Editor/ProcessTimeouts.h` shape and show a visible
      failure, never a silent hang.
- [ ] Confirm local and remote buffers mix in one window layout.

**Connect**
- [ ] SSH transport: shell out to real `ssh`/`sftp`, reusing the user's config, agent and
      `known_hosts`. Try `ControlMaster` multiplexing before reaching for libssh.
- [ ] Parse `~/.ssh/config` directly (`Host`/`HostName`/`User`/`Port`/`IdentityFile`/
      `ProxyJump`, wildcards) to fill in defaults.
- [ ] A connect prompt that completes the project name first, then the host, then the
      remote path (lazy `readdir`), all through `FuzzyMatch`.
- [ ] A first-connection host-key trust prompt, following `Project/Trust.h`'s precedent.

**Project tree and search**
- [ ] A lazy, async remote directory listing for the sidebar (`AsyncFileLoader`
      precedent).
- [ ] Remote project search. Start with a remote `rg`, whose semantics differ from ned's
      gitignore/binary rules. The agent is the fix for exact parity. Decide how
      project-replace, find-references and the agenda scan degrade in client-only mode.

**Agent (phase b)**
- [ ] A headless `add_executable` over `ned_lib`. Measured 2026-09-08: it links without
      `-ljanet` and runs real `ProjectSearch` at 8.7 MB, so no library split is needed.
- [ ] A seam to spawn LSP/DAP/task/VCS subprocesses remotely instead of through
      `ChildProcess`, and relay remote inotify. This is the biggest lift in the section.
- [ ] Auto-deploy with a version handshake. Build per architecture, either
      cross-compiled or prebuilt, because the remote host may have no toolchain.

**Client/server protocol** — its first consumer is the agent, and the second is a
daemon/attach mode.
- [ ] Design the protocol. Local execution is the degenerate case: in-process is one
      transport behind the protocol, not a bypass around it. Send operations, not
      buffers. Compress per frame, and negotiate the codec at the handshake with
      "none" as a first-class option; nothing compression-related is linked today.
      Build on `FramedConnection`: timeouts on every blocking call, a non-blocking
      connect, an async write queue, and no lock held across a join. Decide the trust
      model together with the framing, since this is a remote code execution surface.
      Still open: the framing, JSON vs. something denser, streaming and cancellation
      (`$/progress`/`$/cancelRequest` are prior art), versioning, and
      `AF_UNIX`/stdio-over-ssh/TCP.
- [ ] **Daemon/attach mode** (`emacsclient`-equivalent): keep a warm process (buffers,
      LSP connections, undo) and attach a new terminal to it.
- [ ] **Remote debug sessions**, e.g. attaching to live PHP in production.
- [ ] Decide on port forwarding: only the agent's own socket, or also forwarding a
      user's dev-server port back to the local machine.

### Documentation & Companion Tooling

- [ ] **Doxygen developer docs**, themed with Doxygen Awesome, as an opt-in `docs` target
      through `doxygen_add_docs()`. It is not part of `ALL`.
- [ ] **`ned-setup`**: first-run detection of shell integration, language servers and
      debug adapters, generating an editable Janet file loaded from `init.janet`.
      Deliberately a generator rather than silent runtime detection.
- [ ] **Cookbook entries for tools that already work with no new code**: Valgrind
      (`--vgdb=yes --vgdb-error=0` + DAP attach), and Docker/embedded/OpenOCD targets
      through `ned/set-dap-adapter`/`ned/set-dap-attach`.

### Notcurses Patches to Upstream

`Patches/notcurses/` carries each patch as a `git am`-able file (regenerated by
`regenerate.sh`, verified against pristine v3.0.17).
- [ ] `PatchNotcursesNulKey` (Ctrl+Space swallowed as NUL) and `PatchNotcursesMouseWheel`
      (SGR wheel-right misdecoded). Both reproduce on upstream `master` as of 2026-09-06
      and have no existing issue. They are ready to PR.
- [ ] `PatchNotcursesBracketedPaste`, for upstream
      [#2704](https://github.com/dankamongmen/notcurses/issues/2704). Our shape buffers in
      `EventLoop::Run()`, not in Notcurses, so it doesn't match tstack's sketch there.

### Known Issues / Test Flakiness (Watch List)

Real, reproduced, non-urgent. Fix opportunistically; delete when fixed.

- **Org clock and agenda timestamps are UTC.** `TimestampFromTimePoint` and "today"
  (`Org.cpp`, `Project/Agenda.cpp`, the deadline prompt) floor `system_clock` to days.
  Real Org writes local time. A fix must move all of them together, and it shifts
  existing files' clock lines by the UTC offset.
- **F# grammar**: application and infix share `prec-left 16`, so `1 + f a` parses as
  `(1 + f) a`. Also, `let f x y = ()` without a trailing newline doesn't parse as a
  function.
- **Markdown emphasis on a pathological paragraph.** Each unclosed opener reads ahead up
  to 4096 codepoints: 15,000 of them in 45 KB take 0.65 s. A closer-free memo in the
  scanner state would fix it if a real document hits this.
- **`BufferView's highlight cache updates after an edit…` flakes under `--order rand`**
  (about 1 in 10–15 runs). The test's *first* assertion fails: the first `Paint()`
  renders a JSON string as `Default`. It smells like a parse-readiness race
  (`ModePrewarmTest.cpp` precedent) and hasn't been root-caused.
- **Notcurses and the terminal can disagree on glyph width**: VS16 emoji, and Nerd Font
  private-use glyphs under tmux. Nothing has been seen in Konsole. A startup
  cursor-position probe could calibrate an exception table.
- **`search-everywhere debounces a background text search…` flakes under `ctest -j8`**.
  It's a timing margin under contention; pin the debounce to a fake clock if it
  resurfaces.
- **Hunk unstage matches point against the cached staged diff**, which drifts when
  unstaged edits exist earlier in the file. It's exact in the stage-then-undo flow.

## Maybelist (Speculative — Neither Committed nor Rejected)

### Big subsystems

- [ ] **Jupyter notebooks.** Subsystem-sized, comparable to LSP and DAP combined. The
      shape: a ZMQ kernel client (5 sockets, HMAC-SHA256 multipart JSON, discovery via
      `share/jupyter/kernels/*/kernel.json`) on the `FramedConnection` threading model.
      A `NotebookView` composes per-cell `Buffer`s and `Mode`s with output panels between
      them. Rich output reuses `Table.h`, the Notcurses pixel path, and the system
      image decoders `AcpPanel/InlineImages` already links. Phasing: nbformat
      round-trip → a read-only view → a headless kernel client → run cell → the rest.
      Hard rule: opening a notebook never executes anything. ipywidgets is out.
      Justified when notebooks are edited here regularly.
- [ ] **Native Windows port.** ned is POSIX throughout. The work: `ChildProcess` on
      `CreateProcess` (the dependency root, since every LSP/DAP/ACP/task/VCS integration
      sits on it), `PtyProcess` on ConPTY, raw terminal I/O on the Win32 console or VT
      passthrough, Notcurses' own Windows support (check it first), and the broker's
      `/proc/self/exe` staleness check. WSL already works today. Justified by a real
      Windows user.
- [ ] **Real-time collaborative editing** (CRDT). No design exists. Justified when a
      concrete pairing workflow wants it.
- [ ] **Jank replaces Janet.** Embedding works (`Docs/JankFeasibility.md`, 2026-09-08).
      Three things block it: ~237 MB RSS against ned's ~28 MB, a hard dependency on
      Clang/LLVM (~266 MB of shared libraries), and BDWGC thread registration at 39
      thread sites. Re-check upstream's embedded builds before any workaround.
- [ ] **AI edit-prediction** (Zed's Zeta-style next-edit). ACP now specifies `nes/*` over
      `document/*`; claude-agent-acp 0.84 doesn't implement it. Prove the value first with
      an explicit command over ACP. The new work is multi-line inline rendering, and
      `Text/LineDiff.h` plus `UndoTree` already supply the prompt input.

### Performance

- [ ] **Incremental fold/locals/indent passes.** Queries are incremental (`changed-ranges`),
      but the linear work around them isn't: the fold scan (~3 ms at 107 KiB of C++),
      locals-to-nodes (~25 µs/KiB, hence the 256 KiB `kMaxLocalConditionBytes` cap), and
      the indent imprint walk (+21 ms on Enter at 128 KiB of JS). Use reuse keyed on
      `TreeEdit::structuralChanges`, not windowing, since truncated folds are wrong at the
      window edge. Justified when typing on large files feels slow again.
- [ ] **Resolve highlight capture names once per compiled capture.** `std::_Hash_bytes` is
      ~6% of a 128 KiB JS keystroke. The likely cause is string-keyed lookups in Mode.cpp,
      unconfirmed. Justified when highlight is next profiled.
- [ ] **O(1) `parse::NodeParent`.** Needs a per-tree memo, which means shared mutable
      state on a `const` `TreeData` that is shared across threads. Justified by a hot
      single-call site.
- [ ] **`IncrementalParseCache` using `Buffer::Edits()`** instead of diffing a second full
      copy of the document. This changes `Mode`'s pure "full text in" seam. Justified by
      an RSS measurement that shows the doubled text.

### LSP

- [ ] **Quick-fix marker coverage.** Selection-scoped refactors and source actions light
      nothing, and neither do prose (harper-ls) fixes. The latter needs a second viewport
      request on the prose connection. `C-c C-a` reaches all of them at point.
- [ ] **Async server→client requests.** `window/showMessageRequest` always answers null,
      because `Client::RequestHandler` is synchronous. `window/showDocument` drops the
      column and `takeFocus`. Justified by a server whose question matters.
- [ ] **Dynamic registration beyond `didChangeWatchedFiles`/`didSave`.** For example,
      on-type formatting triggers are read from `initialize` only.
- [ ] **Semantic tokens overriding a grammar `comment`.** Decide the policy when a server
      actually recolours one.
- [ ] **New-subdirectory watch latency.** The tree snapshot refreshes every ~30 s. Reacting
      to `IN_ISDIR` creates needs a `mkdir -p` rescan and rename bookkeeping. Justified
      if the delay annoys.
- [ ] **Widening the project-wide problem list.** Three directions: coverage (a crawl or
      headless opens, for quiet servers), cold start (persist the store, which needs
      staleness solved first), and staleness (re-resolve `{line, character}` against the
      file on disk instead of flagging it). Justified when a server in use is that quiet,
      or when the stale flag becomes noise.
- [ ] **LSP broker pre-warming** of recent projects at `ned --foreground`. Blocked on
      argv, because server commands exist only in a live `init.janet`. Needs a
      `(root, language) → argv` state file written by interactive attaches.
- [ ] **Language servers for Markdown fences and Org `#+BEGIN_SRC` blocks**, like HTML's
      embedded documents. Justified when someone wants diagnostics in literate files.
- [ ] **`dap_get_pointer_graph` MCP tool.** First extract
      `BufferView::ExpandPointerGraphNode`'s traversal into a UI-free helper. A column
      for `goto_location` is the same kind of add.

### Editing & UI

- [ ] **Span-aware rows in `ListPopupRow` and `TableView` cells.** This unblocks a
      highlighted search-everywhere preview and coloured table cells.
- [ ] **TableView**: remember sort order across restarts, and let columns be resized or
      reordered by hand. Justified when a panel wants it.
- [ ] **Colour swatches**: `lab()`/`lch()`/`oklab()`/`oklch()` need a gamut-mapping
      policy first, because round-tripping would clip P3 colours. The 8 KiB per-line
      scan cap misses minified CSS; lifting it means windowing on horizontal scroll.
- [ ] **Buffer-word completion for huge buffers**, via a viewport-windowed scan.
- [ ] **Excerpt-scoped search** for `next-error`, dabbrev and Vim `/`. Only isearch and
      query-replace honour `ExcerptBodyRanges` today.
- [ ] **Merge view base pane.** `MergeViewSession` already derives it from diff3 markers.
- [ ] **Merge-conflict chrome with no VCS verdict** falls back to marker text, and `C-c x`
      ignores the verdict. Revisit if a custom provider can't express "unmerged", or if
      "chips gone but `C-c x o` worked" reads as a bug.
- [ ] **Status gutter per-pane frontier.** Today closing a pane never commits, and two
      panes on one log share a frontier.
- [ ] **`describe-bindings` for a mode the pane isn't showing.**
- [ ] **Debugger**: a `Manager`-side variables cache shared by `DebugPanel` and inline
      values; inline values for fields and watches (key on `evaluateName`); and a popup
      for debug-console completion instead of readline-style listing.
- [ ] **Bracketed-paste multi-cursor distribution.** A terminal paste inserts at one point
      only. Justified if multi-cursor editing and paste are used together.
- [ ] **`Keymap::Bind` enforcing reachability.** `AmbiguousBindings()` is test-only
      today. Changes `Bind`'s signature, including `ned/define-key`.
- [ ] **Configurable indicator glyphs** (fold `…`, truncation `»`, indent guide `│`, wrap
      `↳`, no-EOL `¬`): one mutex-guarded module plus `ned/set-glyph`, validated as
      single-width. Justified when someone wants a different character rather than a
      different weight.
- [ ] **A settings surface beyond hand-written `init.janet`**, generalizing the theme
      editor's live editing. Unscoped.
- [ ] **Split BufferView's prompt/session machine out**, taking the LSP pickers with it.
      Extracting `LspFeatures` by protocol was measured and rejected, because most LSP
      features *are* prompts. Justified when `BufferView.h` starts costing time.
- [ ] **Merge-aware cross-session undo.** Three-way-merge a novel on-disk change into the
      persisted tree instead of discarding it. Risky: it synthesizes an undo node the
      user never typed. Justified if the content gate proves too lossy.
- [ ] **Anchors Janet surface.** There's no RAII handle, so a leaking script leaks
      forever. Justified when a plugin wants to mark a position.

### Refactoring tools

- [ ] **Import fixup off the main thread.** It takes ~1.2 s in the worst case (renaming
      `Mode.h`). Justified when a real repository makes the pause visible.
- [ ] **External-move review as a status-line prompt** instead of switching the pane
      unprompted. Justified if branch switches make it grate.
- [ ] **Project-wide watching for external moves.** Today a move is only paired when
      both ends are directories of open buffers.

### Language coverage misses

`Docs/LanguageMatrix.md` is the gap tracker. These are the known misses below its
granularity. Each is justified when one turns up in real use.
- [ ] **Charset edges**: BOM-less UTF-16 gains a BOM on save; a huge UTF-16 file is
      refused; an `.editorconfig` charset stated before `init.janet` disables it has
      already decoded.
- [ ] **Mixed tabs and spaces** (GNU C: 2-column indent levels, 8-column tabs). Needs a
      tab width separate from `IndentStyle::width`.
- [ ] **Locals forms that decline**: Lisp destructuring, fish `set -l -x`/`read -l`,
      use-before-binding in whole-scope languages, Perl lexical `my sub`, Markdown
      reference links.
- [ ] **change-signature misses**: a Kotlin interface listed before the superclass,
      static C# extension calls, Ruby/Crystal bare `super`, unapplied or infix function
      references, and infix Haskell equations.
- [ ] **Imports that name no single file**: C#/F#/Vala namespaces, Elixir multi-alias,
      Ada case mismatches, external Bazel repositories, Scala selector/rename imports,
      Org `[[file:]]` links, CMake variables, include globs, Just `mod name`, Nim
      `pkg/[a, b]`. Also source roots read from the build (Cabal, `elm.json`,
      Maven/Gradle, rebar).
- [ ] **Tests with no name to run by**: D `unittest` without a UDA, MATLAB `%%` sections,
      and judge's top-level `test` forms.
- [ ] **Native formatter parity** with gofmt alignment, gofmt/black redundant parens,
      rustfmt/Prettier width wrapping, rustfmt `where`, JS/shared/`overrides` Prettier
      configs, C#'s finer brace categories, Vala call spacing, perltidy paren tightness,
      the proto blank-line cap, and Scala braced bodies. An external formatter is what
      makes a file canonical.
- [ ] **Code-block definitions in Markdown/Org outlines** (`:injected-symbols`).
- [ ] **Raw `.gcov` output.** Only lcov `.info` is parsed.
- [ ] **Godot**: the grammar is live, but Godot 4's language server reportedly speaks
      LSP over TCP to a running editor, and its debug adapter is unverified. Confirm both
      before scoping a socket `Transport` (`Docs/LanguageCoverage.md`).
- [ ] **Carbon**: watch, don't wire. Pre-0.1; the admission trigger is in
      `Docs/LanguageCoverage.md`.
- [ ] **Android device tooling**: `adb logcat` streaming, and install-and-run on a
      device. Justified when shelled-out `gradlew`/`adb` tasks prove too manual.

### Issue trackers & VCS

- [ ] **A generic async external record source** (`Provider` + `Runner` + registry),
      extracted only if the tracker and VCS copies genuinely agree.
- [ ] **Tracker rough edges**: a panel added from the REPL doesn't appear on the rail
      until restart; two repositories' `#12` share one buffer (qualify as
      `owner/repo#12`); Jira search stops at 100 issues; Jira scoped tokens need the
      `api.atlassian.com` gateway.
- [ ] **Board panels**: GitHub Projects (GraphQL) and Jira agile boards, for when column
      order matters more than status grouping.
- [ ] **Generalize the VCS provider shape** past version control (cloud CLIs, Terraform,
      Docker).

### ACP

- [ ] **Panel extras**: PR-style review comments on an agent's diff fed back as a
      prompt; transcript search and timestamps.
- [ ] **Subagents as child sessions** (`clientCapabilities.subagents`, SDK PR #1992;
      codex-acp sends it). Per-session routing already exists. Justified when an agent in
      use sends it.
- [ ] **Spec surface Claude never exercises**, each justified when an agent in use needs
      it: `terminal/*` (M/L — terminal-per-id lifetime plus a transcript embed),
      `plan_update`/`plan_removed`, `mcp/connect`, audio content, and opencode's
      `todowrite`/exit-code shapes. Gemini CLI and Codex API keys already work through an
      `env` prefix on the agent's argv.

## Won't Do (at Least Not Soon)

- **Org Babel** — arbitrary code execution from opening a text file. If it is ever
  revisited, reuse the Jupyter rich-output renderer.
- **Org table formula engine** and **Org export/publishing** — each is tool-sized.
- **Plugin marketplace/registry** — it implies a supply-chain trust problem. The model is
  Janet plus `ProjectTrust`-gated project plugins.
- **Hosted session sharing** (OpenCode-style) — it needs a backend, and ned is
  local-first.
- **Alt+Click cursor creation** — mouse-only; keyboard multi-cursor covers it.
- **X11 primary selection** — middle-click paste is Wayland-only by preference.
- **Bundling or requiring a JVM.**
- **Screen-reader accessibility** — a raw-cell-grid TUI has no accessibility tree.
- **`C-x` as the Emacs prefix under Vim mode** — it's Vim's decrement. `C-w` and
  `:sp`/`:vs`/`:on` cover window management.
- **Answering OSC 52 clipboard queries from terminal subprocesses** — it would hand the
  clipboard to every subprocess, and xterm's default doesn't answer either.
- **`libned` as a shared library** — the static `ned_lib` serves every consumer,
  including the headless agent.
- **LSP requests measured out** (probe with `Tools/lsp-capability-probe.py --require
  <capability>`, and include dynamic registration):
  - `inlayHint/resolve` — only lua-language-server sets `resolveProvider`. Reopen on a
    server installed here that returns a real tooltip.
  - `workspace/diagnostic` — no installed server advertises it. The problem list
    covers files with no buffer through push diagnostics instead.
  - `textDocument/inlineValue` — phpactor returns only `VariableLookup`, which ned
    already computes natively. Reopen on a server that returns
    `InlineValueEvaluatableExpression`.
  - `foldingRange` and `selectionRange` — ned's own tree does both, offline, for every
    language.
  - `moniker` — no index consumer. `inlineCompletion` — ACP is the answer.
    `notebookDocument/*` — no notebook surface.
  - `willSave`/`willSaveWaitUntil` — ned runs its own format-on-save pipeline.
  - harper-ls rejects a `null` answer to `workspace/configuration` and logs on
    `didSave`. Both are its quirks; revisit only if a second server does the same.

## Notes for Whoever Builds Next

- Build/test: `cmake --preset default && cmake --build build`, then
    `ctest --test-dir build`. Sanitizer opt-in: `-DNED_ENABLE_SANITIZERS=ON` with
    `-DCMAKE_BUILD_TYPE=Debug` — the suite is expected clean; a finding is a real bug.
- `ctest` and `./build/ned_tests` are two genuinely different checks, not a convenience
  pair — run both before calling a change clean. `ctest` gives each case its own process
  (isolation, plus `--timeout N` names a hang instead of wedging the run, and `-j8`
  surfaces cross-process races over shared paths); the single-process binary is the only
  thing that catches global-state bleed between cases, and it runs them back-to-back with
  no per-case process startup in between, which has surfaced timing races `ctest` shows as
  reliably green. Both flavors have been found here, and each mode missed the other's
  (2026-09-08: `ctest -j8` alone caught the protocol-client poll deadlock and never saw
  the LSP frame-count race; the single-process run was the exact inverse). Never run two
  `./build/ned_tests` binaries concurrently — they contend and wedge each other; use
  `ctest` for parallelism.
- `ned_tests` is deliberately hermetic against the terminal and the desktop it runs on,
  via static-initialized guard translation units that flip a process-wide switch before
  any case runs (`Tests/ClipboardTestGuard.cpp` → `SetClipboardEnabled`/`SetOsc52Enabled`,
  `Tests/TerminalOutputTestGuard.cpp` → `ned::ui::SetHeadlessOutputForTesting`,
  `Tests/ProseCheckerTestGuard.cpp`, `Tests/SigpipeTestGuard.cpp`). Anything new that
  writes to the real terminal, the system clipboard, or shells out to a desktop tool
  needs the same treatment — add a switch and a guard rather than fixing it at each call
  site, since a test that legitimately re-enables the feature locally would otherwise
  reintroduce the escape.
