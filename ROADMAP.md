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
- **Keep `Source/UI/` loosely coupled from the TUI library where it's cheap.**.

## Open Items

### Embedded Language
- [ ] **Jank replaces Janet** - Once I'm able to do so, I'd love to replace our
      internal scripting representation to move to [jank](https://github.com/jank-lang/jank)

### Language Intelligence

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
- [ ] Go-to-file-at-point for import/include directives (v1 shipped:
      `Mode::importTarget`, a tree-sitter-query-driven capability mirroring
      `Mode::fold`/`expandSelection`'s "one function pointer per capability"
      shape — every bundled language with a real import/include construct has
      its own small `*-imports.scm` query, `RegisterDynamicMode` takes the
      same query file for a runtime-loaded grammar). Still open: LSP as a
      first-choice resolver ahead of the hand-rolled one where a server can
      answer it (clangd supports `textDocument/documentLink` for `#include`);
      Python's leading-dot relative imports (`from . import x`); PHP's
      namespace `use` (needs a PSR-4 autoloader parse, out of scope for the
      hand-rolled resolver); JS's dynamic `import(...)`; node_modules
      `package.json` main/exports resolution beyond `index.*` inference;
      Rust when it gets a bundled mode at all.

### Navigation & search

- [ ] **Multibuffers** — `Editor/Multibuffer.h`'s stitching primitive shipped, read-only
      v1 scope (see its own header comment). Three consumers so far:
      `vcs-full-diff-buffer` (every changed file's real diff hunks),
      `lsp-diagnostics-buffer` (every open buffer's Code-origin LSP diagnostics, one
      excerpt per diagnostic, reusing the ordinary diagnostic gutter/underline/
      severity-color pipeline via real composite-space `Buffer::Diagnostic` entries
      rather than a new tint), and `project-find-references` (`M-?`/`ESC ?` -- one
      excerpt per whole-word RE2 match for the identifier at point, across the
      project; a fast textual approximation, not real semantic LSP references, which
      still don't exist as a client capability -- see the `.gitignore` item below for
      where the RE2 engine it's built on came from). Every consumer shares the same
      jump-to-source path: `vcs-visit-result` (`C-c v v`) is generic over
      `MultibufferIndex`, not actually VCS-specific despite the name inherited from
      its first consumer -- worth a rename/rebind to something like
      `multibuffer-visit-result` once a fourth consumer makes the misnomer harder to
      justify. Plain `*project-search*` results are a separate command entirely
      (`project-search-visit-result`, `C-c C-v`) with its own regex-based `path:line:`
      parse, not the `MultibufferIndex` one -- confirmed confusing in practice (two
      different chords depending on which results buffer you're in). Worth unifying
      the two into one command/binding that tries `MultibufferIndexFor` first and
      falls back to the `path:line:` regex, so every results-style buffer (plain
      search, diff, references, diagnostics) answers to the same "visit" chord.
      Still open: fuller VCS history views (e.g. a full commit's diff from
      `*vcs log*`, not just the working tree), making a multibuffer genuinely editable
      (each excerpt writing back to its real source buffer) rather than read-only, and
      a result cap/warning for `project-find-references` on a very common short
      identifier (no limit today -- thousands of matches would build a proportionally
      huge composite buffer).
- [ ] **`.gitignore` correctness gap left by dropping `rg`** -- `ProjectSearch.cpp`'s
      backend is now an internal, multi-threaded RE2 engine (no more `rg` shell-out or
      single-threaded `std::regex_search`-per-line fallback; thread count is a
      configurable setting, `Editor/SearchSettings.h`, `ned/set-project-search-threads`,
      default 4 -- this is I/O-bound, not CPU-bound, so more threads than that mostly
      just contends on the same disk/page cache). One correctness gap this shipped
      with rather than closed: dropping `rg` also dropped its superior `.gitignore`
      handling -- `GitIgnore.h`'s `GitIgnoreMatcher` is still root-`.gitignore`-only,
      not nested/global/`.git/info/exclude`-aware the way `rg` was, so a project
      relying on any of those will see different (too-inclusive) results than it did
      under `rg`. This is the shared backend for `ProjectSearch`/`ProjectReplace`'s
      match-finding and for the find-all-references multibuffer consumer above.
- [ ] **PCRE2 for in-file regex matching/replacing** (eventually, after the RE2
      search work above) -- `Editor/QueryReplace.cpp`'s `query-replace-regexp` and
      `Editor/ProjectReplace.cpp`'s actual per-file rewrite step are the only two
      user-facing "type your own regex" surfaces left on plain `std::regex`
      (ECMAScript syntax) now that project search itself has moved to RE2 (see above; everywhere
      else `std::regex` appears in this codebase -- `GitIgnore.cpp`'s glob
      translation, `Link.cpp`/`Org.cpp`'s hardcoded patterns, `TreeSitter/Query.cpp`'s
      `#match?` predicate compilation, `BufferView.cpp`'s fixed result-line parsing --
      is internal fixed-pattern plumbing, not user-typed). PCRE2 (also
      `FetchContent`'d) is the deliberate choice over RE2 for this pair: `std::regex`
      per the C++ standard's ECMAScript grammar has no lookbehind support at all and
      no named capture groups, is essentially byte-oriented rather than genuinely
      Unicode-aware (`\p{...}` classes), and its interpreter (no JIT) has a long-
      standing reputation as one of the slower mainstream regex engines -- PCRE2's
      JIT (`pcre2_jit_compile`) closes that gap while adding the missing features.
      Still a backtracking engine underneath (unlike RE2), so this needs a real
      match-limit (`pcre2_set_match_limit`) as a safety net -- moving to PCRE2 doesn't
      remove the catastrophic-backtracking exposure `std::regex` already has today,
      it just makes the common case faster and the syntax more complete.

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

### Gaps found comparing against mainstream editors (2026-08-23 survey)

Named so they're a conscious decision, not an oversight — being in a mainstream editor
isn't by itself a reason ned needs it too; several below are listed specifically to be
argued against, not just added.

Real, fairly uncontroversial gaps:

- [ ] **System clipboard integration** (OSC 52, or shelling out to `xclip`/`wl-copy`/
      `pbcopy`/`termux-clipboard-set` when present) for `KillRing`/yank. Kill-ring is
      purely internal today — `kill-ring-save`/`yank` never touch the terminal's real
      clipboard, so nothing killed in ned can be pasted into another app, and nothing
      copied elsewhere can be yanked into ned. Arguably more everyday friction than
      several already-tracked items, for a terminal editor especially.
- [ ] **Auto-closing/matching brackets and quotes** (typing `(`/`"`/`{` inserts the
      closer too, typing the closer again just moves past it). VSCode/Sublime/JetBrains
      default this on; Emacs treats it as an opt-in minor mode (`electric-pair-mode`) —
      ned should follow Emacs' precedent here (a togglable mode, not a forced-on
      default) rather than assume VSCode's.
- [ ] **Buffer-local completion fallback** for a file with no LSP server configured
      (Emacs' `dabbrev-expand`/`hippie-expand`, Vim's `<C-n>`/`<C-p>` keyword
      completion) — scan open buffers for word-prefix matches. LSP ghost-text
      completion (`LspManager::RequestCompletion`) is the only completion source
      today; a plain-text buffer, a README, or a language nobody's configured a server
      for gets no completion at all.
- [ ] **Live per-line VCS diff gutter** ("this line added/changed/removed since the
      last commit," recomputed as you type — Gitsigns/GitGutter's own feature) —
      distinct from the blame gutter already shipped (committed history, not
      working-tree state) and from `vcs-status` (file-level, not line-level).
- [ ] **Split-pane resize** (drag or keyboard) — `WindowManager`'s own header comment
      already documents "fixed 50/50 splits only, no drag-resize yet," but it was never
      promoted to a tracked item here.
- [ ] **Structured test-runner integration** (discover a project's test framework,
      gutter pass/fail marks per test, jump-to-failing-test) — `TaskRunner` only shells
      out and streams raw combined stdout/stderr; nothing parses a test framework's own
      result format into anything more structured than a scrollback buffer.
- [ ] **Snippet expansion** (TextMate-style tabstops — `for<TAB>` expands to a
      skeleton with fill-in fields, `<TAB>` hops between them). No bundled engine, no
      `ned/*` scripting surface for one. A sizable feature — the tabstop-cursor
      relocation-on-edit problem it needs is structurally the same one
      `Buffer::AddCursorAt`'s secondary-cursor relocation already solves, worth
      building on rather than inventing a second edit-relocation mechanism.
- [ ] Trailing-whitespace / indentation-guide visualization.
- [ ] A real visual side-by-side 3-way merge/diff view. `AutoMerge` already
      auto-resolves the common case and drops real `<<<<<<<`/`=======`/`>>>>>>>`
      conflict markers into the buffer for a genuine divergence (deliberately, so the
      auto-resolved case needs no bespoke UI at all — see `Text/ThreeWayMerge.h`) but a
      real conflict today is still hand-edited text markers, not a visual diff.
- [ ] A plugin marketplace/package registry (VSCode extensions, MELPA/straight.el).
      Ned's whole model is one Janet-scriptable environment plus opt-in project-local
      plugins (`Editor/ProjectPlugins.h`) gated by `ProjectTrust`'s hash-based,
      disuse-expiring trust registry — a marketplace implies a supply-chain-trust
      problem this project has so far deliberately stayed out of. Leaning "won't do"
      rather than "open," named here so that's a conscious call, not silence.
- [ ] A single fuzzy command palette unifying M-x/find-file/switch-buffer into one
      popup (VSCode/Sublime's Cmd+Shift+P). Real Emacs itself keeps these as separate,
      purpose-built commands with their own bindings — consistent with this project's
      stated Emacs-class-parity vision, so this reads as a different, already-chosen
      philosophy rather than an obvious gap.

### Input model: optional Vim/vi keybinding emulation (idea, unstarted — design sketch only)

Raised as a question: could ned support Vim-style modal keybindings *alongside* its
existing Emacs-style ones, not as a replacement? Genuinely feasible, and this
architecture happens to already carry most of the pieces an evil-mode-style emulation
layer (Vim-inside-Emacs, the proven precedent — also IdeaVim, VSCode Vim) is built
from, rather than needing a second parallel input stack. Kept on the roadmap on
adoption grounds specifically, not personal taste — modal editing is not this
project's own preference, but a huge fraction of the people who'd otherwise try ned
already have Vim muscle memory, and "you have to give that up" is a real adoption
tax an opt-in mode removes at no cost to anyone who doesn't touch the setting:

- **Not literally simultaneous.** A key like `d` can't mean both "self-insert" and
  "delete operator" at the same instant with no disambiguating state — that's a
  genuine conflict, not a restriction to design around. What *is* feasible, and what
  every real Vim-emulation-inside-another-editor does, is a mode flag (global setting
  or per-buffer minor mode, plus a `--vim`-style CLI flag as a third entry point) that
  selects which top `KeymapStack` layer is live: an Insert state that IS ned's existing
  default layer (self-insert + every current Emacs binding, unchanged), versus
  Normal/Visual/Operator-pending states that are new, Vim-specific layers.
  `Mode`/`KeymapStack` already treat "a minor mode is just another keymap layer, OR'd
  in by `KeymapStack::Match`" as the standing convention (see `Mode.h`'s own doc
  comment) — this reuses that unmodified.
- **Normal-mode keymap**: mechanical but large — bind the full printable-ASCII range to
  Vim commands (`h`/`j`/`k`/`l` motion, `x`/`dd`/`yy`/`p`, etc.) rather than leaving any
  of it to fall through to `self-insert-command`, since in Vim's Normal mode almost
  nothing should insert text. Ordinary `Keymap` work, just wide.
- **Operator + motion composition** (`d` + `w` deletes a word, `c` + `i` + `"` changes
  inside quotes) is the one genuinely new mechanism — Vim's operators compose with
  *any* motion generically, which `KeyChord`'s static pre-declared sequences don't
  model directly. Needs an explicit operator-pending state machine reading the next
  fully-resolved motion (itself a normal command, with its own optional count prefix)
  as a target offset rather than moving point directly, then applying delete/change/
  yank over `[point, target)` — the same *shape* `Editor/PrefixArgument.h`'s
  `PrefixArgumentReader` and `Editor/IncrementalSearch.h` already use (a pure,
  buffer-free state machine `BufferView` drives key-by-key), not a new kind of
  component for this codebase. Text objects (`di(`, `ci"`) are motions in this same
  sense and can lean on the tree-sitter infrastructure `sexpMotion`/
  `expandSelection` already parse for.
- **Visual mode** maps fairly directly onto region selection and `Rectangle.h` already
  shipped (char/line/block visual = point+mark region / line-extended region /
  rectangle selection, respectively) — mostly new bindings over existing primitives.
- **Macros and registers** need no new mechanism at all: `Dispatcher` already records
  keyboard macros natively (`q`/`@` would just be new bindings onto it), and
  `Editor/Register.h`'s single-char-keyed `RegisterTable` is already Vim-register-shaped.
- **Ex commands** (`:w`, `:%s/foo/bar/g`, `:q`) are a new `BufferView::InputMode` parsing
  ex-command syntax, mapping onto the existing `CommandRegistry` (`save-buffer`,
  `project-replace-regexp`, ...) the same way M-x already does — a parser and a mapping
  table, not new editing primitives.

Net: a genuinely large feature (comparable in scope to the Org-mode work), but not an
architectural fight — it slots in as "another major-mode-shaped subsystem," which this
codebase already does repeatedly (Org, VCS, DAP, ACP...), rather than requiring changes
to `Keymap`/`KeymapStack`/`Dispatcher` themselves. Unscoped beyond this sketch — no
estimate of which pieces would ship in a first cut.

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
