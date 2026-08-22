# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — the detailed
per-feature design/decision records this file used to carry were pruned 2026-08-20 and
live in git history (`git log --follow ROADMAP.md`, `git show <rev>:ROADMAP.md`);
current architecture is documented in `CLAUDE.md` (note: some of its "see ROADMAP.md's
X entry" pointers refer to those pruned records).

## Vision

An Emacs-class terminal editor: buffers, windows, keymaps, modes, minibuffer,
kill-ring, undo tree, isearch — "everything is a programmable command," with Janet
filling Elisp's role. The whole editor is a Janet-scriptable environment, not a C++
app with a config file bolted on. Modern, memory-safe C++23; TUI rendered with
Notcurses (TermOx → FTXUI → Notcurses over the project's life).

## Guiding constraints

- **Memory safety.** No raw owning pointers — `unique_ptr`/`shared_ptr`,
  `string`/`string_view`, `vector`/`span`. Janet's C heap is external and stays
  malloc'd internally.
- **Programmability first.** New editor capability = a named command reachable from
  keybindings, `M-x`, and Janet uniformly — not hardcoded control flow.
- **XDG Base Directory compliance.** Config → `$XDG_CONFIG_HOME/ned/`, user data →
  `$XDG_DATA_HOME/ned/`, caches → `$XDG_CACHE_HOME/ned/`, other persistent state →
  `$XDG_STATE_HOME/ned/`. Never a bare dot-file in `$HOME`.
- **Keep `Source/UI/` loosely coupled from the TUI library where it's cheap.** This
  paid for itself twice (TermOx → FTXUI, FTXUI → Notcurses) — both migrations stayed
  contained to `Source/UI/` and never touched `Text/`/`Editor/`/`Janet/`.

## Open items

### Embedded language
- [ ] **Jank replaces Janet** - Once I'm able to do so, I'd love to replace our
      internal scripting representation to move to [jank](https://github.com/jank-lang/jank)

### Language intelligence

- [ ] **Embedded-language documents** (HTML with inline `<script>`/`<style>`,
      Vue/Svelte-style SFCs) — segment the buffer into per-language virtual documents
      (tree-sitter injection queries find the boundaries), sync each to its own
      server, remap positions back. `LspManager` gained real multi-server-per-buffer
      diagnostics merging with the prose-checking feature (harper-ls as a second,
      independent diagnostics channel alongside the primary language server) — that
      two-server shape is a fixed pair keyed by a reserved language key, not yet a
      general N-server-per-buffer mechanism this would need.
- [ ] Per-capture highlighting round 2 (v1 shipped, exhaustive-highlighting
      follow-up: enumeration of all 17 bundled queries' 87 capture names, defaults
      closing every gap found, `HighlightSpan` carrying an interned capture id, a
      dotted-name-inheritance override store — capture chain beats `SyntaxClass`
      override beats built-in theme, field by field — capture→class remapping, and
      the `ned/set-capture-*`/`ned/capture-names` Janet surface). Deliberate cuts:
      **language-scoped rules** (an explicit decision — one rule set for all
      languages; the resolution walk in `SyntaxTheme.h` is the seam a
      `<lang>/<name>` tier would prepend to later, and markdown's hardcoded
      `punctuation.special` special case in `Mode.cpp` is the first candidate to
      migrate onto it), per-capture styling in the Minimap (class-level only
      there), and theme-file serialization of capture overrides (overlaps the
      bold/italic round-trip loose end below).

### Navigation & search

- [ ] **Multibuffers** — a virtual buffer stitching excerpts from multiple
      files/locations (all references, all diagnostics, fuller VCS history views) into
      one scrollable, editable view. A good fit for the Rope design; needs its own
      design pass.

### Large files

- [ ] **Windowed/paged editing for genuinely huge files** (multi-GB). Not a tweak to
      the async loader — `Rope` and everything on it assume fully-resident content.
      Recommended v1 shape: a **read-only mmap-backed viewer** past a size threshold,
      lazily building a line-offset index only near the viewport, with an explicit
      "load fully to edit" step reusing the async loader. In-place windowed *editing*
      needs a disk-backed piece table — most of a new engine, explicitly not v1.
- [ ] Loose end: buffers restored by a project session open before the async-loader
      hook is wired, so a huge file inside a restored session still loads
      synchronously at startup.

### Org & structured editing (v1 shipped; this is v2+)

- [ ] Agenda view: date/deadline-driven scheduling. `ProjectAgenda.h` already ships a
      cross-file active-TODO *list* (every non-DONE headline under the project root, reusing
      `ProjectSearch`'s results-buffer/visit machinery); what's still missing is real
      SCHEDULED:/DEADLINE: timestamp parsing and a genuine date-driven agenda view (today's
      items, overdue deadlines) — no structured timestamp parsing exists yet at all, so this
      folds into the date/recurrence item below rather than being separate work.
- [ ] Scheduling/deadlines with real date/recurrence logic.
- [ ] Property drawers.
- [ ] Capture templates (quick-add an entry from anywhere).
- [ ] Clocking/time tracking.
- [ ] Markdown (GFM) table editing surface — Org's table ops didn't carry over because
      GFM's delimiter row holds per-column alignment state a column op must rewrite.
- [ ] **Universal clickable in-buffer affordances** — file paths and URLs clickable in
      *every* mode (open the file, `xdg-open` the URL), generalizing the
      click-to-action plumbing Org links use rather than keeping it Org-specific.

### Editor ergonomics

- [ ] Terminal panel round 2 (v1 shipped: libvterm-backed drawer over the
      `OverlayHost` floating-widget layer, `toggle-terminal` on `` C-` ``/`C-c t`,
      title-row `[▼]` minimize/`[▲]` maximize/`[×]` close mouse buttons alongside
      the keyboard toggle, 2000-line scrollback via Shift+PageUp/Down,
      respawn-on-Enter after exit; `C-c t` on a visible panel hides rather than
      focuses — the keyboard-complete cycle on legacy-encoding terminals where
      `` C-` `` never arrives, a real live-use report). Deliberate v1 cuts: no
      drag-resize of the drawer height (needs overlay mouse-capture semantics;
      height is Janet-configurable via `ned/set-terminal-height-percent`
      instead), no scrollback search/selection/copy, no multiple terminals/tabs,
      no terminal-side mouse forwarding to the shell (clicks focus the panel,
      wheel scrolls the ring — TUI apps inside the terminal don't receive mouse
      events), and no OSC 52/title integration.
- [ ] **Remote development** (SSH remote editing).
- [ ] Emacs keymap round 2 (round-1's deliberate cuts): prefix arguments (`C-u`),
      `zap-to-char`, sentence/sexp motion, kill-append on consecutive kills.
- [ ] Multi-cursor round 2 (v1 cuts): kill-ring/register/rectangle commands acting
      per-cursor (with a real decision on per-cursor kill-ring semantics), and
      scrolling the view to a newly added occurrence cursor.
- [ ] External-modification round 2: three-way merge when both buffer and disk changed
      (`SavedSnapshot_` gives a diff3 base for free), and Emacs' ask-on-first-edit
      supersession prompt.

### Collaboration & AI

- [ ] **AI-assisted editing** (inline completion, chat with codebase context) — the
      natural shape is a Janet-scriptable ACP-client integration; `Process/ChildProcess`
      (the shared subprocess primitive `Lsp`/`Dap`/`Tasks`/`Vcs` all already build on) is
      reusable for this too, following the same client/manager split as `LspManager`/
      `LspClient` — not yet started, no `AcpManager`/`AcpClient` exists. The
      floating/overlay widget layer this needs now exists (`UI/Overlay.h`'s
      `OverlayHost`, terminal-panel follow-up): an LLM panel is one `Add()` with a
      right-dock placement function. The same layer is the intended home for a
      completion-popover replacement of ghost text, an M-x dropdown, and
      code-action lists — all currently squeezed into the one-row EchoArea.
- [ ] **Real-time collaborative editing** (CRDT-based) — the biggest lift in this
      file; last.

### Documentation & companion tooling

- [ ] **Documentation framework** — man page(s), PDF, and a web page generated from
      one shared source (pandoc is the direct fit), mining the `ned/*` binding doc
      strings already passed to `Register<Fn>`. A build/release-time step, nothing
      ned does at runtime.
- [ ] **Environment setup tool** (`ned-setup` or similar) — first-run detection: shell
      integration, plus scanning the system for installed tree-sitter grammars and
      *generating an editable Janet file* loaded from `init.janet`. Deliberately a
      standalone, inspectable generator — silent runtime auto-detection was considered
      and rejected (system grammar layouts aren't portable).
- [ ] **Tree-sitter-assisted formatter** with JetBrains-level per-rule configurability
      ("a dprint clone that is actually awesome") — a substantial project per
      language, not a utility. Its highlighting groundwork (exhaustive capture
      enumeration + per-capture identity) shipped with the exhaustive-highlighting
      follow-up; scope it once concrete gaps left by external formatters are known.

### Small loose ends

- [ ] **`libned` as a real shared library** — `ned_lib` (static today) exists solely so
      `ned_tests` can link real editor code without pulling in `main()`; a static lib
      already does that job. Worth revisiting only if a second real consumer shows up
      (an embedding use case, a separate CLI tool, ...) — would need symbol-visibility
      curation (everything's exported by default today) and SONAME/ABI-versioning
      discipline, neither of which pays for itself with zero external consumers. Raised
      during the Gentoo packaging follow-up below.
- [ ] Hunk unstage matches point against the *cached* staged diff, which drifts when
      unstaged edits exist earlier in the file — exact in the common
      stage-then-undo flow; revisit only if it bites.
- [ ] LSP deliberate cuts, revisit on demand: syncing every open buffer (not just the
      active one), incremental sync, idle server teardown, multi-root workspaces, raw
      subprocess stderr capture.
- [ ] DAP deliberate cuts: attach mode, thread picker, watch expressions,
      conditional/logpoint breakpoints, adapter-verified breakpoint positions,
      setting variables, a REPL console.
- [ ] VCS: "Generalize the two-callback plugin shape past version control" (cloud
      CLIs, Terraform, Docker) remains a framing, not a plan.
- [ ] Self-hosting: no special-casing yet for editing ned's own config in ned
      itself — `ned/*` function/macro names and Janet-mode completion don't know
      about each other, so editing `init.janet`/`.ned/init.janet` gets no
      tab-completion against the real `ned/*` API surface. From `Stuff.md`
      (folded in and removed as a standalone file).
- [ ] A friendlier, possibly visual surface for browsing/editing ned's own
      settings beyond hand-writing `init.janet` — real live-editing already
      exists for themes specifically (`save-theme`/`ned/theme-set`, see
      `UI/ThemeFile.h`); a general settings surface would generalize that.
      Vague, unscoped — from `Stuff.md`.

## Won't do (at least not soon)

- **Org Babel** — subsystem-sized, and arbitrary code execution triggered by opening a
  text file: a security surface to design around deliberately, not bolt on.
- **Org table formula/spreadsheet engine** — a small programming language of its own.
- **Org export backends / publishing pipeline** — each a standalone tool-sized effort.
- **Alt+Click cursor creation** — mouse-dependent in a terminal app where SSH/tmux
  mouse passthrough is unreliable; keyboard multi-cursor commands cover it.
- **Bundling or requiring a JVM** for anything (heavy checkers like `ltex-ls` stay
  opt-in user configuration).

## Notes for whoever builds next

- Build/test: `cmake -S . -B build && cmake --build build`, then
  `ctest --test-dir build`. Sanitizer opt-in: `-DNED_ENABLE_SANITIZERS=ON` with
  `-DCMAKE_BUILD_TYPE=Debug` — the suite is expected clean; a finding is a real bug.
