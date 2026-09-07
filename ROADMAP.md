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

Go bundled language support is shipped (`tree-sitter/tree-sitter-go`, `GoMode()`,
highlights/tags reused unmodified from upstream, hand-written folds/indents/tests —
deliberately no import-target query — `CMakeLists.txt`'s own comment explains why a
package-based import path can't be resolved by a
syntax-only query the way Rust's file-per-module `mod foo;` can; `gopls`/`dlv dap`/
`go test -json` are config-only, no new code) — see `git log --grep=go-bundled-language`.
Go's basename-only `file:line` resolution gap is tracked under Test-runner gaps below.

C# bundled language support is shipped too (raised 2026-09-06 — no system
`tree-sitter-csharp` install available, and a system grammar `.so` wouldn't have carried
queries anyway, same Java/Kotlin reasoning above): `tree-sitter/tree-sitter-c-sharp`,
`CSharpMode()`, highlights/tags reused unmodified, hand-written folds/indents/tests
(xUnit `[Fact]`/`[Theory]`, NUnit `[Test]`/`[TestCase]`/`[TestCaseSource]`, MSTest
`[TestMethod]`/`[DataTestMethod]`) — deliberately no import-target query, same
namespace-vs-file reasoning as Go's own. `csproj`/`sln` have no fixed filename (unlike
every other bundled language's root marker), which needed a real, generically reusable
`LspRootResolver.cpp` addition: a marker of the form `"*.<ext>"` now means "any file with
this extension in this directory," not one exact name — see
`MarkerExistsInDirectory`'s own comment there. `OmniSharp`/`csharp-ls` and `netcoredbg`/
`vsdbg` are config-only, no new code. See `git log --grep=csharp-bundled-language`.

Every bundled language in this file comes from `FetchContent` in `CMakeLists.txt`, never
a system package — this was already the standing policy before C# (see the Java/Kotlin
item above, which independently arrived at the same conclusion), not a new one adopted
here. A system-installed grammar `.so` never carries its `queries/*.scm` regardless of
language, so leaning on one was never going to be the shortcut it looks like.

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

