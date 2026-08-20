# Ned Project Map

## Vision

An Emacs-class editor: majority feature/behavior parity with Emacs (buffers, windows,
keymaps, modes, minibuffer, kill-ring, undo, isearch, "everything is a programmable
command") with obvious exceptions where 45 years of Elisp packages don't translate.
Janet fills Elisp's role — the whole editor is a Janet-scriptable environment, not a
C++ app with a config file bolted on. Implementation is modern, memory-safe C++23
(stdlib containers/smart pointers, no raw `new`/`delete` in editor code). The terminal UI
is rendered with FTXUI (migrated from TermOx in Phase 7 — see that phase's own entry),
pushed toward gradients/fades/advanced theming, but only after the editing core works.

## Guiding constraints (apply to every phase)

- **Memory safety**: no raw owning pointers. `std::unique_ptr`/`shared_ptr`, `std::string`/`string_view`,
  `std::vector`/`std::span`. Janet's C heap is external and stays malloc'd internally — we only
  own the C++ side.
- **Programmability first**: any new editor capability should be reachable as a named,
  interactive "command" callable from a keybinding, from Janet, and (later) from M-x —
  not hardcoded control flow in `main.cpp`.
- **Editing features before visuals.** Phases 5–6 (theming/fanciness) don't start until
  Phases 1–4 are usable as a real editor.
- **XDG Base Directory compliance.** No dot-files/dot-dirs dumped straight in `$HOME`.
  Config → `$XDG_CONFIG_HOME` (default `~/.config`), user data → `$XDG_DATA_HOME`
  (default `~/.local/share`), non-essential caches → `$XDG_CACHE_HOME` (default
  `~/.cache`), other persistent state (history, recent-files, etc.) → `$XDG_STATE_HOME`
  (default `~/.local/state`) — always under a `ned/` subdirectory, always reading the
  env var first and falling back to the spec default. Applies the first time any phase
  needs to read/write a file outside the project or an explicitly-given path.
- **Keep `Source/UI/` loosely coupled from the TUI library's own specifics where it's
  cheap to do so.** No retroactive refactor of existing widgets, and no new abstraction
  layer built ahead of need — but when a choice is free or nearly free, prefer the one
  that doesn't wire a TUI-library-specific type/idiom deeper into `Source/Editor/`-facing
  code than it needs to be. This constraint did its job once already: it's exactly what
  kept the Phase 7 TermOx → FTXUI migration a contained, `Source/UI/`-scoped rewrite
  that never touched `Source/Editor/`/`Source/Text/`/`Source/Janet/` — see that phase's
  own entry for how it played out. Kept as a standing constraint rather than retired,
  since the same discipline is what would make any *future* TUI library swap cheap too.

### Org-like structured editing

Scoped as "Org-*like*" deliberately, matching the original stub's own wording, not a
full Emacs Org mode port — real Org mode's full feature surface (below) is arguably as
large as the rest of this editor combined, and most of it (agenda views across a whole
tree of files, a spreadsheet formula language, multi-backend document export, a
publishing pipeline) is a separate, standalone subsystem-sized effort each, not
something to casually fold into one editor phase. Full feature list actually fetched
from orgmode.org for this triage, not assumed: markup/structure, tables (+ CSV
import/export + spreadsheet formulas), Org Babel (embedded, executable, 80+-language
code blocks), export backends (HTML/LaTeX/ODT/Pandoc/custom), publishing projects,
TODO keywords + custom task states + state cycling, agenda views (daily/weekly/custom),
priorities, deadlines/scheduling, tags, clocking/time tracking, capture templates,
custom link types, and Org's own extensibility framework.

**What's still not here:** tables, links, and real tree-sitter-org highlighting.

**User wishlist, recorded for later, not started this slice:**
- **Universal clickable in-buffer affordances, not just Org.** The user's own framing:
  if Org gets links/checkboxes, every mode should get equivalent "fanciness" where it
  makes sense — a file path under the cursor opening that file, a URL in a comment
  becoming a real hyperlink, possibly XDG-opening it (`xdg-open`-style) on click. Not
  scoped or designed yet; would need the same kind of click-to-action plumbing Org's own
  link-follow will eventually need, generalized across every `Mode` rather than built
  Org-specific from the start.
- **Exhaustive, per-capture-name-configurable tree-sitter highlighting.** Today's
  `SyntaxClass` (23 members, `Mode.h`) is a curated, hand-picked set of categories, not
  an exhaustive mirror of every capture name a real grammar's `highlights.scm` can
  produce (see that file's own header comment). The ask: a sensible default mapping for
  *every* capture kind tree-sitter can produce, PLUS a way to configure/override any of
  it — explicitly framed by the user as groundwork for an eventual tree-sitter-driven
  formatter ("a dprint clone that is actually awesome"), which is exactly the kind of
  thing that needs fine-grained, complete node/capture classification to do well. Ties
  directly into the existing "Companion tooling: ... tree-sitter-assisted formatter"
  entry further down this file. Not designed yet — a real scoping pass (what does
  "every possible kind of highlight" concretely enumerate to, across the 13 bundled
  grammars) would be the right first step whenever this is picked up.


**v2+ maybe**, real Org value but bigger or needing v1 to exist first:
- Agenda view (aggregate TODOs/deadlines across a whole tree of files into one buffer)
  — the single most-loved Org feature for a lot of users, but a genuinely new kind of
  UI (a synthesized, cross-file, non-editable-in-the-usual-sense view) rather than an
  extension of anything Ned already has.
- Scheduling/deadlines with real date/recurrence logic.
- Property drawers.
- Capture templates (quick-add an entry from anywhere in the editor).
- Clocking/time tracking.

**Won't, at least not soon** — the parts of Org that are each a subsystem-sized effort
in their own right:
- Org Babel (embedded, executable code blocks) — beyond the sheer scope, this is
  arbitrary code execution triggered by opening/editing a text file, a real security
  surface this project would need to design around deliberately, not something to
  bolt on casually.
- The table formula/spreadsheet engine — a small programming language of its own.
- Export backends and the publishing pipeline — each a standalone tool-sized effort.

## Wishlist (unsequenced)

Everything left that isn't sequenced or scheduled against a real phase — draw from this
once whatever's currently sequenced is solid. Phase 9 ("Zed-inspired features") and the
former standalone "Companion tooling" section have been folded into this one list rather
than kept as separate headers, now that both of Phase 9's own sequenced items (structural
selection expansion, the fuzzy file finder) have shipped as Phase 10 above and nothing
else in either list is scheduled against anything. Grouped by how big a foundational lift
each is, not by priority.

- **Language intelligence**
  - [x] Tree-sitter-based syntax highlighting — done (see "Tree-sitter foundation",
        "Mode/highlighting redesign for tree-sitter", and "Bundle remaining tree-sitter
        grammars" above); likely a prerequisite for most of the rest of this group.
  - [x] Structural/AST-aware selection expansion (expand-to-next-syntax-node) — done,
        see Phase 10 above.
  - [x] LSP client: autocomplete, diagnostics, go-to-definition, hover docs, code
        actions, rename, multi-language support. Slice 1 (core plumbing + diagnostics), slice 2
        (hover + ghost-text completion), slice 3 (code actions/quick fixes), and slice 4
        (go-to-definition + rename) are all done — see "LSP client — slice 1"/"slice 2"/"slice 3"/
        "slice 4" above. Multi-language support was never a separate slice of its own — it falls
        out of `LspServerConfig`'s existing per-language command lookup for free, one config entry
        per language. Still deliberately out of scope, unrelated to any one slice: DAP debugging
        (its own wishlist item just below), syncing every open buffer rather than just the active
        one, incremental sync, idle-timeout server teardown, multi-root workspaces, and raw
        subprocess stderr capture — see "LSP client" and "LSP client — error visibility
        follow-up" above for why each of those is a deliberate cut, not an oversight.
  - [ ] DAP (Debug Adapter Protocol) client for in-editor debugging.
  - [ ] Spell/grammar-checking, prose-oriented, as a **diagnostics channel over the
        existing LSP client**, not a bespoke hunspell/libhunspell integration (revised
        from an earlier draft of this entry that proposed spawning `hunspell -a`
        directly — superseded once it was clear multiple real checkers already speak
        LSP natively, making this a config/plumbing question rather than a new
        subsystem). Concretely:
        - **`harper-ls`** (native Rust, no JVM, does both spelling and basic grammar,
          English-dialects-only) is the preferred default — detect it on `PATH`
          (likely via the same system-probing approach the "Environment setup tool"
          companion-tooling entry below already plans for tree-sitter grammars) and
          wire it in automatically when present, no user config required.
        - **Fallback chain** when `harper-ls` isn't installed or for non-English
          content: other installed spell checkers exposed via an LSP wrapper (thin
          hunspell/nuspell-backed LSP servers exist) should be tried in order, ending
          in "no prose checking" rather than a hard failure. Also covers real grammar
          checking (e.g. `ltex-ls`, which wraps LanguageTool) as an opt-in, heavier
          alternative someone can point Janet at themselves — `ned` itself should
          never bundle or require a JVM.
        - **Not language-gated the way `clangd`/`pyright` are.** Spelling should
          attach to any prose-shaped buffer (Markdown, org, plain text, commit
          messages, maybe comments/strings later) independent of whichever primary
          language server is already bound to that buffer for code intelligence —
          this is *not* the existing per-language `LspServerConfig` lookup, which
          picks exactly one server per buffer today.
        - **Open architectural question, resolved as "not today":** checked directly
          against `Source/Editor/Lsp/LspManager.h` — it's strictly 1:1, `bufferState_`
          keyed by `Buffer*` holding one `BufferSyncState{language, ...}`, `clients_`
          keyed by language name. Real work needed is multi-server-per-buffer
          diagnostics merging (e.g. harper-ls + clangd both annotating the same whole
          buffer), not adding a checker — that's the actual size of this item, not
          "just another server config entry."
        - **Embedded-language documents (HTML with inline `<script>`/`<style>`, Vue/
          Svelte-style single-file components) are explicitly a separate, bigger
          problem from the above**, raised in the same discussion: no LSP server
          natively spans multiple languages in one file. The real editors that handle
          this (VS Code's HTML/CSS/JS services, Volar, the Svelte language server) all
          segment the document into per-language virtual sub-documents and forward
          each to its own server, remapping every position in the response back
          through an offset map. Tree-sitter injection queries (the same mechanism
          `nvim-treesitter` uses) are the natural way to find those region boundaries
          here, since `ned` already builds a real parse tree per buffer — but the LSP
          side would need `bufferState_` to go from "one language per buffer" to "N
          (language, byte-range) regions per buffer," each syncing its own virtual
          document. Large enough to be its own follow-up once multi-server-per-buffer
          is solved, not bundled into this entry.
        - **Must never run against binary buffers** — key off the existing
          binary/text classification (`RequestOpenBinaryFile`/large-file-async-load
          machinery) rather than inventing a new check.
        - A Janet plugin author should already be able to register any LSP-speaking
          checker (spelling, grammar, or both) themselves via existing
          `LspServerConfig`-style hooks once the multi-server question above is
          resolved — this entry is about making a good one work out of the box, not
          about needing new plugin infrastructure beyond that.
        Raised by the user alongside the LSP design discussion; not scoped in detail
        yet — grammar-checking in particular is explicitly not something `ned` needs
        to build itself, only support as a pluggable LSP-backed checker.
- **Navigation & search**
  - [x] Fast fuzzy file finder / command palette — done, see Phase 10 above.
  - [ ] Multibuffers: a virtual buffer stitching together excerpts from multiple
        files/locations (e.g. all references, all diagnostics, as one scrollable view)
        — a genuinely interesting fit for our Rope/Buffer design, worth a design pass
        of its own when it comes up.
- **Large file handling** (large-file-async-load / open-binary-anyway follow-ups laid the
  groundwork here: binary-content refusal with an explicit override — CLI `--force-binary`,
  an interactive y/n confirmation reachable from find-file, a sidebar click, or a CLI-opened
  path alike, all routed through the same `BufferView::RequestOpenBinaryFile`/
  `WindowManager::RequestOpenBinaryFile` pathway — and a background-thread chunked loader
  with a live-growing preview for files over `kAsyncLoadThreshold` (16 MiB), so opening a
  large *legitimate* text file no longer blocks the UI. Everything below is what's still
  unaddressed, raised together during that same work.)
  - [ ] **Windowed/paged editing for genuinely huge files** (multi-GB — the user's own
        framing: "anything over a few hundred megabytes sounds crazy" to hold fully
        resident, even as fast as you can scroll). Explicitly discussed and scoped as a
        real architectural direction, not a tweak to the current async loader: `Rope` is
        fundamentally in-memory (a structurally-shared B-tree over the *entire* content),
        and everything built on it — `UndoTree` snapshots, whole-buffer search, tree-sitter,
        line/byte-offset counting — assumes the whole file is addressable. A true windowed
        buffer needs a second, disk-backed text-storage engine, not a change to this one.
        Recommended shape, not yet designed in detail: treat it as a **read-only,
        mmap-backed viewer** past a size threshold — lazily build a line-offset index only
        near wherever the user has scrolled (not the whole file up front), page content in
        as the viewport moves, and require an explicit "load this fully to edit it" step
        (reusing the existing async loader) before allowing real edits. Deliberately *not*
        attempting in-place windowed editing of an untouched-elsewhere file: an edit before
        the visible window shifts every later byte offset, so genuine windowed editing needs
        a real piece-table with disk-backed runs — most of a new engine, not a follow-up.
  - [ ] `kAsyncLoadThreshold` (`BufferList.cpp`) and `kMaxHighlightBytes`
        (`BufferView.cpp`) are both hardcoded C++ constants today, the same "hardcoded for
        now" scope cut `TabWidth`/`Theme` selection originally were before they grew a
        Janet-facing setter (`ned/set-tab-width`, etc.) — expose both the same way once
        there's a real need to tune them per project/machine rather than guessing at one
        number that fits everyone.
  - [ ] `ModeLine`'s "Loading…" indicator (open-binary-anyway/large-file-async-load
        follow-ups) is a plain binary state today, not a live percentage — `AsyncFileLoader`
        tracks bytes-read/total-bytes as local, per-chunk values inside its own background
        thread but never surfaces them anywhere UI-reachable, specifically to avoid the
        cross-thread-safe-accessor plumbing that would need (Buffer itself can't hold the
        progress atomics directly — it's moved into a `unique_ptr` on open, and
        `std::atomic` isn't movable). Worth adding once wanted: likely a small
        `shared_ptr<LoadProgress>` handed from `AsyncFileLoader` to `WindowManager`, queried
        by `ModeLine::Paint` for whichever buffer is active.
  - [ ] The async loader only ever fires for files opened *after* `EventLoop` exists
        (`main.cpp`) — a file passed directly on the command line (`ned hugefile.txt`,
        not binary, just large) still loads synchronously before the UI ever appears,
        since `BufferList`/the initial `OpenOrCreateFile` call both run before `EventLoop`
        is constructed. Only the *binary-refusal* half of that same gap was actually
        closed (the deferred `RequestOpenBinaryFile` call right after
        `windowManager->TakeFocus()`) — a large-but-legitimate CLI-opened file has no
        equivalent deferral yet. Would need the same "try, catch, defer" shape, just
        triggering a deferred *load* instead of a deferred *confirmation prompt*, and
        general enough that it might be worth restructuring startup to construct
        `EventLoop` earlier instead — flagged in the async-load follow-up as out of scope
        specifically because of the risk of disturbing `TerminalColorProbe`'s own strict
        "before anything else reads stdin" ordering requirement; a real evaluation of that
        risk hasn't happened yet.
- **External tool integration (version control and beyond)**
  - [x] VCS-agnostic version control, via a plugin system rather than a hardcoded git
        integration — the `blame`/`log` slice of the vocabulary is done (blame gutter +
        `*vcs blame <file>*`/`*vcs log <file>*` multibuffer follow-up); `status`/`diff`/
        `stage`/`unstage`/`commit`/`branch` are reserved in the vocabulary but not yet
        implemented, see below. The user's own framing, realized as designed: stay
        deliberately agnostic about which VCS a project uses — a plugin (a Janet script,
        the natural fit given this project's own "everything is programmable, Janet fills
        Elisp's role" foundation, `Source/Janet/`, rather than a new plugin-language/
        runtime) translates a small internal vocabulary into whatever a specific VCS
        actually needs, so editor-facing commands/keybindings/UI stay the same regardless
        of git vs. Mercurial vs. Subversion vs. jj. Concretely: `Source/Editor/Vcs/
        VcsProvider.h` is the Janet-free interface (`Detect`/`BlameArgv`/`ParseBlame`/
        `LogArgv`/`ParseLog`, plus the reserved-but-unimplemented vocabulary named in a
        comment); `Source/Editor/Vcs/VcsProviderRegistry.h/.cpp` is the process-wide
        registry (`RegisterProvider`/`ActiveProviderFor`, first-`Detect`-match-wins,
        mirroring `ModeOverrides.h`'s own mutex-guarded-static-state pattern);
        `Source/Janet/JanetVcsProvider.h/.cpp` is the adapter a Janet plugin's five
        callbacks (`detect`/`blame-argv`/`parse-blame`/`log-argv`/`parse-log`) get wrapped
        in, reached via `ned/vcs-register-provider` (`Source/Janet/EditorBindings.cpp`);
        `Source/Janet/Plugins/vcs-git.janet` is the bundled reference implementation for
        git, embedded into the binary at CMake configure time via a new
        `ned_embed_janet_plugin` function mirroring `ned_embed_treesitter_query`'s own
        "generate a `.cpp` constant from a checked-in source file" approach (this
        codebase has no `Resources/`-style loose-runtime-file convention), loaded by
        `Source/Janet/PluginLoader.h/.cpp`'s `LoadBundledPlugins` right after
        `InstallEditorBindings` and before the user's own `init.janet` (so it can
        override/unregister a bundled provider). `Source/Editor/Vcs/VcsRunner.h/.cpp` is
        the async execution glue: every VCS operation is split into a `build-argv`
        callback and a `parse-output` callback, both cheap/synchronous and only ever
        called on the main thread (required, not a style choice — see `VcsProvider.h`'s
        own header comment for the Janet-threading constraint this satisfies, the same
        `janet_pcall`-corruption landmine `Value.h`'s `RootedValue` CAUTION comment
        documents), with the actual subprocess spawn/wait done via the existing
        `TaskProcess`/`ChildProcess` on a background thread in between. `BufferView`'s
        blame gutter (`kBlameWidth`, `blameLineInfo_`, `EnsureBlameGutterCache`,
        `RequestBlameForCurrentBuffer`) is the rightmost gutter column (layout is now
        `[status][diagnostic][gap][digits][gap][fold][blame]`), populated only by an
        explicit `vcs-show-blame` (`C-c v b`) request — never auto-recomputed per
        `Paint()` the way the diagnostic/fold gutters are, since there's no cheap
        synchronous source to recompute from; it goes stale and clears (not
        resynthesizes) the instant the buffer's content generation changes, an honest
        "blank" rather than silently-wrong attribution. `vcs-show-blame` deliberately
        stays on the current buffer rather than switching away to a results buffer —
        revised after the original default (jump straight to a separate `*vcs blame*`
        buffer) was tried and reported back as disconnected from the code actually being
        read, not useful as the default action; `vcs-blame-detail-at-point`
        (`C-c v i`) is the "on request" companion, a synchronous read of already-loaded
        gutter data reporting the full author/date/summary for point's line via the
        status line, since the gutter's own fixed-width column only ever fits a short
        hash. The separate full-history view is still available (`vcs-blame-buffer`,
        M-x only, matching `lsp-show-log`'s own no-dedicated-binding precedent) for
        anyone who wants it, just no longer what a bare "show blame" reaches for.
        `vcs-show-log` (`C-c v l`) and `vcs-visit-result` (`C-c v v`, reusing
        `VisitSearchResult`'s exact `path:line:` parsing via a new shared
        `JumpToPathLine` helper) round out the multibuffer half.
        Still explicitly out of scope, reserved for later: `status`/`stage`/`unstage`/
        `commit`/`branch` (named in `VcsProvider.h` as comments, no real methods yet)
        and a full historical multibuffer beyond one synthesized buffer per query
        result (the fuller "Multibuffers" wishlist entry below is still its own,
        larger, unscoped thing). The "generalizable past version control" framing
        (cloud-provider CLIs, Terraform, Docker via the same two-callback-per-operation
        plugin shape) is unchanged and still just a framing, not attempted.
  - [x] Diff gutter markers (added/modified/removed line indicators, live-refreshing
        against HEAD) — the follow-up the blame gutter's own `[ ]` originally deferred,
        now done. `VcsProvider::DiffArgv`/`ParseDiff` round out the vocabulary
        (`VcsDiffHunk{oldStart, oldCount, newStart, newCount}`, the same fields a
        unified diff's own `@@ ... @@` hunk header carries); `vcs-git.janet`'s
        `diff-argv` runs `git diff --no-color -U0 --` (no context lines -- exactly
        what a gutter marker needs) and `parse-diff` parses the header (own small
        `parse-range`/`parse-hunk-header` helpers, handling the "count omitted when
        it's 1" shape all three of add/modify/delete hunks can take, verified against
        real `git diff -U0` output for all three during development, including the
        trickiest single-line no-comma case). `VcsRunner::RequestDiff` mirrors
        `RequestBlame`/`RequestLog` exactly. Unlike blame, **this is live, not
        on-request** — a deliberate revision after the user tried blame's own
        on-request-only default and found a *disconnected* separate results buffer
        "completely useless," then explicitly asked for live gutter markers instead,
        "fancy and modern," with an explicit "not stupid about it" debounce ask.
        Concretely: `BufferView::RunCommandAndHandleOutcome` (the one choke point
        every dispatch passes through) arms a `kDiffRefreshDebounce` (1200ms,
        hardcoded C++ for now, same scope cut `TabWidth`/`Theme` selection originally
        were) `DeadlineTimer` on any content-changing edit — mirrors
        `completionDebounceTimer_`'s own re-arm-cancels-the-pending-fire shape exactly
        — and bypasses the debounce entirely (refreshes immediately) the instant
        `Buffer::Modified()` transitions true -> false, i.e. a real save; switching to
        a newly-active buffer (`Paint()`'s own `modeSyncBuffer_` check) also refreshes
        immediately, clearing the previous file's markers first so nothing shows the
        wrong file's state even briefly. Every failure path (no path, no provider,
        process failure) degrades silently — a live background feature must never
        interrupt with a status message the way `vcs-show-blame`'s own on-request
        errors do. Rendering, per the "be as fancy as you like" brief: a new,
        deliberately **leftmost** gutter column (`kDiffWidth`, layout now
        `[diff][status][diagnostic][gap][digits][gap][fold][blame]` — the one place
        this feature set broke its own "append new gutters at the rightmost edge"
        precedent, specifically to match where VS Code/GitLens/vim-gitgutter
        conventionally put their own change bars) shows a solid color swatch for
        Added (bright green) / Modified (bright blue) lines and a thin
        "▔" (upper-one-eighth-block) notch in bright red marking a deletion boundary
        (no swatch — a deletion doesn't cover a real line, it sits between two).
        Added/Modified lines also get a subtle whole-line background wash — genuinely
        alpha-blended-*looking* despite this codebase's `Color`/`Cell` model having no
        real alpha channel, via `Color::Interpolate` blending a low percentage
        (`kDiffLineTintAlpha`, 0.14) of the accent color into the theme's own
        background, the same technique the blame gutter's own commit-age coloring
        already established — skipped entirely when selection/isearch highlighting
        also applies to a character, since a three-way blend there would read as
        muddy rather than fancy.
- **Collaboration & AI**
  - [ ] Real-time collaborative editing (CRDT-based shared sessions) — the biggest
        lift in this list; revisit only once the single-user core is solid.
  - [ ] AI-assisted editing (inline completion, chat with codebase context) — natural
        fit for Janet given "everything programmable," likely implementable as a Janet-
        scriptable integration rather than something hardcoded in C++. Raised alongside
        the fuzzy-candidate-list-styling follow-up's own note (Phase 10 above) on this
        codebase's total lack of a floating/popup/overlay widget concept — an
        LLM-integration panel is exactly the kind of overlay-shaped need that note flagged
        as worth reconsidering that gap for, not settled by anything shipped so far.
- **Editor ergonomics**
  - [ ] Multiple cursors / multi-cursor editing — explicitly deferred, not started, after
        a real design pass during the keybinding-audit follow-up (see
        `Docs/KeybindingAudit.md`'s "Maybe want" bucket) surfaced enough real scope to be
        its own phase rather than a session's work. What's already in place, and what
        genuinely isn't:
        - **Already there**: the riskiest-looking piece turns out to already be solved.
          `Buffer::RelocateForInsert`/`RelocateForDelete` (private static helpers,
          generic-code-folding follow-up, see that entry above) are the one shared
          relocation primitive `Point_`/`Mark_`/`NarrowedRange_`/`FoldMarkers_` already
          all route through on every edit — explicitly justified at the time by this
          exact future need (multi-cursors named directly in that entry's own
          reasoning). A `Buffer::Cursor{point, optional<mark>}` list could reuse this
          primitive unchanged, just looped over N entries instead of 2 fixed fields.
        - **Still genuinely missing**:
          - *Undo grouping.* `UndoTree` records/amends one Rope snapshot per logical
            edit; a single keystroke applied at N cursor positions needs to undo as one
            step, not N — no batching mechanism exists today, and every content-mutating
            `Buffer` method (`InsertAt`, `InsertAtPoint`, `DeleteRange`,
            `DeleteForwardAtPoint`, `DeleteBackwardAtPoint`) currently records/amends
            individually per call.
          - *Downstream single-mark assumptions.* `kill-region`/`kill-ring-save`/
            rectangles/registers/`narrow-to-region`/`toggle-line-comment`'s region logic
            (Phase 9 keybinding-audit follow-up work) all assume exactly one point/mark
            pair — multi-cursor needs an explicit decision on whether these become
            per-cursor, stay single-cursor-only, or are simply unsupported while
            multiple cursors are active.
          - *Rendering + input.* `BufferView` paints one caret and one selection
            highlight per frame today; N of each, plus new cursor-management commands
            (select-next-occurrence, add-cursor-above/below, collapse-to-one) and their
            keybindings, none of which exist yet.
          - Mouse-driven cursor creation (Alt+Click, the common cross-editor
            convention) is separately flagged **Don't want** in the keybinding audit —
            downstream of this feature and additionally mouse-dependent in a terminal
            app where a meaningful fraction of usage is over SSH/tmux with imperfect
            mouse passthrough.
        - Revisit as its own phase once picked up, not folded into an unrelated
          feature's scope — closer in size to Phase 7 (TermOx → FTXUI migration) or
          Phase 8 (window splitting) than to a single keybinding follow-up.
  - [ ] **Task runner** (build/test tasks from within the editor) — sequenced ahead of
        the terminal panel below, not bundled with it, after a real discussion of how the
        two relate. Shape: spawn a subprocess (build/test command, Janet-configured, not
        hardcoded), stream its stdout/stderr into a read-only, live-appended buffer —
        Emacs `*compilation*`-style, the same "read-only, tossable buffer" convention
        project-search results already use — rather than a full interactive session.
        Deliberately reuses `Source/Editor/Lsp/Transport.h`/`LspClient.h`'s existing
        async-subprocess/JSON-RPC-over-stdio plumbing as its transport layer instead of
        writing new process-management code from scratch — the same
        spawn/read-async/marshal-onto-main-thread-via-`Post` shape `LspManager` already
        has, just without the JSON-RPC framing for a plain build command (real JSON-RPC
        framing only matters once this same layer is reused for something that speaks
        it — see below).
        - **Deliberately scoped as reusable ACP-client transport groundwork.** Raised by
          the user alongside interest in a future Agent Client Protocol (ACP) integration
          for LLM-assisted editing (see "AI-assisted editing" above) — ACP agents, like
          LSP servers, are a persistent subprocess speaking structured JSON-RPC over
          stdio, not an interactive pty session. That makes the task runner's own
          subprocess/streaming transport the right thing to build *now* in a way a
          future `AcpManager`/`AcpClient` could reuse (mirroring `LspManager`/
          `LspClient`'s own split), rather than an unrelated one-off. Not building ACP
          support itself here — just not building the task runner's transport in a way
          that would need throwing away later.
        - **Explicitly not a terminal emulator** — no pty, no VT100/xterm escape-sequence
          interpretation, no interactivity (no stdin forwarding to the child process). A
          build/test command's own output is what needs to be readable, not a live shell
          session; see the terminal panel entry below for why that's a genuinely
          different, much bigger problem deliberately kept separate.
  - [ ] **Built-in terminal panel** — a real interactive pty
        (`forkpty`/`posix_openpt`) plus VT100/xterm escape-sequence emulation (cursor
        movement, colors, resizing, an actual interactive shell), not a variant of the
        task runner above. Considered together with the task runner and deliberately
        *not* merged with it: unlike a build command's own output, this needs genuine
        terminal emulation to be usable for arbitrary shell use, and buys nothing toward
        a future ACP integration (an ACP agent is a JSON-RPC peer, not something driven
        through a terminal emulator) — the two were originally one bullet point, split
        once it was clear they're different-sized problems solving different needs, not
        two slices of the same feature. No design pass done yet; revisit once the task
        runner's own subprocess-transport layer exists, since a real terminal panel would
        still want its own separate pty-backed path rather than reusing that transport.
  - [ ] Remote development (SSH remote editing).
  - [ ] Per-file session persistence: remember each file's last point/scroll position
        (keyed by absolute path) across restarts, restored on `find-file`/`ned <path>`,
        persisted under `$XDG_STATE_HOME/ned/` per this project's own XDG convention.
        Raised alongside the buffer-switch scroll-clamping fix (`BufferView::topLine_`
        is currently BufferView-level, not per-buffer) — a real prerequisite would be
        moving last-viewed position to live per-`Buffer` first, which this would then
        also persist to disk; scoped as its own slice rather than folded into that fix.
- **Project documentation output** (documentation *of* `ned` itself — user/config/Janet-API
  reference — not an in-editor document viewer; this project has none today beyond
  `README.md`/`ROADMAP.md`/`CLAUDE.md` prose)
  - [ ] Some real framework for producing `ned`'s own documentation in multiple output
        forms — man page(s), PDF, and a rendered web page/site at minimum, ideally all
        generated from one shared source rather than maintained independently by hand in
        three formats that inevitably drift. Concretely likely means: author the actual
        content once (Markdown, or a doc-comment-adjacent convention pulled from
        `ned/*`-binding doc strings already passed to `Register<Fn>` in
        `Janet/EditorBindings.cpp` — a real, already-structured source of per-function
        documentation that goes nowhere today beyond Janet's own introspection), then run
        it through an existing toolchain rather than writing a bespoke multi-format
        generator: `pandoc` (Markdown -> man/PDF/HTML in one tool, the most direct fit) or
        a static-site generator plus `pandoc`/`groff` for the man/PDF legs specifically.
        Not scoped in detail yet — raised as a real, wanted capability (having something
        beyond three plain top-level `.md` files as this project matures), not a specific
        design; likely belongs near the "Companion tooling" entry below once picked up,
        as a build-time/release-time step rather than anything `ned` itself needs to do at
        runtime.
- **Visual**
  - [ ] Minimap. Rich built-in theme set. (Overlaps with Phase 6.)
- **Companion tooling** (standalone utility programs shipped alongside `ned`, not part of
  the editor binary itself — the user's own framing: "towards the end of our dev, maybe
  they even belong in their own phase, when we're starting to setup the outside tooling")
  - **Environment setup tool** (name TBD, e.g. `ned-setup`). Detects and initializes the
    first-run environment: shell integration, and — the piece that directly builds on the
    mode-overrides/dynamic-grammar-loading work above — scans for tree-sitter grammars
    already installed on the system (the same `libtree-sitter-<name>.so` +
    `queries/<name>/highlights.scm` convention explored while building that phase, though
    the exact layout varies by distro/OS and isn't something to hardcode into `ned`
    itself, only into this separate, inspectable tool) and generates a Janet script —
    loaded *from* `init.janet`, not silently run by `ned` itself — wiring up
    `ned/register-language-grammar`/`ned/set-mode-for-extension`/`ned/set-mode-for-filename`
    calls for whatever it found. Runs automatically on first launch if `ned` finds no XDG
    config for itself yet (per the user's own suggestion), and presumably also runnable
    standalone/re-runnable later. A real, standalone generator producing a concrete,
    editable file is the right shape for this — not silent runtime auto-detection baked
    into `ned`'s own startup path, which was explicitly considered and rejected during the
    dynamic-grammar-loading follow-up for the same non-portable-system-layout reason (see
    that phase's own `ROADMAP.md` entry).
  - **Post-save formatter with tree-sitter-assisted, JetBrains-level configurability.** The
    existing `FormatOnSave.h/.cpp` mechanism (format-on-save follow-up) only ever shells
    out to one external, already-opinionated formatter (clang-format, dprint,
    php-cs-fixer, ...) configured via `ned/set-format-command` — this is a different, much
    larger idea: a real formatter built on `ned`'s own tree-sitter parse, with rule
    granularity closer to JetBrains' per-language formatters than any single external tool
    the user's tried has offered. Flagged honestly as the bigger of the two asks —
    genuinely a large, open-ended undertaking (a real configurable formatter is a
    substantial project in its own right per language, not a small utility), not something
    to scope in detail this far out. Revisit once there's a concrete phase for it, likely
    informed by exactly which per-language formatting gaps external tools kept leaving
    unfilled.

---

## Decisions made during development

~~Text storage data structure.~~ Originally proposed a gap buffer, but revised before
writing any code: a persistent rope over UTF-8 bytes (`ropey`/Xi lineage), because (a)
full grapheme-cluster correctness was pulled into Phase 1 scope, which meant the
"simpler" codepoint-array storage stopped paying for itself, and (b) gap buffers are
cheap near the last edit and O(n) everywhere else, which is a bad match for
Janet-driven programmatic edits scattered across a buffer — not an edge case here,
since "everything is programmable" is core to the project, not incidental usage.

~~Test framework.~~ Catch2 (see Phase 0).

~~Unicode granularity.~~ Full grapheme-cluster correctness from the start (`utf8proc`
for UAX #29 segmentation), not deferred codepoint-only handling.

~~Undo model.~~ A real undo tree (see Phase 1), not Emacs' flat list — a deliberate
"do it better" call the user signed off on, since it keeps Emacs' non-destructive
history guarantee without the "redo by undoing the undo" surprise.

~~Kill-ring scope.~~ Global across buffers, Emacs-style.

~~Home-directory hygiene.~~ XDG Base Directory compliance (see guiding constraints
above) — first relevant when Phase 3's init-file loading lands.

## Notes for whoever builds next

- Build/test verified via `build/` (Unix Makefiles): `cmake -S . -B build && cmake --build build`,
  then `ctest --test-dir build`. Sanitizer opt-in verified separately with
  `-DNED_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug`.
