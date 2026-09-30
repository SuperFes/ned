# MAYBE

Ideas that are neither committed nor rejected, and ideas that were rejected. Every
Maybelist entry names the trigger that would promote it to `TODO.md`. Promote or delete
on revisit rather than letting detail accumulate. Won't Do records a rejection and its
reason, so it stays a conscious call.

## Maybelist

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