Candidate-popup hover-highlight and wheel-scroll are shipped too — see
`git log --grep=listpopup-scroll`. Both go through one new `ListPopup::SetOnScrollBy`
hook that fires a signed row delta (a wheel tick is always ±1; a hover move's delta is
the hovered row minus `model_.selectedIndex`, both indices into the popup's own
already-displayed rows) rather than an absolute target — `BufferView::ScrollCandidatePopup`
replays that many synthetic Up/Down key chords through whichever `HandleXKey` the
current `InputMode` already dispatches real arrow presses through, so none of the dozen
different candidate-list modes (M-x, find-file, switch-to-buffer, VCS branch switch,
`lsp-code-action-select`, workspace-symbol, ...) needed their own per-mode
highlight/scroll logic duplicated. Confirmed live (tmux, raw SGR bytes): hovering a row
moves the selection (and the popup's own visible-window scroll) exactly like pressing
Down/Up would, two wheel ticks move it by exactly two, and click-to-activate still
works unchanged.

### Mouse Ergonomics

Design stance: over SSH/tmux/a bare terminal, mouse support is genuinely unreliable (no
capture semantics, TUI subprocesses inside `TerminalPanel` don't receive forwarded
clicks at all) — so the mouse must never
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

Hover tooltips (mouse hover, not click, triggering `lsp-hover`'s content) are shipped —
see `git log --grep=mouse-hover`. The feasibility question (raised above) resolved to a
real, confirmed mechanism: this installed Notcurses build's own SGR decoder (`in.c`'s
`mouse_click`, `mods % 4 == 3`) hard-codes a bare no-button-held motion report's `evtype`
to `NCTYPE_RELEASE` (the library's own comment calls this "oddly enough"), so
`Event::mouse()` (`Widget.cpp`) decodes a hover move as `MouseEvent{button = None, motion
= Released}`, never `Motion::Moved` — unambiguous versus a real button release (always a
real button id), so `BufferView::MaybeScheduleHover`'s gate on exactly that combination is
the reliable "hovering, nothing held" signal on this backend, colliding with nothing
shipped (every existing `Motion::Released` consumer already gates on its own
drag/resize/repeat flag first). Debounced via `EventLoop::DeadlineTimer` (reusing
`editor::lsp::LspCompletionDebounceMs()`, the signature-help/document-highlight
precedent), dismissed on move-away/keypress/click/buffer-switch (`BufferView::DismissHover`),
rendered into a `ListPopup` in preview-only mode (empty `rows`, `Anchor()`-placed the same
way the completion popup anchors under point, just under the hovered mouse position
instead). One gotcha worth remembering for the next mouse-driven-by-position feature: a
hover-move landing outside `BufferView`'s own `Box_()` (moved into the sidebar/tab bar/
mode line/another pane) needed its own explicit dismiss check — there's no "mouse left me"
event in this codebase, a widget just stops being handed positions outside its box, so
without that check a tooltip could go stale forever once the mouse left the pane without
an intervening keypress. Confirmed live end-to-end against a real `clangd` (tmux smoke
test, raw SGR bytes fed into the pty): hover text renders, updates when the hovered
symbol changes, and dismisses correctly.

Hunk-level stage/unstage/revert via `BufferView`'s own right-click gutter menu are shipped
too — see `git log --grep=hunk-context-menu`. Landed on `BufferView`'s gutter menu rather
than `VcsPanel`'s (which has no per-hunk row to click at all, only whole-file entries) since
hunk staging was already point-based there (`vcs-stage-hunk`/`vcs-unstage-hunk`, `C-c v
h`/`C-c v H`) — right-click at the same point is the natural mouse accelerant, gated (same
v1 simplification the fold/breakpoint/blame gutter rows already use) on `DiffGutterActive()`
alone, not on whether the clicked line specifically has a change. Revert (`vcs-revert-hunk`,
`C-c v x`) is a genuinely new capability, not just a menu wrapper: no hunk-level discard
existed at any layer before this, keyboard included. Required a third `VcsProvider` patch
operation alongside Stage/UnstagePatchArgv (`RevertPatchArgv`: `git apply --reverse
--unidiff-zero`, no `--cached` — discards from the working tree, reading from the same
unstaged diff Stage does) plus a `VcsRunner::RequestHunkRevert`, and — since this discards
uncommitted work with no undo — a new `BufferView` y/n confirmation (`InputMode::
ConfirmRevertHunk`/`InteractiveRequest::ConfirmRevertHunk`, `ConfirmOverwriteSave`'s own
shape) gating it regardless of entry point. No explicit `Buffer::Revert()` call needed after
a successful revert — `AutoRevert`/`FileWatch`'s existing sweep picks up the now-changed
file on its own next tick, the same "unmodified buffer, changed on disk" case those already
handle. Confirmed live end to end against a real git repo with two well-separated `-U0`
hunks (tmux, real SGR right-click + menu-click + 'y'): reverting the hunk at point discarded
only that hunk from the working tree, left the other hunk (and the open buffer, once
auto-reverted) untouched.

Drag-and-drop from `ProjectSidebar` into a pane is shipped too — see
`git log --grep=sidebar-drag-drop`. Mirrors `ProjectSidebar::IsResizing()`/`EndResize()`'s
own cross-widget cooperation shape (`DraggingFilePath()`/`EndFileDrag()`): a left-press on a
file row (never a directory — nothing sensible to open a directory *as*) arms the drag
alongside its existing open-preview behavior, and each `BufferView` checks it ahead of its
own `LocalMouseEvent` gate, exactly like the resize check just above it. A real bug was
caught live in a two-pane split test (tmux, raw SGR bytes): `EndFileDrag()` was originally
called unconditionally by whichever pane's `OnMouseEvent` happened to run first for the
Released event (`Container::OnEvent`'s fixed child order, unrelated to drop position), so
the pane the file was actually dropped on found the drag already cleared and silently
no-opped. Fix: gate the whole branch — not just the open — behind `Box_().Contain()` first,
so only the one pane whose box genuinely contains the drop ever consumes it; a two-`BufferView`
regression test now locks this in. Accepted v1 trade-off, documented at the source: since the
row's own press-time open (unchanged, existing preview behavior) already fires before any
drag is known to be one, a real drag-and-drop also leaves the file open in whichever pane was
already focused, not just the drop target — deliberately not restructured to defer that open
until release, which would have meant moving long-tested click-vs-double-click timing logic
off Pressed and onto Released for its own sake. A drop that lands on neither `ProjectSidebar`
itself nor any pane (the tab bar, VCS panel, echo area) leaves the drag armed until the next
one overwrites it — harmless, since nothing else ever reads it.

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
variables/choices/transforms/macro-replay, bundled default snippets, and multiple
concurrent terminal tabs (any number of embedded shells as uniform, closable `PanelDock`
tabs — `new-terminal` on `C-c C-t` always adds one more alongside whatever's already
open; `toggle-terminal` on `` C-` ``/`C-c t` targets whichever is first, creating one if
none remain; `PanelDock`'s own tab strip gained TabBar-style wheel-scroll + `‹`/`›`
overflow indicators as part of the same work) are all shipped — see `git log
--grep=<topic>` for each (`terminal-panel-scrollback`, `jumplist-ring`, `changelist-ring`,
`dot-repeat-count-override`, `vim-global-marks`, `vim-magic-translation`,
`vim-macro-register`, `dap-round-3` through `dap-round-5`, `session-persistence-round-2`,
`snippet-expansion-gaps`, `bundled-snippets`, `multiple-terminal-tabs`).

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
- [ ] **Test-runner gaps**: no gutter-click run-this-test; pytest needs `-v` or
      junit-xml for per-test pass marks; Go's basename-only `file:line` can miss
      jump-to-source in multi-directory modules (Go itself now has full test discovery
      via `go-tests.scm`, same as Rust's `rust-tests.scm`).
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
- [ ] **Persistent left-side glyph rail for toggling panels** (raised 2026-09-06,
      design-sketched 2026-09-07). Divider-double-click-to-collapse was inconsistent
      across panels — only `ProjectSidebar` had it; `VcsPanel` modeled its own collapse
      on `ProjectSidebar`'s convention but never wired the double-click check, and
      standalone `AcpPanel` had no mouse divider-collapse at all — all three now share
      the same `dividerClickPending_`/`kDoubleClickWindow` pattern (see
      `git log --grep=divider-double-click-collapse-gap`). The bigger idea that
      prompted this: `ProjectSidebar` already collapses to a 1-column border strip with
      a glyph hint — generalize that strip into an always-visible, VS Code-style
      "activity bar" holding one glyph per always-docked left panel (files, VCS),
      rather than collapse-to-a-strip being `ProjectSidebar`/`VcsPanel`-only chrome.

      Concretely surfaced 2026-09-07 by a real bug: `ProjectSidebar` and `VcsPanel` are
      two fully independent `Widget`s docked in the same left slot, each owning its own
      border, width, collapse-to-strip state, resize-drag divider, and keyboard focus —
      kept "mutually exclusive" only by ad hoc coordination
      (`BufferView.cpp`'s `ToggleProjectSidebar`/`ToggleVcsPanel` collapse the other
      panel on toggle; a startup-time check, added in the same fix, collapses whichever
      is the tie-break loser if both panels' independently persisted
      `sidebar-visible`/`vcs-panel-visible` variables came back `true` — see
      `git log --grep=vcs-diff-preview-and-two-left-bars`). That's a patch over the
      structural problem this bullet already named: any future left-docked panel would
      have to duplicate the same chrome and exclusivity dance again.

      Design sketch for the rail, one level more concrete than "generalize the strip"
      above: a `LeftDock` widget (`Source/UI/LeftDock.h/.cpp`) owning the single
      border/width/collapse state for the left slot, plus an ordered list of registered
      `{glyph, name, Widget* content}` panels and an `activePanel_` index — a fixed
      ~3-column glyph strip (highlighted background for the active panel) beside a
      single `Canvas::ForBox` sub-region for whichever content widget is active;
      collapse-to-strip keeps only the glyph column painted, mirroring
      `ProjectSidebar`'s existing "`Collapsed()` reports 1-column width" precedent so
      the icons stay a click target to re-expand. `ProjectSidebar`/`VcsPanel` lose
      `DrawBorder`/`Width()`/`Collapsed()`/`SetWidth`/`SetCollapsed`/the resize-drag
      divider entirely and become plain content views — the real size of the change is
      every existing call site reading those today (`BufferView`'s narrowing-clamp
      check on drag, `WindowManager`'s `Container` `SizeSpec`, `TabBar`'s
      reveal-in-sidebar, `ProjectSidebar`'s own sticky-scroll) repointing at
      `LeftDock`'s width/collapsed instead of the individual panel's; `bufferRow`'s
      `Container` in `main.cpp` drops the `projectSidebar`/`vcsPanel` two-sibling
      entries for one `leftDock` sibling. Today's two independent
      `sidebar-visible`/`vcs-panel-visible` variables collapse into
      `leftDock-collapsed` + `leftDock-active-panel` — the `left-panel-active`
      tie-break variable from the 2026-09-07 fix becomes exactly this second variable,
      so that work carries forward rather than being thrown away.
      Migration order to keep this reviewable rather than one big-bang commit: (1)
      **done 2026-09-07** — `Source/UI/LeftDock.h/.cpp` built standalone (rail plus a
      bordered content region hosting whichever registered `{glyph, name, Widget*}`
      panel is active; `AddPanel`/`SwitchTo`/`CommitSwitchTo` mirror `PanelDock`'s own
      stable-id shape; width/collapse/resize-drag mirror `ProjectSidebar`'s contract)
      with `Tests/LeftDockTest.cpp` exercising it headlessly against two fake content
      widgets, `PanelDockTest.cpp`'s own precedent (13 cases — registration, paint
      delegation, rail-click switch/collapse/expand, resize-drag commit, mouse-event
      forwarding into the active panel's own local coordinates) — see
      `git log --grep=unified-left-dock`. (2) **done 2026-09-07** — `ProjectSidebar`
      stripped of `DrawBorder`/`Width`/`SetWidth`/`Collapsed`/`SetCollapsed`/
      `ToggleCollapsed`/`ExpandedWidth`/`IsResizing`/`UpdateResize`/`EndResize`/
      `TakeKeyboardFocus`/`SetOnWidthCommitted`/`SetOnCollapseCommitted` entirely and
      hosted alone in a real `LeftDock` wired into `WindowManager`/`main.cpp`'s
      `bufferRow` (`VcsPanel` still a separate, fully chrome-owning sibling — step 3's
      job). Row 0 stays the widget's own content row (project name, click-to-switch)
      rather than becoming a border title, since `LeftDock`'s per-panel border title is
      a fixed label with no way to express a live value. Surfaced one real design gap
      the sketch hadn't covered: collapsing while the hosted content holds keyboard
      focus needs to hand focus back, but collapse (`LeftDock`) and the focus-return
      hook (`ProjectSidebar::SetOnFocusReturn`) now live on two different objects —
      closed with a small generic bridge, `Widget::OnFocusPreempted()` (default no-op,
      `ProjectSidebar` overrides it to fire `onFocusReturn_`, `LeftDock::SetCollapsed`
      calls it on the active content when collapsing while that content is `Focused()`).
      `LeftDock::PrepareForKeyboardFocus`/`NoteFocusReturned` (added this step, not in
      the original step-1 sketch) replace `ProjectSidebar::TakeKeyboardFocus`'s own
      expand-and-remember/restore pairing now that collapse state moved. Live-verified
      in a real running session (tmux): rail-click collapse/expand, `C-c p` expanding a
      collapsed dock and taking focus, arrow-key tree navigation, and Escape returning
      focus *and* re-collapsing all confirmed working end to end — see
      `git log --grep=unified-left-dock` (full test suite green throughout,
      3654 cases). (3) strip chrome out of `VcsPanel`, register it as the second panel,
      retire the now-dead exclusivity code in `BufferView.cpp` and the startup tie-break
      variable in favor of the dock's own state; (4) update `C-c p`/`C-c v p`'s meaning
      (dock-switch instead of two independent toggles) and this entry.

      Design question resolved 2026-09-07: the rail stays left-side-only and does
      *not* replace `PanelDock`'s own tab strip for the bottom-docked panels, nor
      `AcpPanel`'s right-dock mode — `LeftDock` covers only the panels that are always
      present in the layout (`ProjectSidebar`/`VcsPanel`), while `PanelDock` covers
      panels that come and go (terminal, debug console, Janet REPL). Kept as three
      distinct widgets rather than unified into one tab system, the same way `TabBar`
      (buffer tabs, always present, top-docked) already stays separate from both —
      "always available" vs. "toggled into existence" is a real difference in what's
      being switched between, not just a placement difference, so collapsing them into
      one mechanism would blur that rather than simplify anything. A genuinely
      medium-sized refactor (new widget + retrofitting two established panels' call
      sites) — worth its own dedicated session with real build/test checkpoints per
      step, not a single sitting. Not started.
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
- [ ] **Bracketed-paste multi-cursor distribution** (paste-perf-and-drag-drop
      follow-up, scoped down 2026-09-06). A real terminal paste (via
      `BufferView::HandleBulkPastedText`/`Buffer::InsertAtPoint` fast path) inserts at
      a single point only, even with secondary cursors active — v1 deliberately
      matches the existing middle-click-paste precedent rather than `yank`'s
      `ForEachCursor`-based per-cursor splitting. Revisit if multi-cursor editing and
      real terminal paste turn out to be used together often enough to matter.

### Merge Conflict Resolution Mode (New Feature)

The conflict hunk model (`Text/ConflictHunk.h`'s `ParseConflictHunks`, the read-back
counterpart to `Text/ThreeWayMerge.h`'s own marker-writing convention), per-hunk
resolution commands (`merge-take-ours`/`-theirs`/`-both`/`-neither`/`-keep-base`,
`Editor/ConflictResolution.h`, one undo step each), wrapping navigation
(`next-conflict-hunk`/`previous-conflict-hunk`), a themed ours/theirs/base background
tint (`Theme::conflictOursBackground` et al.), and a `VcsPanel`-open status-line hint are
all shipped under a `C-c x <letter>` prefix — see `git log --grep=merge-conflict-resolution`.
No modal state was introduced at all (a deliberate simplification over the original
scoping): resolution is just ordinary commands over ordinary buffer text, so "never a
modal trap" falls out for free rather than needing its own design. A conflict-scoped
`M-o/t/b/d/k/n/p` fast-key layer (`BufferView::HandleConflictQuickKey`, smerge-mode's own
precedent) rides on top, shadowing those otherwise-global bindings only while the active
buffer has an unresolved hunk — see `git log --grep=conflict-quick-keys`.

- [ ] **A per-hunk inline mouse action row** (take-ours/take-theirs/take-both/take-neither
      as clickable text, `BufferView`'s existing gutter-click precedent) — the mouse-driven
      fast path the keyboard-only v1 above is missing; not required, scope as a follow-up.
- [ ] **Whole-file / whole-hunk-run bulk actions** — "take all ours"/"take all theirs"
      for a file with many mechanically-identical hunks (e.g. a lockfile or generated
      file where one side is always right) — a `VcsPanel` per-file action, not a
      per-hunk one; scope only if real usage shows the per-hunk loop is too slow for
      that case.
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

ACP MCP tool-server bridge, v1 slice is shipped (`Editor/Mcp/`: `McpBridgeServer` +
`McpToolRegistry` + `McpTransport`/`McpSocketPath`, `ned/set-acp-mcp-bridge`, default
on) — see `git log --grep=acp-mcp-tool-bridge`. The transport question the original
item posed (in-process call surface vs. a real local MCP server) resolved to the
latter, forced by the spec itself: stdio is the only MCP transport every agent MUST
support (http/sse both require an agent capability that can't be assumed), and a
stdio server is necessarily a separate OS process the agent spawns — so the live
`ned` process listens on its own per-process Unix domain socket
(`$XDG_RUNTIME_DIR/ned/mcp-<pid>.sock`) and `ned --mcp-stdio-relay <socket-path>` (a
dumb stdin/stdout↔socket byte pump, no JSON parsing) is the `mcpServers` "command" the
agent actually spawns — real protocol handling (`initialize`/`tools/list`/`tools/call`)
happens inside the live process where `LspManager`/`VcsRunner`/`TestRunner`/open
`Buffer`s actually live.

Remainder slice shipped too (`git log --grep=acp-mcp-tool-bridge-remainder`): 12 more
tools — `git_stage`/`git_unstage`/`git_commit`/`git_branch_list`/`git_branch_switch`/
`git_blame`, `rerun_failed_tests`, `workspace_symbols`, `format_buffer` (mutating —
applies the server's edits as one undo step, `Editor/Lsp/LspEditApply.h` extracted out
of `BufferView.cpp` so both share the exact same apply logic instead of forking a
copy), `code_actions`/`preview_rename` (deliberately listing/preview-only, not
applying), and `get_diagnostics_log(category?)`. No ned-side permission gate was added
for the mutating tools (git stage/unstage/commit/branch-switch, format_buffer) —
MCP's own spec places "a human in the loop with the ability to deny tool invocations"
as the *client's* (the agent's) responsibility, the same place `fs/write_text_file`'s
own approval already lives; ned doesn't duplicate it. `format_buffer`'s async
callback re-confirms the buffer is still open at the same address before touching it
(`BufferView::RequestLspFormatThenSaveBuffer`'s own stale-pointer-as-opaque-key idiom)
since the human can close it while the LSP request is in flight.

Two things were deliberately cut after checking the real APIs against this item's own
original aspirational list, both still open:
- [ ] **Real rename/code-action apply + `goto(file, line)` navigation** — all three
      need a live `WindowManager`/`BufferView` (`ApplyProjectEdit`'s multi-file
      transaction machinery for the first two — `ProjectUndoManager` recording, file
      create/rename/delete via `DocumentChangeOp`; `BufferView::JumpToPathLine` for the
      third) that `McpToolRegistry` doesn't have and doesn't currently reach. Not a
      thin wrapper the way everything shipped so far is — a real follow-up, not
      attempted here.
- [ ] **Org `capture_note`** — `OrgCapture::InsertCapture` only inserts a template
      whose text is fixed at Janet-registration time (`%?` just marks where point
      lands after expansion); there's no way to inject agent-supplied free text into a
      capture through the existing API. Needs a small `OrgCapture.h` capability
      addition (accept caller-supplied text to substitute at `%?`) before a faithful
      tool can exist — `clock_in`/`clock_out` were dropped from scope for the same
      "not actually a thin wrapper" reasoning, though those don't need the API change,
      just a decision on how an MCP tool expresses "at point" headlessly.
DAP↔ACP debugging bridge is shipped (2026-09-07) — see `git log --grep=dap-acp-bridge`.
Structured tools (`dap_list_breakpoints`/`dap_set_breakpoint`/`dap_remove_breakpoint`,
`dap_continue`/`dap_pause`/`dap_stop_session`/`dap_step_over`/`dap_step_into`/
`dap_step_out`, `dap_get_current_location`/`dap_get_stack_trace`/`dap_get_scopes`/
`dap_get_variables`/`dap_evaluate`, `dap_list_watches`) let an ACP agent act as a real
pair-debugger via `Editor/Mcp/McpToolRegistry.cpp` — thin wrappers over `DapManager`'s
already-async-callback-shaped public methods, the same shape every prior MCP tool used;
no new `DapManager` capability was needed. Debug-session context injection also shipped
as `dap-ask-agent` (`BufferView::SendDebugStateToAgent`): a one-click command that
gathers the stopped session's stack/scopes/variables/watches (`ShowDebugInfo`'s own
fan-out extracted into a shared `BuildDebugInfoLines` helper) and sends them as one
plain-text prompt via `AcpManager::SendPrompt` — pre-formatted text, not a structured
resource attachment (`SendPrompt` has no attachment mechanism to hook into; see the
"ACP context auto-attach" gap below, still open). The pointer-graph/memory/disassembly/
thread/function-and-exception-breakpoint surface stayed out of the MCP tool set
deliberately — not part of the ask-a-question/set-a-breakpoint/step/inspect loop this
slice targets; a `dap_get_pointer_graph`-shaped tool would need either duplicating
`BufferView::ExpandPointerGraphNode`'s cycle-detection loop or extracting it into a
shared, UI-free helper first (`Editor/PointerGraphNode.h`'s data shape is already
reusable, the traversal algorithm isn't yet) — a real follow-up, not attempted here.
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

### Notcurses Patches Worth Upstreaming (Watch List)

Not submitted anywhere yet — a deliberate choice (2026-09-06), not an oversight. Recorded
so the research doesn't have to be redone before actually opening anything.

- [ ] **`CMake/PatchNotcursesNulKey.cmake`** (Ctrl+Space/Ctrl+@ swallowed as a NUL byte)
      and **`CMake/PatchNotcursesMouseWheel.cmake`** (SGR wheel-right, Cb=67, misdecoded
      as a motion+release) are both still-reproducible bugs against upstream
      `dankamongmen/notcurses` `master` as of 2026-09-06 (verified live against the
      current `src/lib/in.c`, not just our pinned `v3.0.17`) — checked GitHub issues for
      both ("Ctrl+Space", "NUL", "0x00", "wheel") and found nothing matching, so these
      look like genuinely novel, unreported bugs. Both patches are already small,
      root-caused, and general (not ned-specific workarounds), so they're close to
      PR-ready as-is whenever we decide to open them.
- [x] ~~Bracketed paste mode~~ — shipped 2026-09-06 (`CMake/PatchNotcursesBracketedPaste.cmake`),
      see `git log --grep=paste-perf-and-drag-drop`. Upstream issue
      [#2704](https://github.com/dankamongmen/notcurses/issues/2704) has been open since
      2023 with a maintainer-endorsed design sketch from `tstack` (`lnav`'s maintainer)
      that was never turned into a PR — our own patch deliberately took a simpler shape
      (no `fbuf`/`paste_content` field inside Notcurses itself; all buffering happens in
      `EventLoop::Run()`, which already owns the right place for it), so it isn't a
      drop-in match for that sketch if we ever revisit upstreaming this specific gap.

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
      - **C#** (needed for Godot-via-Mono) — bundled language support shipped, see the
        Language Intelligence section above; `OmniSharp`/`csharp-ls` are the LSP options,
        config-only, not touched by that work.
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
Code coverage gutter is shipped (2026-09-06) — see `git log --grep=code-coverage-gutter`.
`Editor/Coverage/CoverageOutputParser.h` parses lcov's `.info` format (covers `lcov`
itself, `llvm-cov export -format=lcov`, and `gcovr --lcov` with one parser); the gutter
column (`C-c T c`/`load-coverage-report`, `ned/set-coverage-file`) marks covered/partial/
uncovered per line, cross-referenced against the VCS diff gutter's own data to flag an
uncovered line that's also newly added/modified. Raw per-file `.gcov` output was
deliberately left out (a directory-scan problem, not a single-document parser like every
other format this codebase's `Editor/*OutputParser.h` files handle); revisit only if
lcov's `.info` format proves insufficient in practice.

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
