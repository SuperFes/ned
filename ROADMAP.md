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
- [ ] **Header/source switching** (`M-o` or similar — real go-to-definition,
      `lsp-goto-definition`/`M-.`, already ships via ordinary
      `textDocument/definition`; this is the distinct "same logical file, other
      half" hop). clangd exposes `textDocument/switchSourceHeader` as a custom
      LSP extension for exactly this — no new wire-protocol plumbing needed,
      `LspClient::SendRequest` already takes an arbitrary method string, so this
      is a new `LspManager` method plus a command/keybinding. Only meaningful
      where a server actually implements the extension (clangd does; nothing
      else bundled-config'd today does); needs a non-LSP fallback for languages
      with no such extension and no clean single-definition-file split anyway
      (same-basename/known-extension-pairs heuristic, scanning sibling
      directories — a real fallback, not a v1 cut, since C/C++ users are the
      only ones with a server-native answer).
- [ ] **Go-to-file-at-point for include/import-style directives**
      (`#include "foo.h"`/`<vector>`, Python `import`/`from ... import`, JS/TS
      `import`/`require(...)`, Rust `mod`/`use`, ...) — distinct from
      `Link.h`'s generic bare-path detection (`DetectLinkAtPoint`'s file
      candidate needs a whitespace-delimited, slash-or-extension-shaped token;
      a quoted `"foo.h"` or angle-bracketed `<vector>` doesn't parse as one,
      and even a correctly-extracted target needs *language-specific*
      resolution rules Link.h has no notion of — quote-form C/C++ searches the
      including file's own directory before any include path, angle-form
      searches only compiler/project include paths (clangd's own compilation
      database, not filesystem-guessable); JS/TS needs `node_modules`
      resolution plus extension/`index.*` inference; Python needs package/
      `sys.path`-style resolution). Likely shape: a small per-mode
      `IncludeTarget` extraction function (tree-sitter query per language,
      mirroring `Mode::fold`/`expandSelection`'s "one function pointer per
      capability" shape) feeding a per-language resolver, with LSP as a
      first-choice resolver where a server can answer it (clangd again
      supports resolving `#include` targets via `textDocument/documentLink`)
      and the hand-rolled resolver as fallback where it can't.

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

### Collaboration & AI

- [ ] **AI-assisted editing** (inline completion, chat with codebase context) — a
      Janet-scriptable Agent Client Protocol (ACP, Zed's open agent/editor standard)
      integration, `Editor/Acp/` (v1 shipped): `Transport` (newline-delimited JSON-RPC,
      distinct from `Lsp`/`Dap`'s shared `Content-Length` framing — the one real wire-
      level difference from those siblings) + `AcpClient` (bidirectional, with an
      async-capable agent→client `RequestHandler` LSP's synchronous-only one couldn't
      model) + `AcpManager` (single-session handshake, `fs/read_text_file`/
      `fs/write_text_file` bridged into real buffers reusing `AutoRevert`/`AutoMerge`'s
      own Revert/MergeExternalChanges gating, `session/request_permission` as a
      numbered-choice prompt mirroring `LspCodeActionSelect`) — same client/manager
      split as `LspManager`/`LspClient`, built directly on `Process/ChildProcess`
      exactly as anticipated. `acp-start-session`/`acp-send-prompt`/`acp-stop-session`
      (`C-c a s`/`C-c a p`/`C-c a k` — unreachable by typing, see below;
      `M-x`/Janet only for now) stream a session into a plain read-only
      `"*acp: <agent>*"` buffer — `TaskRunner`'s own output-buffer convention, kept
      alongside the real chat panel below rather than replaced. A real dockable chat
      panel shipped as a follow-up (`UI/AcpPanel.h`, an `OverlayHost` overlay, bottom by
      default/right via `ned/set-acp-panel-dock`, `acp-toggle-panel` on `C-c c`), built
      on a new structured transcript (`AcpManager::Transcript()`, `Buffer::Diagnostic`-
      style wholesale-replace + generation counter) rather than the flat output buffer.
      Deliberate cuts, still open: permission-prompt *resolution* stays in `BufferView`'s
      existing echo-area flow, the panel only displays the pending prompt; no scrollback
      in the panel (same v1 cut `TerminalPanel` itself has); a completion-popover
      replacement for ghost text, an M-x dropdown, and code-action lists currently
      squeezed into the one-row EchoArea are still unbuilt (the panel didn't turn into a
      general popover host); `terminal/*` tool-call support (spawning real
      `ChildProcess`/pty-backed terminals on the agent's behalf) and `elicitation/create`
      structured forms — both left undeclared as client capabilities, so a
      spec-compliant agent won't invoke them, rather than answered badly; multiple
      concurrent agents/sessions; `session/load` history replay;
      `session/set_config_option`/`session/set_mode` surfaced to the user; MCP server
      passthrough (`session/new`'s `mcpServers` is always sent as `[]`). The exact
      `session/update` sub-schema (message-chunk/tool-call/plan discriminated union)
      still isn't pinned down here against the authoritative ACP JSON schema, only
      informally documented at the time this was written — `AcpManager::
      HandleSessionUpdate`'s defensive, best-effort parsing (now including a `"plan"`
      branch, previously silently dropped) is expected to need widening once exercised
      against a real agent. Also newly found, keymap-collision follow-up: `Keymap::Resolve`
      returns a `Match` the instant a node's own command is set, before consulting its
      children, so any `C-c a <x>` binding was unreachable by typing once `C-c a` itself
      was bound to `org-agenda` — confirmed live, affected `acp-start-session`/
      `acp-send-prompt`/`acp-stop-session`; `acp-toggle-panel` avoided the same trap by
      binding `C-c c` instead. Worked around by moving the three ACP session commands to
      `C-c A s/p/k` (shifted "A", distinct from plain "C-c a"), and
      `Keymap::AmbiguousBindings()` (walks the trie for any node holding both a `command`
      and non-empty `children`) now guards the shipped default/Org/Markdown keymaps via
      `CommandsTest.cpp`'s "shipped keymaps have no unreachable-by-typing bindings" test
      — a future collision like this fails `ctest` instead of needing another live tmux
      session to find. Still open: `AmbiguousBindings()` is diagnostic only, not
      enforcement — `Keymap::Bind` still lets a caller construct this shape, it's just
      caught by the regression test for the keymaps that ship today. A real structural
      fix (Emacs' own `define-key` semantics: reject/restructure a bind that would shadow
      an existing command with a longer sequence, or vice versa) would change `Bind`'s
      signature across every call site including `ned/define-key` (would need to surface
      as a catchable Janet error for user init.janet scripts) — worth doing eventually,
      deferred as its own change since it's a multi-file API change, not a one-off
      collision fix.
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
