# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — the detailed
per-feature design/decision records this file used to carry were pruned 2026-08-20,
2026-08-25, and again in this pass (2026-09-06); full history lives in git
(`git log --follow ROADMAP.md`, `git show <rev>:ROADMAP.md`, or `git log --grep=<slug>`
for a specific feature — most shipped items below name the slug to search for). Current
architecture is documented in `CLAUDE.md`. When an item here ships, replace its entry
with a one-line pointer to the shipping commit (or delete it outright) rather than
writing up what was done — the writeup belongs in the commit message, this file is a
todo list, not an archive. A shipped feature that left behind a genuine gap gets its own
short, standalone `- [ ]` item under the relevant section, not a paragraph of "here's
what shipped" with the gap buried at the end of it.

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

- [ ] **Java & Kotlin bundled language support** (raised 2026-09-03 — user wants broad,
      general-purpose Java support, Kotlin alongside it, and Android development to not
      be painful). A system-installed `libtree-sitter-java.so` (confirmed present on this
      machine, e.g. Gentoo's own package) has no queries because a compiled grammar
      `.so` never carries them — `queries/*.scm` are separate text files that live in the
      grammar's own repo, not in the shared library; this is normal for every grammar,
      not a Java-specific gap. `DynamicGrammar.h`'s `dlopen`/runtime-load path (currently
      the only way to reach a non-bundled grammar via `init.janet`) would still need
      those query files sourced from somewhere, so it doesn't actually save the real
      work. The clean path is instead the same one every one of the ~20 bundled
      languages already takes: a real `ned_add_treesitter_grammar(tree-sitter-java
      https://github.com/tree-sitter/tree-sitter-java.git <tag>)` `FetchContent` entry in
      `CMakeLists.txt` (pulls the grammar's own `queries/highlights.scm` etc. with it,
      compiled in via `ned_embed_treesitter_query`, no system package involved at all —
      same reasoning applies to Kotlin, whose absence from the system's own tree-sitter
      packages is irrelevant to ned's build). A community-maintained
      `tree-sitter-kotlin` grammar exists (verify current maintainer/repo/tag before
      wiring it in — unlike Java's grammar, it isn't the tree-sitter org's own). Once
      grammars are in, "broad support" is the same checklist every bundled language
      gets, applied to two more:
      - `TreeSitterMode`/`TreeSitterModeFromLanguage` one-line `JavaMode()`/`KotlinMode()`
        factories (`Mode.h`'s existing pattern), `*-tags.scm` for the symbol-kind gutter,
        smart-indent queries (`cpp-indents.scm`'s own precedent), `*-tests.scm` for test
        discovery (JUnit 4/5's `@Test` annotation, Kotlin's `kotlin.test`/JUnit-on-Kotlin).
      - LSP: `eclipse.jdt.ls` (jdtls) for Java, `kotlin-language-server` (or Kotlin's
        newer official LSP, verify current recommendation) for Kotlin — both configured
        the ordinary `ned/set-lsp-command` way, nothing bundled/auto-detected (this
        project's own stated policy). `Editor/Lsp/LspRootResolver.cpp`'s per-language
        root-marker table gets `java`/`kotlin` entries: `pom.xml`/`build.gradle`/
        `build.gradle.kts`/`settings.gradle.kts`.
      - Test running: `TestOutputParser.h` already has a `"junit-xml"` parser (Maven
        Surefire/Gradle both emit JUnit XML reports) — Java/Kotlin projects need zero new
        parser work, just `ned/set-test-command`/`ned/set-test-results-file` pointed at
        `mvn test`/`gradle test` and the report path.
      - DAP: `java-debug` (the adapter behind VS Code's own Java debugging, also usable
        standalone) — `ned/set-dap-adapter`/`ned/set-dap-launch`, same as any other
        language, no new `DapManager` work.
      - **Android development** mostly falls out of the above once Java/Kotlin +
        `Editor/Tasks/`'s existing generic task-runner (`ned/set-task-command` pointed at
        `./gradlew ...`) + the already-bundled XML mode (layout files) are in place — no
        Android-specific engineering needed for editing/building/testing an Android
        project. A real gap worth naming separately: `adb logcat` streaming and a
        one-click "install + run on device/emulator" flow have no natural home in
        anything that exists today — worth scoping only if plain shelled-out
        `adb`/`gradlew` tasks prove too manual in practice, not speculatively.

- [ ] **Go bundled language support** (audit finding, 2026-09-06 — Go has no bundled
      mode at all today, unlike every other mainstream language now covered. The only
      existing Go awareness in the tree is `TestOutputParser.h`'s `"go-json"` output
      parser, which works fine with no mode behind it). Same checklist Rust's own
      bundled support just proved out (`git log --grep=rust-bundled-language`):
      `tree-sitter/tree-sitter-go` (the tree-sitter org's own grammar) via
      `ned_add_treesitter_grammar`, a one-line `GoMode()` factory, and
      `go-tags.scm`/`go-folds.scm`/`go-imports.scm`/`go-indents.scm`/`go-tests.scm`
      (check which of the grammar's own `queries/*.scm` are directly reusable the way
      Rust's `highlights.scm`/`tags.scm` were, vs. needing a hand-written local query the
      way Rust's fold/import/indent/test queries did — and re-check for the same
      exact-range tag-duplication bug that surfaced in `tree-sitter-rust`'s own
      `tags.scm`, since it's plausible other grammars share the same upstream pattern).
      `gopls` via `ned/set-lsp-command` plus a `go.mod` entry in `LspRootMarkers`;
      `dlv dap` for DAP. Test running needs zero new parser work — `TestOutputParser.h`'s
      `"go-json"` format already exists, just wire `ned/set-test-command` to
      `go test -json ./...`. Once a real mode exists, Go's basename-only `file:line`
      resolution gap (Editor Ergonomics' test-runner-gaps item, below) is worth
      revisiting too.

- [ ] Go-to-file-at-point resolver: LSP-first resolution (`textDocument/documentLink`,
      e.g. clangd's own `#include` support) — the one item left open from the
      resolver-gaps sweep (Python relative imports, PHP PSR-4, JS/TS dynamic
      `import()`, node_modules `package.json` resolution, and Rust's `mod` declarations
      all closed, see `git log --grep=resolver-gaps`). Needs new request/response
      plumbing and an async-aware call site — `OpenLinkAtPoint` is synchronous today,
      and every other async LSP feature (`RequestDefinition`, `RequestCodeActions`,
      `RequestCodeLenses`) already establishes the fire-request/generation-guarded-
      callback idiom this would need to adopt, so this is a restructuring, not a new
      architecture.

- [ ] **LSP write/read protocol-stall timeout split** (`Transport.h`'s
      `ProtocolStallTimeoutMs()`, currently one 30s value shared by both directions) — a
      stalled *write* no longer blocks the main thread at all now that writes go through
      an async queue (`git log --grep=async-write-queue`), but splitting read vs. write
      timeouts would still be reasonable defense-in-depth: a healthy server should never
      take long just to *accept* a notification, even if a slow read (a large
      workspace-wide rename) legitimately needs more tolerance.

- [ ] Whether Markdown fenced code blocks / Org `#+BEGIN_SRC` blocks should get the same
      real-LSP-sync treatment HTML `<script>`/`<style>` embedded documents already have
      is an open question — spawning a live language server per code fence in an
      ordinary notes file could be noisy for illustrative/incomplete snippets.

- [ ] **LSP multi-root, remainder** (per-buffer root resolution itself shipped, see
      `git log --grep=lsp-multiroot`) — a server that itself supports the LSP
      `workspaceFolders` protocol (one process, multiple folders) is never used that
      way; this client always spawns a separate process per resolved root instead. Also,
      several connection-scoped caches (`semanticTokensLegend_`,
      `onTypeFormattingTriggers_`, `pullDiagnosticsUnsupported_`, `inlayHintsUnsupported_`,
      `codeLensUnsupported_`, `activeProgress_`, the `failedCommands_`/`disconnected*`
      status-latch group) stay keyed by the plain language string rather than the
      per-root connection identity — two *simultaneously running* servers for the same
      language against two different roots can shadow each other's legend/status/
      progress-label (every actual request still routes to the correct per-root
      connection regardless).

- [ ] **Candidate-popup hover-highlight and wheel-scroll** (click-to-activate shipped
      for every fuzzy candidate popup and `lsp-code-action-select`, see
      `git log --grep=listpopup-mouse`) — hover-highlight-on-mouse-move is blocked on
      the same open question Mouse Ergonomics' hover-tooltips item names below (bare
      motion events with no button held aren't confirmed to reach a widget's `OnEvent`
      on this Notcurses backend); wheel-scroll is session-level, not something
      `ListPopup` itself does, since a driving session's `rows` is already a
      pre-truncated window.

### Mouse Ergonomics

Design stance: over SSH/tmux/a bare terminal, mouse support is genuinely unreliable (no
capture semantics, no bare-hover-motion confirmed on this backend, TUI subprocesses
inside `TerminalPanel` don't receive forwarded clicks at all) — so the mouse must never
be the *only* path to a control; every mouse action needs a keyboard equivalent that
already exists or gets added alongside it. That said, "unreliable as the sole path"
doesn't mean "not worth it" — a right-click menu scoped to exactly what's under the
cursor is often faster than keyboarding to a location and running a named command, even
for a keyboard-first user. Treat mouse work as an accelerant layered on existing
commands, never a replacement for them.

Right-click context menus for `BufferView` (content + gutter), `TabBar`, `ProjectSidebar`,
and `VcsPanel` (whole-file/stash scope) are all shipped — see `git log --grep=context-menu`
for the sweep. So are double/triple-click word/line select, gutter click (fold/breakpoint
toggle), middle-click paste (Wayland primary-selection), and click-drag selection in the
terminal-panel scrollback.

- [ ] **Hunk-level stage/unstage/revert via `VcsPanel`'s right-click menu** — the
      shipped context menu covers whole-file/stash operations only; hunk-level ops stay
      keyboard/point-based.
- [ ] **Drag-and-drop from `ProjectSidebar` into a pane** to open a file there
      (dragging already exists for tab reorder, sidebar resize, scrollbar/minimap
      thumb, terminal-panel scrollback selection — this would be a new drag *source*
      distinct from all of those, not a new mechanism).
- [ ] **Hover tooltips** (mouse hover, not click, triggering `lsp-hover`'s content) —
      blocked on whether bare motion events (no button held) reach a widget's `OnEvent`
      at all on this Notcurses backend. Needs a small probe before this is even known
      feasible, not just a wiring task.

### Window Layout

- [ ] **Drag-resize of window splits** — `WindowNode` splits are fixed 50/50 only today
      (`WindowManager.h`'s own documented simplification); no way to drag a split's
      divider, unlike every panel's own divider (`ProjectSidebar`/`VcsPanel`/`AcpPanel`/
      `PanelDock` all support drag-resize).

### Navigation & Search

Full-commit diff view (`*vcs log*` → a commit's whole diff) and multibuffer
auto-collapse-on-build (large excerpt/result sets fold by default, configurable via
`ned/set-multibuffer-auto-collapse-*`) are shipped — see
`git log --grep=full-commit-diff-view`/`--grep=auto-collapse-on-build`.

- [ ] **Multibuffer gaps, remainder**: `project-find-references`' RE2 text-scan
      fallback path (no LSP server running for the buffer) and the real
      `textDocument/references` path both still build one excerpt per match with no
      upper bound on total work done — auto-collapse only degrades *display*
      gracefully, not the search itself. `VisitResultUnderPoint`'s jump-to-source also
      stays line-granularity even though `Buffer::ExcerptRange` already carries the
      exact source byte range that would let it preserve the intra-line column.
- [ ] A real visual side-by-side 3-way merge/diff view. `AutoMerge` auto-resolves the
      common case and drops real `<<<<<<<`/`=======`/`>>>>>>>` conflict markers into the
      buffer for a genuine divergence, but a real conflict is still hand-edited text,
      not a visual diff — see "Merge Conflict Resolution Mode" below, which scopes a
      chord/mouse-driven *resolution* workflow over these same markers without needing
      this visual diff first.
- [ ] Native Vim-mode `]c`/`[c` binding (gitsigns' own convention) for
      `vcs-next-hunk`/`vcs-previous-hunk` — the global `C-c v N`/`P` binding already
      works under Vim mode via the shared keymap-stack fallthrough, so this is polish,
      not a functional gap.

### Editor Ergonomics

Scrollback search/selection/copy, the Vim-mode gaps sweep (jumplist ring, changelist
ring, dot-repeat count override, cross-file `A`-`Z` marks, magic-regex translation,
macro registers), DAP rounds 3-5 (attach mode, hit-count/function/exception breakpoints,
restart-frame, breakpoint-line remapping, debug-console history + scrollback,
disassembly/memory view, cross-restart breakpoint/watch persistence), snippet
variables/choices/transforms/macro-replay, and bundled default snippets are all
shipped — see `git log --grep=<topic>` for each (`terminal-panel-scrollback`,
`jumplist-ring`, `changelist-ring`, `dot-repeat-count-override`, `vim-global-marks`,
`vim-magic-translation`, `vim-macro-register`, `dap-round-3` through `dap-round-5`,
`session-persistence-round-2`, `snippet-expansion-gaps`, `bundled-snippets`).

- [ ] **Multiple terminal tabs/instances in `TerminalPanel`** — one embedded shell at a
      time today; `PanelDock`'s tab strip switches between *different panel types*
      (Terminal/ACP/Debug Console), not between multiple concurrent shells.
- [ ] **Terminal-side mouse forwarding** — clicks/wheel inside `TerminalPanel` are
      consumed by the panel itself (focus, scrollback ring); a TUI subprocess running
      inside it (e.g. `htop`, `vim`) never receives a forwarded mouse event.
- [ ] **OSC 52/title integration inside the embedded terminal** — a program running
      inside `TerminalPanel` that emits its own OSC 52 clipboard/title sequences isn't
      relayed anywhere; unrelated to `Editor/Clipboard.h`'s own OSC 52 *write* path for
      ned's own copy/paste commands, which already works.
- [ ] **DAP gaps, remainder**: data breakpoints (tied to a live variable rather than a
      source line) have no natural entry point yet. Thread-focus reattachment across a
      session restart is deliberately excluded even if revisited — a fresh session has
      entirely new thread IDs, nothing meaningful to reattach it to.
- [ ] **No server/daemon mode** — no `emacsclient`-equivalent; one process per terminal,
      no way to keep a warm process (buffers, LSP connections, undo history) alive and
      attach a new terminal client to it.
- [ ] **Test-runner gaps**: no gutter-click run-this-test; no Go test discovery (no
      bundled Go mode yet — see the new item above; Rust itself now has full discovery
      via `rust-tests.scm`); pytest needs `-v` or junit-xml for per-test pass marks;
      Go's basename-only `file:line` can miss jump-to-source in multi-directory modules.
- [ ] **Nested snippet placeholders' inner stops** (`${1:foo ${2:bar}}` keeps only the
      literal text "foo bar", the inner `$2` tabstop is dropped). Confirmed *not*
      reachable via the tree-sitter fold-depth machinery (`CodeFold.h` re-derives a
      fold's extent from a fresh AST parse on demand; a snippet body has no
      grammar/parser behind it to re-derive anything from). The real blocker: field 2's
      range would need to sit properly *contained inside* field 1's range, and
      `Buffer::SnippetRange`'s relocation/gravity model currently only understands
      "disjoint or adjacent" — true nesting needs new relocation semantics in
      `Text/Buffer.h`, not a parser gap. Not attempted.
- [ ] Hunk unstage matches point against the *cached* staged diff, which drifts when
      unstaged edits exist earlier in the file — exact in the common stage-then-undo
      flow; revisit only if it bites.
- [ ] **Persistent left-side glyph rail for toggling panels** (raised 2026-09-06).
      Divider-double-click-to-collapse was inconsistent across panels — only
      `ProjectSidebar` had it; `VcsPanel` modeled its own collapse on `ProjectSidebar`'s
      convention but never wired the double-click check, and standalone `AcpPanel` had
      no mouse divider-collapse at all — all three now share the same
      `dividerClickPending_`/`kDoubleClickWindow` pattern (see
      `git log --grep=divider-double-click-collapse-gap`). The bigger idea that
      prompted this: `ProjectSidebar` already collapses to a 1-column border strip with
      a glyph hint — generalize that strip into an always-visible, VS Code-style
      "activity bar" holding one glyph per togglable panel (files, VCS, terminal, ACP
      chat, debug console), rather than collapse-to-a-strip being
      `ProjectSidebar`/`VcsPanel`-only chrome. Open design question before starting:
      whether the rail *replaces* `PanelDock`'s own tab strip for the bottom-docked
      panels too, or stays left-side-only for the `ProjectSidebar`/`VcsPanel` pair —
      deciding this up front matters so the rail doesn't become a third parallel "which
      panel is where" bookkeeping system alongside `PanelDock` and `AcpPanel`'s own
      right-dock mode. Not designed in detail yet, just scoped.
- [ ] **`libned` as a real shared library** — `ned_lib` (static today) exists solely so
      `ned_tests` can link real editor code without pulling in `main()`; a static lib
      already does that job. Worth revisiting only if a second real consumer shows up
      (an embedding use case, a separate CLI tool) — would need symbol-visibility
      curation and SONAME/ABI-versioning discipline that don't pay for themselves yet.
- [ ] A friendlier, possibly visual surface for browsing/editing ned's own settings
      beyond hand-writing `init.janet` — real live-editing already exists for themes
      specifically (`save-theme`/`ned/theme-set`); a general settings surface would
      generalize that. Vague, unscoped.
- [ ] **Alternate "modern" keymap (VS Code/JetBrains-style)** — tabled 2026-09-04
      discussion. The itch: Emacs's C-w/M-w/C-y read as arbitrary next to the
      now-universal C-x/C-c/C-v cut/copy/paste convention. Audited and found to be a
      real structural conflict, not a simple rebind: C-x and C-c are Emacs *prefix*
      keys here (C-x owns file/window ops, C-c is this codebase's own mode/user prefix
      with dozens of bindings), and C-v is already bound (scroll-page-down). Retrofitting
      the default keymap would evict all of those, not just rename two keys. C-w/C-y
      also aren't plain cut/paste — they're `KillRing` ops (a ring, not a single slot),
      so a literal rebind needs to keep kill-ring semantics under new trigger keys, not
      just alias them. Right approach if this gets picked back up: a third selectable
      full keymap (`ned/set-keymap-style` or similar: `emacs` default, `vim`, `modern`)
      reusing the existing command set wholesale, the same shape `ned/set-vim-mode`
      already proves out — not a patch on the Emacs default, since that default stays
      load-bearing for `C-c`'s existing feature bindings either way. Not started; no
      keymap table drafted yet.

### VCS Side Panel

Shipped 2026-09-01 (`UI/VcsPanel.h/.cpp`) — see `git log --grep=vcs-side-panel`.

- [ ] Directory-tree rows use indentation only, no box-drawing tree-connector glyphs
      (`ProjectSidebar`'s `├─└─│`) — revisit if the plain-indent tree reads as too flat.

### Merge Conflict Resolution Mode (New Feature)

Recorded 2026-09-06. Today's conflict support is detection-only: `VcsPanel` flags a
conflicted path (`RefreshConflictedPaths`, `text::HasConflictMarkers` over the working
tree file) and jumps to the first marker on open; `save-buffer` separately refuses to
write unresolved `<<<<<<<`/`=======`/`>>>>>>>` markers to disk
(`InteractiveRequest::ConfirmSaveWithConflicts`). There is no resolution UI — a conflict
is still resolved by hand-editing marker text. The ask: a fast, chord-and-mouse-driven
mode for tearing through a conflicted file's hunks, without losing the ability to fall
through to a normal manual edit at any point (never a modal trap).

- [ ] **Conflict hunk model** — parse a buffer's `<<<<<<< ours` / `|||||||` (optional
      diff3 "base" section, if `merge.conflictStyle = diff3` is set) / `=======` /
      `>>>>>>> theirs` runs into a `ConflictHunk{startByte, endByte, oursRange,
      baseRange (optional), theirsRange, resolved}` list, mirroring `Text/ThreeWayMerge.h`'s
      own marker-writing convention in reverse (that file writes markers; this reads them
      back). Lives in `Text/` (pure, buffer-free, unit-testable against crafted marker
      text) alongside `ThreeWayMerge.h` — same layering `HasConflictMarkers` already sits at.
- [ ] **Per-hunk resolution actions** — take-ours / take-theirs / take-both (ours then
      theirs, or theirs then ours) / take-neither (delete the hunk entirely) / keep-base
      (diff3 only), each a pure `Buffer` edit replacing the whole marked region with the
      chosen content, one undo step per action so a wrong pick is a single `undo` away.
      Manual editing inside a still-open hunk (or after a taken resolution) must stay
      completely unblocked — this is an accelerator over hand-editing, never a
      replacement UI that locks the buffer.
- [ ] **Navigation** — `next-conflict-hunk`/`previous-conflict-hunk` commands (jump point
      to the next/previous unresolved hunk in the current buffer, wrapping), the natural
      generalization of `VcsPanel`'s existing "jump to first conflict" affordance.
      Candidate default chords: `C-c C-n`/`C-c C-p` mirroring Org's own next/prev-heading
      feel, or a dedicated `M-n`/`M-p`-under-conflict-mode pair — needs a keymap-collision
      pass the way `AcpPanel`'s `C-c c`/`C-c a` split needed one (see
      `Keymap::AmbiguousBindings()`).
- [ ] **A visual affordance for "which hunk is under point"** — at minimum, a themed
      background tint over the ours/theirs/base spans while a hunk is unresolved
      (`Theme` already has this shape for isearch matches/selection/snippet fields —
      same mechanism, a new `Theme::conflictOursBackground`/`conflictTheirsBackground`
      pair). A per-hunk inline action row (take-ours/take-theirs/take-both/take-neither
      as clickable text, `BufferView`'s existing gutter-click precedent) is the natural
      mouse-driven fast path a keyboard-chord-only design would be missing; scope as a
      follow-up once the pure model + chords land, not required for v1.
- [ ] **Auto-entry** — opening a buffer whose on-disk content has conflict markers (or
      `VcsPanel` jumping to one) should offer to enter this mode automatically, the same
      spirit as `AutoRevert`/`AutoMerge`'s existing "detect the condition, offer the
      fix" sweeps — but user-confirmed, not silent, since it changes buffer content.
      `VcsPanel`'s existing `conflictedPaths_` set / `RefreshConflictedPaths` sweep is
      the natural detection source to hook rather than inventing a second scanner.
- [ ] **Whole-file / whole-hunk-run bulk actions** — "take all ours"/"take all theirs"
      for a file with many mechanically-identical hunks (e.g. a lockfile or generated
      file where one side is always right) — a `VcsPanel` per-file action, not a
      per-hunk one; scope after per-hunk resolution ships and only if real usage shows
      the per-hunk loop is too slow for that case.
- [ ] Out of scope for v1: rebase/cherry-pick conflict *sequences* (resolve, `git rebase
      --continue`, repeat) — the hunk-resolution primitive above is what such a sequence
      would be built on later, but driving the sequence itself needs its own `VcsRunner`
      plumbing (`RequestRebaseContinue`/abort/skip) not touched here.

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

Local-only slice shipped (`Editor/ProjectRegistry.h`, `switch-project`/`open-project`
on `C-c P s`/`C-c P o`, `Editor/TerminalTabLauncher.h` for opening a picked project in a
new terminal tab — tmux, screen, Konsole, WezTerm, Ghostty, kitty live-verified; GNOME
Terminal shipped but unverified) — see `git log --grep=named-projects`.

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

Interrupt/spinner, thought/text split, streaming debounce, collapsed tool-call lines,
composer word-motion/history, minimize/resize, auto-reconnect, transcript/composer
word-wrap, checkpoint/rewind, lightweight Markdown rendering, @-mention file
autocomplete, and the tabbed `PanelDock` bottom-dock overlay (Terminal/ACP/Debug
Console sharing one tab strip, close/maximize/resize-drag promoted to the dock) are all
shipped — see `git log --grep=ACP`/`--grep=panel-dock` for the history.

- [ ] **AI-assisted editing (ACP) gaps** (validated live 2026-08-26 against Claude
      Code's own ACP adapter): no scrollback in the panel; a real diff view (actual +/-
      lines, not just a line-count delta) has no reusable line-diff utility yet
      (`ThreeWayMerge.h`'s LCS diff is a private implementation detail); `terminal/*`
      tool-call support and `elicitation/create` structured forms are undeclared as
      client capabilities; no multiple concurrent agents/sessions (still one at a time,
      `Dap/`'s own precedent); no `session/load` history replay;
      `session/set_config_option`/`session/set_mode` aren't surfaced to the user; no MCP
      server passthrough (`session/new`'s `mcpServers` is always `[]` — see the
      tool-bridge item below for what that would actually unlock); no per-agent
      environment-variable override (`ChildProcess`'s `posix_spawn` always forwards the
      parent's global `environ`); no per-agent "character" (display-name/accent color).
      Separately: `Keymap::AmbiguousBindings()` is diagnostic-only (a
      `CommandsTest.cpp` regression test), not enforcement — `Keymap::Bind` still lets a
      caller construct an unreachable-by-typing binding; a real structural fix (Emacs'
      own `define-key` semantics: reject/restructure a bind that would shadow an
      existing command) would change `Bind`'s signature across every call site
      including `ned/define-key`.
- [ ] **Known rough edge**: a right-docked `AcpPanel`'s resize handle has no visually
      reserved border the way `ProjectSidebar`'s divider column does (right-dock mode
      stays a fully separate, byte-for-byte-unchanged standalone overlay from the
      `PanelDock`-hosted bottom-dock mode).

- [ ] **ACP MCP tool-server bridge** (raised 2026-09-06 — the single highest-leverage
      lever on this whole list). Today the agent's only structured capability is
      `fs.readTextFile`/`writeTextFile`; everything else it does, it either can't do or
      has to infer/shell out for blind to ned's own state. `session/new` already has
      an `mcpServers` field for exactly this and ned always sends `[]`. A small
      ned-hosted MCP server exposing `ned_lib`'s own operations as structured tools
      turns every item below from "teach the agent to shell out correctly" into "add
      one more tool definition":
      - **LSP**: `goto_definition`, `find_references`, `rename_symbol`, `code_actions`,
        `hover`, `workspace_symbols`, `format_buffer`, `get_diagnostics`. Thin wrappers
        over requests `LspManager` already exposes.
      - **DAP** (the flagship case — see the dedicated item just below).
      - **VCS**: `git_status`, `git_diff`, `stage`/`unstage`, `commit`, `branch_list`/
        `switch`, `blame(file, line)` — thin wrappers over `VcsRunner`.
      - **Tasks/TestRun**: `run_tests(filter?)`, `get_test_results()` (the structured
        `TestResult`, not raw stdout), `rerun_failed` — lets the agent drive its own
        fix→test→fix loop instead of being told the results in prose.
      - **Diagnostics/Sanitizer/Valgrind/Massif**: `get_diagnostics_log(category?)`
        surfacing the already-parsed structured findings from `SanitizerOutputParser`/
        `ValgrindOutputParser`/`MassifOutputParser` — currently produced only for a
        human reading `DiagnosticsLog`, wasted on an agent that would otherwise have to
        re-parse raw tool output from a shell command.
      - **Project**: `search(pattern)`, `find_file(query)`, `list_todos()` (Org agenda
        scan) — respects `.gitignore`/binary-detection the way a blind `grep` wouldn't.
      - **Org**: `capture_note(template, text)`, `clock_in`/`clock_out` — lets the agent
        journal its own actions into the user's existing Org workflow.
      - **Navigation**: a `goto(file, line)` tool so the agent can point the human's
        cursor somewhere directly, instead of only describing a location in prose.
      Needs a decision on transport (an in-process call surface vs. a real local MCP
      server the agent connects to over the `mcpServers` field, the more
      spec-faithful option) before any individual tool gets built.
- [ ] **DAP↔ACP debugging bridge** (raised 2026-09-06, the concrete flagship use of the
      tool-bridge above). Structured tools (`dap_set_breakpoint`, `dap_continue`/
      `step_over`/`step_into`/`step_out`, `dap_get_stack_trace`,
      `dap_get_variables(frame)`, `dap_evaluate(expr)`) would let the agent act as a
      real pair-debugger — "set a breakpoint at line 42 and tell me what `x` is when we
      hit it" actually happens against the live session, with the agent reasoning over
      real runtime state (`RequestVariables`, watch-history, the pointer-graph view's
      cycle-safe traversal — all already structured, not scraped from a hex dump)
      rather than guessing from source alone. Separately, **debug-session context
      injection**: a one-click "ask agent about this state" from a stopped breakpoint
      that ships the real stack/variables into the prompt, the same shape the
      diagnostic/test-failure quick actions below use.
- [ ] **ACP context auto-attach** (raised 2026-09-06) — `AcpManager::SendPrompt` sends
      exactly one plain `{"type": "text", ...}` block today; there's no resource
      attachment of any kind, even for the shipped `@`-file-mention (which just inlines
      a path as text). Two related wins:
      - **Auto-attach current buffer + selection** as a resource block on every prompt
        (or a manual `@buffer`/`@selection`, the same shape `@`-file-mention already
        proves out) — stop re-explaining what's on screen every message.
      - **One-click "ask agent" from a diagnostic/test-failure/sanitizer-log line** —
        pre-fills a structured prompt (location + message + surrounding source) and
        fires `SendPrompt` directly from `DiagnosticsLog`/`TestResultsBuffer`, instead
        of manual copy-paste into the composer.
- [ ] **Diff preview before an agent edit's permission grant** — `session/request_
      permission` is a bare y/n today; showing the actual diff first needs a reusable
      line-diff utility (`ThreeWayMerge.h`'s LCS diff is currently a private
      implementation detail, per the gaps bullet above). Worth building alongside
      "Merge Conflict Resolution Mode"'s per-hunk take/reject UI (above) rather than as
      a second bespoke widget — reviewing an agent's proposed edit and resolving a
      merge conflict are the same interaction shape (a hunk, shown, accepted or
      rejected).
- [ ] **Prose-check the ACP composer** (raised 2026-09-06, cheap and independent of
      everything else here) — `ProseChecker` (harper-ls) is already wired generically
      as a diagnostics-only LSP connection keyed by `kProseLanguageKey`; pointing it at
      the `AcpPanel` composer's `MinibufferPrompt` text live (spelling/grammar
      squiggles before hitting Enter) needs no new subsystem, just feeding it a second
      piece of text.
- [ ] **Real-time collaborative editing** (CRDT-based) — the biggest lift in this file;
      last.
- [ ] VCS: "generalize the two-callback plugin shape past version control" (cloud CLIs,
      Terraform, Docker) remains an open idea, not a plan.

Explicitly *not* pulled from prior research: OpenCode's session-sharing (needs a hosted
backend, out of scope for a local-first editor) and a unified command palette (a stated
non-goal, see below).

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
- [ ] **Cookbook entries for debugger-adjacent tools that already work with zero new
      code** (audit finding, 2026-09-06 — a documentation gap, not a code gap):
      Valgrind (`valgrind --vgdb=yes --vgdb-error=0` + DAP `Attach`, memcheck errors
      arrive as ordinary `stopped` events) and Docker/embedded/OpenOCD targets
      (`Attach`'s adapter/config is opaque argv + JSON, so `cortex-debug`-style or
      Docker-aware adapters already work via `ned/set-dap-adapter`/`ned/set-dap-attach`)
      both need a worked example in the docs, not new `DapManager` code.

### Known Test Flakiness / Non-Critical Issues (Watch List)

Real, reproduced, non-urgent — each is safe to leave as-is for now, but worth fixing
opportunistically rather than re-discovering from scratch. Add to this list instead of
just fixing-and-forgetting or letting it fade from memory between sessions. Fixed entries
are removed once shipped rather than kept as a writeup here — see `git log --grep=flak`
for closed-issue history.

- [ ] **`PerformanceTest.cpp`'s huge-buffer point-navigation test under
      `build-sanitize`** (found 2026-09-03, unrelated to whatever change was in flight
      at the time — reproduces on a clean stash of the tree too): "Point navigation
      across a huge (piece-table-backed) buffer stays fast" fails its `< 500ms`
      threshold under the ASan/UBSan configuration specifically (~3.2-3.5s observed,
      consistently, across repeated runs), passing fine under the plain `build` preset.
      Likely just sanitizer instrumentation overhead on a tight wall-clock threshold
      rather than a real regression — the threshold was presumably tuned against an
      uninstrumented build. Not reproduced under plain `ctest -j8`/`build`.

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
- **LSP broker self-staleness detection** (`Editor/Lsp/LspBrokerMain.cpp`'s executable-
  identity check) is Linux-specific (`/proc/self/exe`, and depends on rename-over-a-
  running-binary being legal at all — the exact thing that lets a rebuild replace
  `build/ned` while the broker daemon still has the old inode mapped). Windows
  generally can't do that swap in the first place — the OS locks a running executable's
  file, so a rebuild while the broker is up would fail outright rather than silently
  going stale. A native port needs a different mechanism entirely (or may not need one,
  if Windows' own lock makes the failure mode "rebuild fails with a clear error"
  instead of "silent staleness").

Unscoped beyond this sketch — process spawning is the obvious dependency root; nothing
else works without it.

## Maybelist (Speculative — Neither Committed nor Rejected)

Ideas worth remembering but not worth scoping yet — too undecided for "Open Items",
not disliked enough for "Won't do". Promote or delete on revisit rather than letting
these accumulate detail in place.

- [ ] **Merge-aware cross-session undo** — persistent undo (`Editor/PersistentUndo.h`,
      shipped 2026-08-25) content-gates: on reopen, restores the full tree only if the
      file's current on-disk content exactly matches some node already in the persisted
      tree (any node, not just the tip — covers "quit without saving" for free); no
      match at all just discards the persisted history outright and the buffer starts
      fresh. The fancier version would three-way-merge a genuinely novel external change
      into the persisted history instead of discarding it (base = last-persisted content,
      ours = tree tip, theirs = fresh disk content), reusing `Text/ThreeWayMerge.h`/
      `Editor/AutoMerge.h`'s existing machinery to splice one merge node onto the old tip.
      Deferred because it means synthesizing an undo node for content the user never
      actually typed — a real risk of `undo` doing something surprising later — worth it
      only if the content-gate default proves too lossy in practice.
- [ ] **AI edit-prediction** (Zed's Zeta: predicting the next multi-line edit from
      cursor/edit history, distinct from LSP-driven completion or ACP's chat) has no
      equivalent here. A different feature from everything `Acp/` already provides, and
      probably needs some model-serving backend of its own — worth naming as a conscious
      gap rather than assuming ACP already covers "AI in the editor."
- [ ] **Open-source game-dev platform support** (raised 2026-09-03, explicitly a list to
      pick from, not a commitment to any of it). The real fork in each candidate is
      whether it needs a *new base language* ned doesn't speak yet, or whether it rides
      on one already planned above — worth deciding by language, not by engine:
      - **Godot** (GDScript, optionally C# via Mono) — the most popular open-source
        engine, so the strongest adoption case. Needs a GDScript tree-sitter grammar (a
        community one exists; verify current maintainer/repo before wiring it in, same
        caveat as Kotlin's above) plus the same highlight/fold/indent/tags checklist.
        Real open question, not yet verified live: Godot 4's built-in GDScript language
        server reportedly speaks LSP over a TCP socket to a *running Godot editor
        instance*, not as a spawned stdio subprocess — if true, that's a genuine
        mismatch with `Lsp/Transport.h`'s subprocess-+-pipes assumption and would need a
        real transport-layer addition (a raw-socket `Transport`), not just a
        `ned/set-lsp-command` entry. Confirm before scoping.
      - **Bevy** (Rust, no visual editor — code-first ECS) — needs nothing
        Bevy-specific; general Rust language support shipped 2026-09-04, so this is now
        just `rust-analyzer` + `lldb-dap`/`codelldb` config, the same as any other
        already-bundled language.
      - **LÖVE (Love2D)** and **Defold** (both Lua-based, open source, no bundled mode
        for Lua at all today) — gated on general Lua support, not engine-specific work.
        `lua-language-server` already understands both frameworks' APIs via
        community-maintained meta/addon files.
      - **C#** (needed for Godot-via-Mono) — no bundled mode today; `OmniSharp`/
        `csharp-ls` are the LSP options.
      - **GDevelop** — event-based, largely no-code; not a natural fit for a text editor
        regardless of open-source status. Listed only to record it was considered and
        set aside.
      - **Game-dev debugging**: Bevy (Rust) and LÖVE/Defold (Lua, via
        `local-lua-debugger-vscode`) both debug through the ordinary
        `ned/set-dap-adapter` mechanism once their language support lands, nothing
        game-specific needed; Godot/GDScript is the one real unknown — verify whether
        Godot 4 exposes a real DAP-speaking debug adapter at all before assuming this is
        just a config entry (may share the LSP transport quirk noted above).
        Collaboration/multi-client synced debugging (a bespoke sketch-annotation,
        synced-viewing feature seen in some existing GDB frontends) doesn't fit ned's
        single-user terminal model — considered and set aside, not planned.
- [ ] **LSP broker "server mode"** (raised 2026-09-06, following the fileOperations
      capabilities fix and its live fallout) — today's `Editor/Lsp/LspBroker*` daemon
      always self-terminates ~1 minute after its last attached client disconnects
      (`LspBrokerMain.cpp`'s `kWholeDaemonIdleTimeout`), specifically so a stale process
      never outlives a `ned` binary rebuild for long: `BrokerRouter` caches one real
      `initialize` handshake result — success *or* failure — per `(root, language)` key
      for its own process lifetime, and a live bug showed this can otherwise strand every
      future attacher on a failure cached from a client-capabilities bug that was already
      fixed and rebuilt. A real "server mode" (deliberately kept warm regardless of
      client presence — e.g. a systemd user service, so a fresh `ned` launch never pays
      even the broker's own startup cost) would disable or greatly lengthen that idle
      timeout, which reopens exactly this staleness risk on a much longer timescale.
      Needed alongside it: the daemon noticing its own on-disk executable has changed
      (mtime/inode of `/proc/self/exe`, checked on the same periodic sweep this idle
      timeout already uses) and restarting itself — without that, a "keep warm forever"
      daemon would never pick up a rebuilt binary's fix on its own, the identical bug in
      a longer-lived package. Not scoped further than that; no server-mode design exists
      yet.
- [ ] **Code coverage gutter** (audit finding, 2026-09-06). Parse `gcov`/`lcov .info`
      (and, for the sanitizer-adjacent case, `llvm-cov`) output into a per-line
      covered/uncovered/partial marker, rendered as a new gutter column the same way the
      existing blame/diagnostic/symbol-kind/test-status gutters already work
      (`BufferView`'s established data-driven-gutter pattern — no new rendering
      mechanism needed, just a parser and a data source). A natural pairing with the
      sanitizer/Valgrind-XML/massif output parsers already wired into
      `DiagnosticsLog`/`TestRunner`, and a common request in an editor with this much
      test/debug tooling already built out.
- [ ] **A Janet REPL / scratch-eval buffer** (audit finding, 2026-09-06). Emacs'
      `ielm`/SLIME-style "evaluate an expression, see the result inline or in a
      transcript" has no equivalent here — `Environment::DoFile`/`ned/register-command`
      are the only ways to run Janet code today (load a whole file, or bind it to a
      command first). A live buffer for iteratively evaluating Janet expressions against
      the running editor's own environment would be a natural companion to the existing
      `ScratchPad.h` notes feature, and would make ned's own "the editor is a
      Janet-scriptable environment" pitch (see Vision, above) much more discoverable for
      someone writing `init.janet` for the first time.

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
