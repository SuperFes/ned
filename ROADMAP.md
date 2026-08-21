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

### Language intelligence

- [ ] **Spell/grammar checking as an LSP diagnostics channel** — not a bespoke
      hunspell integration. `harper-ls` as the auto-wired default when on `PATH`;
      fall back through other LSP-speaking checkers, ending in "no prose checking,"
      never a hard failure; `ltex-ls`/LanguageTool stays opt-in user config (ned never
      bundles or requires a JVM). The real work is **multi-server-per-buffer
      diagnostics merging** — `LspManager` is strictly 1:1 (one language, one client
      per buffer) today, and prose checking must attach to any prose-shaped buffer
      independent of the primary language server. Never runs against binary buffers
      (key off the existing binary classification).
- [ ] **Embedded-language documents** (HTML with inline `<script>`/`<style>`,
      Vue/Svelte-style SFCs) — a separate, larger follow-up to the above: segment the
      buffer into per-language virtual documents (tree-sitter injection queries find
      the boundaries), sync each to its own server, remap positions back. Requires
      multi-server-per-buffer first.
- [ ] **Exhaustive, per-capture-name-configurable tree-sitter highlighting** — a
      sensible default mapping for *every* capture name a grammar can produce (today's
      `SyntaxClass` is a curated 23), plus user override of any of it. Groundwork for
      the tree-sitter formatter below. First step when picked up: enumerate the actual
      capture names across the 13 bundled grammars.

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

- [ ] Agenda view — aggregate TODOs/deadlines across a tree of files into one buffer.
      A genuinely new UI kind (synthesized, cross-file), the most-loved Org feature.
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

- [ ] **Built-in terminal panel** — a real interactive pty (`forkpty`) plus
      VT100/xterm emulation. Deliberately not a task-runner variant: the task runner
      streams output into a read-only buffer, this needs genuine terminal emulation
      and its own pty-backed path.
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
      natural shape is a Janet-scriptable ACP-client integration; the task runner's
      subprocess transport was built to be reusable for exactly this
      (`AcpManager`/`AcpClient` mirroring `LspManager`/`LspClient`). Note: ned still
      has no floating/overlay/popup widget concept, and an LLM panel is the strongest
      argument yet for building one.
- [ ] **Real-time collaborative editing** (CRDT-based) — the biggest lift in this
      file; last.

### Theming & visual

- [ ] Theme polish (the planned "phase 4"): keep the tmux sweep script in-repo, theme
      documentation, and the `--detect-theme` precedence note.
- [ ] Stretch clone themes, each one `ThemePalette` literal away: Rosé Pine,
      Everforest, Zenburn, Catppuccin Frappé/Macchiato, Tokyo Night Storm.
- [ ] Make the ANSI theme pair user-selectable on capable terminals / expressible in
      theme files (serialization already round-trips `x:<n>` palette tokens).
- [ ] Bold/italic round-trip in theme serialization (long-standing `Brush`
      limitation — only background/foreground persist).
- [ ] **Pixel-blitter minimap** — render the minimap via `NCBLITTER_PIXEL`
      (ncvisual-backed) on terminals that support it, keeping the braille-glyph
      renderer as the fallback everywhere else. Capability-gate at runtime
      (`notcurses_canpixel`), the same live-context check pattern the ANSI theme
      fallback uses. Decision is settled — the user wants this despite earlier
      counterarguments; don't re-litigate, just sequence it.

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
      language, not a utility. Needs the exhaustive-capture highlighting work above as
      groundwork; scope it once concrete gaps left by external formatters are known.

### Small loose ends

- [ ] Janet-expose remaining hardcoded constants: `kDiffRefreshDebounce` (1200ms),
      `kPageScrollFraction` (0.65), `kMaxBackupBytes` (64 MiB).
- [ ] Session persistence gaps: window-split layout isn't persisted; a
      `.ned/plugins/*.janet` autoload dir; `ned-init-project` offering a `.gitignore`
      append.
- [ ] Hunk unstage matches point against the *cached* staged diff, which drifts when
      unstaged edits exist earlier in the file — exact in the common
      stage-then-undo flow; revisit only if it bites.
- [ ] LSP deliberate cuts, revisit on demand: syncing every open buffer (not just the
      active one), incremental sync, idle server teardown, multi-root workspaces, raw
      subprocess stderr capture.
- [ ] DAP deliberate cuts: attach mode, thread picker, watch expressions,
      conditional/logpoint breakpoints, adapter-verified breakpoint positions,
      setting variables, a REPL console.
- [ ] VCS: multi-line commit messages (`MinibufferPrompt` is single-line by
      construction). "Generalize the two-callback plugin shape past version control"
      (cloud CLIs, Terraform, Docker) remains a framing, not a plan.

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
