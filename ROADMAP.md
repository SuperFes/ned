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

**The v1 scope is now fully shipped** — tree-sitter-org highlighting landed as its
own follow-up (`OrgMode()` in `Mode.cpp`, on Ned's own forked "org" grammar in
`TreeSitter/Languages.cpp`); what remains Org-wise is the "v2+ maybe" and wishlist
material below. Tables slice 1
(parsing, column alignment, TAB cell motion via `org-cycle`/`org-table-align` on
`Source/Editor/Table.h`'s shared toolkit) and links both shipped earlier; tables
slice 2 (2026-08-19) closed slice 1's explicitly deferred editing surface: TAB past
the last cell now appends a new empty row (real Org's own behavior, replacing slice
1's wrap-to-first-cell), `S-TAB` steps backward (`org-table-previous-cell`),
`M-S-up/down` kill/insert rows (`org-table-kill-row`/`org-table-insert-row`),
`M-S-left/right` delete/insert columns, `M-left/right` move columns, `C-c -` inserts
an hrule, and `M-up/down` are real Org's own `org-metaup`/`org-metadown` context
dispatch (table row move inside a table, refusing at the table's edge rather than
dragging a row out; the global `move-line-up`/`move-line-down` behavior everywhere
else) — all built on one shared locate-cell/rewrite-block core factored out of slice
1's align machinery (`Org.cpp`'s `RewriteOrgTable`), every op realigning as a side
effect. Markdown (GFM) tables deliberately did not get the editing surface this
slice — its delimiter row carries per-column alignment state a column op would have
to rewrite; a follow-up if wanted. Formulas stay a "won't, at least not soon," below.

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
  - [x] DAP (Debug Adapter Protocol) client for in-editor debugging. Scoped
        2026-08-19 per the user's own framing: "the useful things — step through,
        inspect, and all that good stuff," with F-keys as the primary bindings.
        **All three slices shipped 2026-08-20.** Slice 1: `Source/Editor/Dap/`
        (`DapConfig` — per-language adapter argv + opaque launch-config JSON,
        configured via `ned/set-dap-adapter`/`ned/set-dap-launch`; `DapClient` — the
        DAP envelope over `Lsp/Transport.h`'s reused framing; `DapManager` —
        single-session lifecycle, breakpoint store, initialize/launch/
        configurationDone handshake, stopped/terminated/exited events), commands
        `dap-continue` (`F5`)/`dap-stop` (`S-F5`)/`dap-toggle-breakpoint`
        (`F9`)/`dap-pause` (M-x), stopped-event jump-to-source via `WindowManager`'s
        focused pane. Slice 2: stepping (`dap-step-over` `F10`, `dap-step-into`
        `F11`, `dap-step-out` `S-F11`), the leftmost debug gutter column (breakpoint
        `●`, execution `▸` — reserved only while the active buffer has breakpoints
        or the stop; layout `[dap][diff][status][diagnostic]...`), and an
        execution-line background wash (three new `Theme` fields, round-tripped
        through `ThemeFile`). Slice 3: `dap-show-debug` builds a read-only `*debug*`
        buffer — frames in the `path:line:` convention so `C-c C-v` visits them for
        free, scope variables with `[ref:N]` markers `dap-expand-variable` drills
        into in place — and `dap-evaluate` prompts for an expression evaluated in
        the stopped top frame ("repl" context). All tested against scripted fake
        adapters over the real framing (`Tests/Dap*Test.cpp`, plus the
        `BufferViewTest` gutter/highlight renders). Deliberate v1 cuts, recorded as
        decisions: attach mode; thread picker (acts on the `stopped` event's own
        thread); watch expressions; conditional/logpoint breakpoints; showing
        adapter-*verified* breakpoint positions (the gutter shows where the user
        toggled); setting variables; a REPL console; persisting breakpoints across
        restarts (pairs with the per-file session persistence entry below).
        - **Protocol shape / transport reuse.** A DAP adapter is a persistent
          subprocess speaking `Content-Length`-framed JSON over stdio — the *identical*
          base framing LSP uses (DAP inherited it), so `Source/Editor/Lsp/Transport.h`'s
          `WriteFrame`/`ReadFrame` are reusable as-is, exactly the reuse the task
          runner already proved out. What differs is the message layer above the
          framing: DAP is *not* JSON-RPC — it has its own `seq`/`type`
          (`request`/`response`/`event`) envelope, and unsolicited `event` messages
          (`stopped`, `terminated`, `output`, ...) are the heart of the protocol, not
          an edge case. So: new `Source/Editor/Dap/` mirroring `Lsp/`'s own split —
          `DapClient` (protocol state machine per session, background read thread
          marshaling onto the main thread via `ned::ui::EventLoop::Post`, same
          threading contract `LspClient` documents), `DapManager` (session lifecycle,
          buffer/UI liaison), `DapAdapterConfig` (per-language adapter command lookup,
          mirroring `LspServerConfig`, configured from Janet — e.g. `debugpy` for
          Python, `lldb-dap` for C/C++, whatever the user points it at; no adapter
          bundled or auto-detected, same posture as LSP servers).
        - **v1 feature set** (the "useful things"): launch (not attach) with a
          Janet-configured program/args per project; source-line breakpoints
          (toggle at point, shown as a gutter marker — the gutter already has a
          multi-column layout to slot into); run control — continue, pause,
          step over, step into, step out; on a `stopped` event, jump to the
          stopped file:line (open via `BufferList::OpenOrCreateFile`, same as
          `VisitSearchResult`) with a distinct current-execution-line highlight in
          `BufferView` (a theme-colored line overlay, same mechanism as the
          selection/isearch overlays); stack trace + scopes/variables inspection
          rendered as a read-only, tossable `*debug*` buffer (the established
          `*search results*`/`*compilation*` convention — no new floating-panel
          infrastructure, which still doesn't exist; frames/variables are lines,
          `RET`/`C-c C-v`-style visit-the-line navigation drills in: visiting a
          frame line jumps to its file:line, visiting a composite variable line
          expands it via a `variables` request); `evaluate` on a prompted
          expression (`MinibufferPrompt` in, echo area/status line out).
        - **F-key bindings** (the user's explicit ask; `KeyTranslation.cpp` already
          delivers `F1`–`F12`, currently unbound globally): the VS/JetBrains-standard
          set — `F5` start/continue, `S-F5` stop, `F9` toggle breakpoint, `F10` step
          over, `F11` step into, `S-F11` step out. All registered as normal
          `CommandRegistry` commands (`dap-continue`, `dap-toggle-breakpoint`, ...)
          so they stay reachable from `M-x`/Janet/rebinding uniformly.
        - **Deliberate v1 cuts** (recorded so they read as decisions, not gaps):
          attach mode; multi-threaded debugging beyond "act on the thread the
          `stopped` event names" (no thread picker); watch expressions (one-shot
          `evaluate` covers the need); conditional/logpoint breakpoints; setting
          variables; REPL-style debug console (the `*debug*` buffer + `evaluate`
          prompt cover v1); persisting breakpoints across restarts (pairs naturally
          with the per-file session persistence entry below whenever that lands).
        - **Suggested slices**, LSP-style: slice 1 — `Dap/` plumbing: adapter spawn,
          initialize/launch/configurationDone handshake, `setBreakpoints`, `stopped`/
          `terminated` events, continue, stop; jump-to-stopped-line. Slice 2 —
          stepping (over/into/out), current-execution-line highlight, breakpoint
          gutter markers. Slice 3 — `*debug*` buffer: stackTrace/scopes/variables
          rendering + drill-in navigation, `evaluate` prompt. Each slice lands with
          its own unit tests against a scripted fake adapter over the real framing,
          the same way `Tests/TaskRunnerTest.cpp`/the LSP tests already fake their
          subprocess peer.
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
  - [x] `kAsyncLoadThreshold` and `kMaxHighlightBytes` — done (loose-ends cleanup,
        2026-08-20): both grew Janet setters taking byte values
        (`ned/set-async-load-threshold`, `ned/set-max-highlight-bytes` — bytes, not a
        MiB unit, since Janet callers can just write `(* 32 1024 1024)`; 0 means
        "async-load everything" / "highlight nothing" respectively, falling out of the
        comparisons rather than being special cases). The threshold's setting lives as
        `text::SetAsyncLoadThreshold` beside `BufferList` (its consumer — the text
        layer can't depend on `Editor/`'s settings files); the highlight cap became
        `Editor/HighlightSettings.h/.cpp`, the `TabWidth.h` pattern verbatim.
  - [x] `ModeLine`'s "Loading…" live percentage — done (loose-ends cleanup,
        2026-08-20), via a simpler wiring than the sketch here guessed: the
        `shared_ptr<LoadProgress>` (atomic bytesRead + write-once totalBytes,
        `Text/Buffer.h`) is held by the placeholder *Buffer itself*
        (`SetLoadProgress`/`CurrentLoadProgress`, cleared by `FinishLoad`) — a
        shared_ptr moves fine even though the atomic inside it can't, which dissolves
        the movability constraint that motivated routing through `WindowManager`.
        `AsyncFileLoader` bumps bytesRead per chunk; `ModeLine::Paint` renders
        `Loading... NN%`, clamped at 100 (a file can grow mid-load) and omitted
        entirely when the size query failed (totalBytes 0).
  - [x] Async loading for a large CLI-opened file — done (loose-ends cleanup,
        2026-08-20), via the "same shape as deferredBinaryOpenPath" option rather than
        restructuring startup (the EventLoop-earlier idea stays unevaluated, now
        without a motivating need): `main.cpp` checks the first path argument's size
        up front and, when it's over `AsyncLoadThreshold()` and not binary (binary
        stays on the sync path so `BinaryFileError` still reaches its interactive
        confirmation), skips the synchronous open entirely, remembering the path as
        `deferredLargeOpenPath`; right after `EnableAsyncFileLoading` wires the async
        opener hook, the deferred `OpenOrCreateFile` returns an `IsLoading()`
        placeholder the background loader fills in, focused via
        `FocusedActiveBuffer().Set` exactly like any interactive open. A scratch
        buffer created purely as the pane's stand-in for that launch is retired once
        the real buffer is showing. Known remaining sliver, recorded not fixed:
        buffers restored by a *project session* open before the hook exists too, so a
        huge file inside a restored session still loads synchronously at startup.
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
        ~~Still explicitly out of scope, reserved for later: `status`/`stage`/`unstage`/
        `commit`/`branch` (named in `VcsProvider.h` as comments, no real methods yet)~~ —
        **vocabulary completion shipped 2026-08-20** ("a rich set of support will always
        win," the user's own call picking this over the other wishlist candidates):
        - **Vocabulary**: `VcsProvider` grew `StatusArgv`/`ParseStatus`
          (`VcsStatusEntry{state, path}` — state kept verbatim as the VCS's own short
          code, e.g. git porcelain's "XY" column, the same "don't reinterpret
          VCS-specific text" call `VcsBlameLine::date` made), `StageArgv`/`UnstageArgv`
          (whole-file), `CommitArgv(root, message)`, `BranchListArgv`/`ParseBranchList`
          (`VcsBranchEntry{name, current}`), `BranchSwitchArgv`/`BranchCreateArgv`.
          Operations without a parse half succeed on exit code 0 alone; commit's first
          output line (git's own "[main abc1234] message") is passed through as the
          status-line summary. Every operation but `Detect` is now default-throwing
          ("<op> not supported by this provider") rather than pure virtual — the
          original blame/log/diff trio was converted to match, so a partial provider is
          one consistent concept; the throw travels `VcsRunner`'s existing onError path
          into a status-line message (unit-tested end to end).
        - **Registration is now a table of callbacks** — `(ned/vcs-register-provider
          "git" {:detect fn :blame-argv fn ...})`, sixteen keys, only `:detect`
          required. A deliberate clean break from the days-old 7-positional-argument
          form (one caller, the bundled plugin) once the vocabulary outgrew positional
          legibility; `JanetVcsProvider` gained a two-string-arg call variant for
          commit/branch-switch/branch-create.
        - **Commands** (`Commands.cpp`, `C-c v` prefix): `vcs-status` (`C-c v s`, a
          root-scoped `*vcs status*` buffer in the `path:1:` visitable shape — a status
          entry has no line number, `:1:` makes `C-c v v` jump to the file's top for
          free), `vcs-stage-file` (`C-c v a`, "a" for add)/`vcs-unstage-file`
          (`C-c v u`) acting on the status buffer's line at point or the current file
          (`BufferView::ResolveVcsFileTarget`), `vcs-commit` (`C-c v c`, single-line
          message prompt), `vcs-switch-branch` (`C-c v w`, prompt opened from the async
          branch-list callback so Tab completes against real branch names, current
          branch excluded), `vcs-create-branch` (`C-c v n`), `vcs-branches` (M-x only,
          `lsp-show-log`'s no-dedicated-binding precedent). `*vcs status*`/`*vcs
          branches*` are find-and-refill-in-place singletons (point preserved/clamped),
          unlike the accumulate-per-invocation `*vcs blame <file>*` buffers, because
          stage/unstage/commit re-trigger the status refresh programmatically; a
          successful stage/unstage/commit/branch-switch also refreshes the diff gutter
          (staging moves changes into the index, which the worktree-vs-index diff then
          stops reporting; a commit moves HEAD).
        - **Bundled git plugin**: `status --porcelain` (rename `->` takes the new name;
          git's double-quoted special-character paths get the surrounding quotes
          stripped, inner escapes left as-is — a recorded degrade-don't-crash
          simplification), `add --`/`reset -q HEAD --` (reset, not the git-2.23+
          `restore --staged`, for portability; fails with git's own message in a
          zero-commit repo, surfaced verbatim), `commit -m`, `branch --list --no-color`
          (detached-HEAD parenthetical skipped so it can't be offered as a switch
          target), `checkout`/`checkout -b`. Covered by parse-only tests (fake `.git`
          dir, no git binary needed) plus a real-temp-repo end-to-end walk of the whole
          stage/unstage/commit/branch flow.
        - **Recorded cuts/limitations, not oversights**: ~~hunk-level staging is the
          named next slice~~ (shipped same day, see below); commit messages are
          single-line (`MinibufferPrompt` is one line by construction); a branch
          switch does ~~not reload open buffers (ned has no auto-revert/
          file-watching concept — the success message says so honestly)~~ — obsoleted
          the same day by the external-modification-safety entry under "Editor
          ergonomics": unmodified buffers now auto-revert on the next tick, and only
          *modified* ones stay unreloaded (their save hits the supersession y/n);
          `VcsRunner`'s failure detail now says "vcs <op> failed" instead of "git ..."
          since the runner never knows which VCS the provider shells out to.
        - **Hunk-level staging shipped 2026-08-20**, the slice the whole-file pair
          above deferred — and cheaper than this entry originally predicted: the
          "hunk-bodies-carrying diff parse" it guessed at turned out unnecessary,
          because the patch handed to the VCS is a **verbatim slice of the raw diff
          output** (file header block + one hunk), never parsed or reconstructed —
          `Editor/Vcs/DiffPatch.h/.cpp`'s pure `ExtractHunkPatch(diffOutput,
          targetLine)` (new-side 1-indexed line; a pure-deletion hunk matches both
          boundary lines, agreeing with the diff gutter's own notch placement), which
          also passes `\ No newline` markers and quoted header paths through
          untouched by construction. Around it: three provider callbacks
          (`:staged-diff-argv` — the index-vs-HEAD diff an *unstage* selects its hunk
          from, since a staged hunk isn't in the worktree diff — and
          `:stage-patch-argv`/`:unstage-patch-argv`, taking root + a patch file's
          path; git: `diff --cached -U0` and `apply --cached [--reverse]
          --unidiff-zero`, the latter flag required for zero-context patches);
          `VcsRunner::RequestHunkApply` chains raw-diff → extract → mkstemp temp
          patch file (`FormatOnSave`'s exact pattern — `TaskProcess` deliberately
          still has no stdin) → apply, under two distinct running-keys (one key
          across the chain would trip `RunAndCollect`'s erase-after-completion over
          the second process), temp file removed on success and failure alike;
          commands `vcs-stage-hunk` (`C-c v h`)/`vcs-unstage-hunk` (`C-c v H`),
          gated on the buffer being un-`Modified()` — the diff describes the file on
          disk while point counts buffer lines, so "save first" is the honest
          answer, not a papered-over limitation. Covered by `DiffPatchTest` (pure
          extraction), a real-temp-repo partial-stage/unstage end-to-end walk
          (`GitVcsPluginTest`: two hunks → stage one → "MM" → reverse-unstage →
          " M"), runner guard tests, and the BufferView gate tests. **Recorded
          caveat**: an unstage matches point's line against the *cached* diff's line
          numbers, which can drift from buffer lines when un-staged edits exist
          *earlier* in the same file — exact in the common stage-then-undo flow,
          approximate past it; revisit only if it bites in practice.
        - Chasing a test failure here also fixed a real latent build trap: CMake's
          `ned_embed_janet_plugin`/`ned_embed_treesitter_query` read their source files
          at configure time with no dependency recorded, so editing `vcs-git.janet`
          silently kept the stale embedded copy until an unrelated reconfigure — both
          functions now append their source file to `CMAKE_CONFIGURE_DEPENDS`.
        Still out of scope: a full historical multibuffer beyond one synthesized buffer
        per query result (the fuller "Multibuffers" wishlist entry below is still its
        own, larger, unscoped thing). The "generalizable past version control" framing
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
  - [x] Emacs keymap coverage + the C-SPC input fix — **shipped 2026-08-20**. The
        reported symptom ("C-SPC doesn't start/end/cancel a block") was never a keymap
        gap — `C-SPC` → `set-mark-command` had been bound all along — but an input-layer
        drop: a legacy terminal (no kitty keyboard protocol, no modifyOtherKeys — e.g. a
        default tmux) sends Ctrl+Space as the single NUL byte, Notcurses' own
        `load_ncinput` normalizes C0 bytes 1–26 to Ctrl+letter but leaves 0 untouched,
        and an id-0 `ncinput` is *unrecoverable* by any caller: `notcurses_get`'s return
        value is the id, with 0 already meaning "no input", so `EventLoop`'s drain loop
        cannot distinguish the keypress from an empty queue even in principle. Fixed
        inside Notcurses itself via a FetchContent `PATCH_COMMAND`
        (`CMake/PatchNotcursesNulKey.cmake`, idempotent, fails the configure loudly if an
        upstream bump drifts the anchor): NUL → id `' '` + `NCKEY_MOD_CTRL`, exactly the
        shape kitty/modifyOtherKeys terminals already report, which `KeyTranslation.cpp`'s
        existing Ctrl branch already translates — so no ned-layer special case exists at
        all. Rode along: `set-mark-command` gained Emacs' C-SPC C-SPC deactivate-in-place
        idiom plus "Mark set"/"Mark deactivated" echo messages; undo is now bound to
        *both* `C-_` and `C-/` (one per keyboard protocol — a kitty terminal reports
        Ctrl+/ as a genuine `C-/` chord, a legacy one sends 0x1F → `C-_`; binding either
        alone leaves undo dead on the other protocol's terminals, a live-tested
        regression each way) and `C-x u`. New commands, all on their real Emacs defaults
        with the usual `ESC` twins: `yank-pop` (`M-y`, backed by new `last-command`
        tracking in `Dispatcher::Feed`/`CommandContext::lastCommand` — the M-x path
        deliberately doesn't update it, a recorded cut), `kill-word` (`M-d`),
        `backward-kill-word` (`M-DEL`), `mark-whole-buffer` (`C-x h`), `transpose-chars`
        (`C-t`), `transpose-words` (`M-t`), `upcase-`/`downcase-`/`capitalize-word`
        (`M-u`/`M-l`/`M-c`, ASCII-only per the word-classification's documented cut),
        `open-line` (`C-o`), `delete-blank-lines` (`C-x C-o`), `just-one-space`
        (`M-SPC`), `delete-indentation` (`M-^`), `back-to-indentation` (`M-m`),
        `save-some-buffers` (`C-x s` — saves all without per-buffer y/n, a recorded
        deviation), `recenter` (`C-l`, one-shot `InteractiveRequest`), and `goto-line`
        (`M-g g`/`M-g M-g`, prompt-shaped, 1-based, clamping out-of-range). Multi-edit
        commands batch through `BeginUndoGroup` so each undoes as one step. Not done,
        deliberately: prefix arguments (`C-u`), `zap-to-char`, sentence/sexp motion,
        kill-append on consecutive kills.
  - [x] External-modification safety — **shipped 2026-08-20**, after live use surfaced
        the gap the hard way (a concurrent editor writing `Commands.cpp` underneath an
        open ned buffer, saved over silently). `Buffer` now records the file's
        `last_write_time` at load/save/revert (`DiskTimestamp_`; stat-*before*-read in
        `FromFile` so a write racing the read is flagged rather than absorbed;
        stat-after-read on the async `FinishLoad` path, an accepted small race);
        `ExternallyModified()` is the derived query (false for pathless/missing —
        deletion isn't supersession — true for a file appearing under a `NewFile`
        buffer). `save-buffer` refuses to silently overwrite a superseded file: it
        raises `InteractiveRequest::ConfirmOverwriteSave`, BufferView runs the y/n
        (mirroring ConfirmCloseBuffer's shape), and y invokes `save-buffer-force` — the
        same save body minus the gate, M-x-reachable as the deliberate escape hatch.
        The other half is default-on auto-revert (`Editor/AutoRevert.h/.cpp`, toggled
        via `ned/set-auto-revert`): on the existing scratch-auto-save tick,
        `AutoRevertBuffers` reloads every open, *unmodified*, file-backed buffer whose
        file changed on disk (`Buffer::Revert()` — one undoable step, point clamped,
        mark/secondaries/narrowing/folds cleared), reporting the reverted names on the
        status line so it's never silent; a buffer with local edits is never touched —
        the save-time confirmation owns that conflict. **Deferred, not designed in**:
        three-way merging when both sides changed (the `SavedSnapshot_` base rope makes
        a diff3 feasible later), and Emacs' ask-on-first-edit supersession prompt.
  - [x] Multiple cursors / multi-cursor editing — **shipped 2026-08-20** as its own
        phase, exactly per the design pass below (kept as the original scoping record;
        every "genuinely missing" item it names landed as predicted, the relocation
        reuse included). What shipped, layer by layer:
        - **`Buffer` core**: `Cursor{point, mark, goalColumn}` secondaries beside the
          untouched primary `Point_`/`Mark_`, relocated at all five content-mutation
          sites through the existing `RelocateForInsert`/`RelocateForDelete` primitive
          (the reuse this entry predicted); `AddCursorAt` (grapheme-snapped, deduped),
          `ForEachCursor` (swap-in-place per secondary — including per-cursor
          goal-column swapping, so multi-cursor vertical motion doesn't leak the first
          cursor's column into the rest — exception-safe via scope guards since
          BufferView deliberately survives throwing commands), and undo grouping
          (`BeginUndoGroup`/`EndUndoGroup`, nestable; mutators route their old inline
          record/amend logic through a shared `RecordOrAmendUndo`). Cursors collapsed
          onto one position by a delete merge immediately — deferred to the end of a
          `ForEachCursor` batch, where mid-loop normalization would invalidate the
          swap slots (a real bug caught by the first test run, not hypothesized).
          `Undo()`/`Redo()` clear secondaries — the explicit v1 answer to "what does
          undo restore": collapsing is predictable, N-cursor restoration isn't.
        - **Commands**: an explicit `PerCursor(fn)` adapter wraps the basic
          motion/editing set (self-insert, newline, delete both directions, char/word/
          line motion, next/previous-line) — applied per command, deliberately not
          globally: kill-ring commands, rectangles, registers, narrowing, and
          toggle-line-comment stay primary-only in v1, which is this entry's
          "explicit decision" question answered as "unsupported while multiple cursors
          are active" for now. New commands: `add-cursor-below`/`add-cursor-above`
          (`C-DOWN`/`C-UP` — free, terminal-reliable, unlike Ctrl+Alt+Arrow or the
          `C-d`-conflicting VS Code chord), `select-next-occurrence` (`M-n`/`ESC n` —
          first press selects the word at point VS-Code-style, later presses add a
          selecting cursor at the next occurrence, wrapping once, skipping owned
          matches), `select-all-occurrences` (M-x only), and `keyboard-quit` (`C-g`)
          doubling as collapse-to-one, Emacs multiple-cursors' own convention.
        - **Rendering**: secondary carets as inverted cells (ScrollBar's thumb
          technique — theme-independent, distinct from the primary's real terminal
          cursor), including the caret-at-line-end case that has no codepoint cell of
          its own; secondary selections ride the existing `InSelection` overlay.
        - Verified end to end against a live pty (C-DOWN → type → save produced the
          two-line simultaneous edit) plus `[MultiCursor]` unit tests across all three
          layers. **Recorded v1 cuts**: mouse-driven cursor creation (Alt+Click stays
          "don't want," below); the view doesn't scroll to a newly added occurrence
          cursor; kill/register/rectangle commands act on the primary only; per-cursor
          kill-ring semantics unexplored.
        Original scoping record follows. ~~Explicitly deferred, not started~~, after
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
  - [x] **Task runner** (build/test tasks from within the editor) — done: see
        `Source/Editor/Tasks/` (`TaskRunner`/`TaskProcess`/`TaskConfig`), wired into
        `main.cpp`/`Commands.cpp` and covered by `Tests/TaskRunnerTest.cpp`. The entry
        below is kept as the original scoping record. Was sequenced ahead of
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
  - [ ] Session persistence, planned 2026-08-20 as three slices after the user asked
        for per-file persistence *plus* contextual project-session restore ("if I just
        run ned from a project folder, it could open my last session in said folder"),
        with the trust model below shaped by their own explicit security asks.
        **Slice 1 (per-file save-place) shipped 2026-08-20**: `Editor/Session.h/.cpp` —
        `FilePlaceStore` (pure, unit-tested core: JSON round-trip via nlohmann, LRU cap
        of 1000, `weakly_canonical` path keys mirroring `DapManager::NormalizePathKey`)
        plus process-wide accessors in the established mutex-guarded-static pattern,
        persisting each file's last point (as line + visual column, never a byte offset
        — robust to outside edits, clamped on restore) and viewport top line to
        `$XDG_STATE_HOME/ned/file-places.json` (the first `$XDG_STATE_HOME` use in the
        codebase). Restore-on-open rides `BufferList::SetOnFileOpened` (a new central
        hook every open path already funnels through); top-line restore lands at
        `BufferView::EnsureTopLineValidForActiveBuffer`'s once-per-buffer-switch seam
        (which also covers a pane's very first Paint, i.e. the startup buffer), clamped
        by `MaxTopLine()` with `ScrollToShowPoint()` still guaranteeing point visible —
        as a bonus this gives *in-session* per-buffer scroll memory, which
        `BufferView::topLine_` alone never had (the "move it per-`Buffer` first"
        prerequisite this entry originally guessed at turned out unnecessary).
        Recording runs on the existing scratch-auto-save tick (`WindowManager::
        RecordSessionPlaces`: all open buffers, then visible panes again with their
        real top lines) plus one forced save after `EventLoop::Run` returns; the
        periodic save skips the disk write when nothing changed (`Dirty()` tracks
        place changes, deliberately not lastUsed bumps). `ned/set-save-place` (default
        on) disables both directions; startup buffers' restore deliberately waits
        until after `init.janet` loads so that toggle is honored. Known, documented
        slice-1 gaps: an `IsLoading()` async placeholder (>16MiB file) never restores
        (point would clamp to 0 in empty placeholder content); a buffer closed between
        5s ticks loses at most that tick's place update; recording is deliberately
        *not* wired to any buffer-switch seam (the old-buffer pointer there can
        already dangle mid-close — see `WindowManager::RecordSessionPlaces`'s comment).
        Post-ship fix (same day, user-reported): the seam-based top-line restore never
        fired for the buffer a pane *starts* on — `BufferView`'s constructor pre-seeds
        `topLineValidatedBuffer_` (so pre-first-Paint scroll events aren't discarded),
        which silently excludes the startup/relaunch case, the single most common way
        a place gets restored. Fixed by also applying the stored topLine in the
        constructor itself, clamped by `min(storedTopLine, pointLine)` (`MaxTopLine()`
        is meaningless at construction — no size yet; the min only bites when the file
        shrank outside ned). Covered by a `[BufferView][Session]` test exercising both
        the constructor path and the switch path.
        **Slice 2 (per-project sessions) shipped 2026-08-20**: `Editor/
        ProjectSession.h/.cpp` — `ProjectSessionData` (open-file set, active file,
        sidebar visibility/width, DAP breakpoints in `DapManager`'s own store shape)
        with the same pure-core-plus-mutex-guarded-static layering `Session.h`
        established, stored in `<root>/.ned/session.json` when a `.ned/` directory
        exists (strictly opt-in, never auto-created; a future `ned-init-project`
        command creates it) else `$XDG_STATE_HOME/ned/sessions/<fnv1a64-of-root>.json`.
        The no-arg launch's root rule changed as planned: walk up from cwd for a
        VCS/`.ned` marker (`FindProjectMarkerRoot`) instead of "cwd is the root
        outright"; an explicitly opened directory keeps its old rule. Only a root
        actually carrying a marker (`HasProjectMarker`) ever gets
        `SetActiveProjectSessionRoot` — a bare non-project cwd like `$HOME` reads and
        writes *no* session, by design. Restore is **Kate-style, per the user's
        explicit mid-implementation call** (reversing the plan's original "CLI paths
        suppress the buffer-list restore"): session buffers restore on *every* launch
        in a project, a named CLI file just wins focus with the session's buffers
        filling in behind (deduped via `FindByPath`, missing files silently skipped,
        each restored buffer getting its save-place restore through the same
        `SetOnFileOpened` hook); the planned `--restore` flag was dropped as
        unnecessary. `--no-restore` makes the run session-inert in BOTH directions —
        it must also suppress saving, or `ned --no-restore quickfix.cpp` would clobber
        the real saved session at quit. `ned/set-session-restore` (default on) is the
        persistent toggle, honored because the restore runs after `init.janet` loads
        while the root is established before it (every load/save re-checks the toggle
        at use time). Breakpoints ride new `DapManager::AllBreakpoints`/
        `RestoreBreakpoints` (normalizing sorted/deduped/non-empty invariants on the
        way in, pushing to a live adapter as a robustness guard) — closes the
        "persisting breakpoints across restarts" DAP v1 cut. Capture is
        `WindowManager::SaveProjectSessionNow` (skips the transient `PreviewBuffer()`
        and scratch-pad buffers — global, not project state) on the same 5s tick as
        `RecordSessionPlaces` plus once after `EventLoop::Run` returns, with an
        unchanged-JSON memo skipping redundant disk writes. Scratch fallback in
        `main.cpp` moved below the restore, so a restored session doesn't leave a
        stray empty scratch tab. Window-split layout remains an explicit slice-2 cut.
        **Slice 3 (trusted project-local `.ned/init.janet`) shipped 2026-08-20**:
        `Editor/ProjectTrust.h/.cpp` — `ProjectTrustStore` (entries keyed by the init
        file's normalized path, carrying its FNV-1a-64 content hash + trustedAt/
        lastUsed) with the same pure-core + mutex-guarded-static layering as its two
        sibling stores, persisted in `$XDG_STATE_HOME/ned/trusted.json`. Loading is
        never silent — this is arbitrary code execution triggered by opening a
        directory, the same concern class recorded against Org Babel: an
        exact-content-match, unexpired trust entry loads the file right after the
        global `init.janet` (project overrides user, early enough for its mode
        overrides/grammars to affect the initial buffer); anything else defers to a
        y(once)/a(always)/n prompt through the focused pane once the UI exists —
        `WindowManager::RequestTrustProjectInit` → `BufferView`'s
        `ConfirmTrustProjectInit` InputMode, the deferred-binary-open pattern exactly,
        with a one-shot decision callback (main.cpp owns the `janet::Environment`, so
        the widget only reports the choice; the trusted hash is recomputed at decision
        time so what's recorded is exactly what got loaded). The accepted cost of
        prompting at all, noted at the call site: a first-time project init's mode
        overrides can't affect the already-selected initial Mode until the next buffer
        switch. Trust expires by *disuse*, the user's own security ask: `lastUsed`
        refreshed on every successful load, entries unaccessed past the window
        (default 30 days, `ned/set-project-trust-expiry-days`, <= 0 = never) pruned at
        store load along with entries whose file no longer exists; a changed file
        always re-prompts regardless of age (the stale entry stays, so an "always"
        re-approval just overwrites it). `ned-init-project` (M-x, no default binding)
        creates the `.ned/` directory and activates session persistence for the
        current run when the root wasn't yet a project. Verified end-to-end against a
        real pty (prompt → "a" → trusted.json entry → silent reload → edit → re-prompt),
        driven by a query-answering harness after plain `script` proved unable to get
        notcurses through its init handshake. Recorded cuts: a `.ned/plugins/*.janet`
        autoload dir, and `ned-init-project` offering a `.gitignore` append.
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
  - [x] Minimap — done: braille-glyph minimap widget replacing the scroll bar
        (`Source/UI/Minimap.h/.cpp`, commit `0e42708`).
  - [x] Tab UX follow-up — **shipped 2026-08-20**, three user reports/asks in one slice:
        - *Active-tab auto-reveal.* Opening/switching to a buffer whose tab sat past the
          overflowing row's right edge left it invisible ("very confusing" — the file
          looked like it hadn't opened). `TabBar::Paint` now scrolls just far enough to
          fully reveal the active tab, but only when the active buffer *changed* since
          the last paint (`lastRevealedActive_`, identity-compared, never dereferenced —
          the `topLineValidatedBuffer_` convention), so manual wheel-browsing of the
          overflow is never snapped back. `scrollOffset_` is also now clamped every
          frame, not just on wheel — closing tabs could strand it past the shrunken row.
        - *MRU close.* Closing the active tab used to land on the *first* tab; now it
          lands on the tab most recently left. `BufferList` owns the MRU order
          (`TouchBuffer`/`MostRecentlyUsedBuffer`, purged in `Close` so never dangling);
          `ActiveBuffer` grew an on-change hook, wired per Pane to `TouchBuffer` — the
          one choke point every switch path (tab click, find-file, switch-to-buffer,
          sidebar preview, close reassignment) already funnels through.
          `CloseBufferNow` asks MRU first, falling back to list order for
          never-activated buffers (e.g. session-restored, never visited), then the
          fresh-scratch conjuring as before.
        - *Tab cycling.* `tab-next`/`tab-previous` (`C-c .`/`C-c ,` — the unshifted
          keys under the move pair's `<`/`>`, so tap-to-walk vs. shift-to-drag lives
          on one physical key pair; `C-x RIGHT`/`C-x LEFT` ride along on Emacs' own
          next-buffer/previous-buffer spots) switch tabs in tab-bar order, wrapping at
          both ends. One-shot `InteractiveRequest`s (`TabNext`/`TabPrevious`) since the
          active-buffer pointer lives in BufferView, not the command layer — and the
          switch goes through `ActiveBuffer::Set`, so cycling feeds the MRU order like
          every other switch path.
        - *Tab reordering.* `BufferList::MoveBufferToIndex` is the one mutation;
          `tab-move-left`/`tab-move-right` (`C-c <`/`C-c >`) are the keyboard face and
          drag-a-tab (live VS-Code-style reorder while held, `TabBar::SetOnReorder` —
          same handler indirection as `SetOnCloseRequest`) the mouse face. Since
          `Buffers()` order is what `SaveProjectSessionNow` persists and session restore
          replays in order (verified end-to-end via a live tmux run before shipping:
          reorder → quit → relaunch came back in the dragged order, active file
          focused), a reorder survives a restart with zero new session state.
        Known cut: the session's `openFiles` order can't preserve a CLI-named file's
        old slot — `ned somefile` opens it first, so its tab leads and the session
        fills in behind (the documented Kate-style "named file wins focus" behavior).
  - [x] ANSI fallback themes — **shipped 2026-08-20**, user report: on a framebuffer
        console (`TERM=linux`, 8 colors, no truecolor/256) the TrueColor-heavy themes
        quantized down to washed-out or black-on-black. `AnsiDarkTheme()`/
        `AnsiLightTheme()` (`UI/Theme.cpp`) are curated Palette16-only counterparts,
        deliberately restricted to indices 0-7 plus `Color::Default` (an 8-color
        terminal's terminfo may or may not map the Bright 8-15 range to bold+base;
        brightness rides `Brush::bold` where one exists instead). `main.cpp` swaps the
        theme local in place via `AnsiFallbackFor(theme)` (pure, picks the variant by
        background luminance — also correctly overrides a `--detect-theme` file, which
        is just as TrueColor as the built-ins) right after `EventLoop` construction,
        gated on the new `EventLoop::CanTrueColor()`/`PaletteSize()` (notcurses
        capability queries — they need the live context, which is why the check can't
        run before the widgets are built; safe because every widget holds `const
        Theme&`/`const Brush&` into that same local and repaints fresh per frame).
        `Color::Interpolate` gained an equal-endpoints short-circuit so the ANSI
        themes' flattened (start == end) mode-line gradients stay real palette colors
        instead of degrading to the RGB approximation table. Verified end-to-end on a
        query-silent raw pty with TERM=linux (tmux is *not* a valid simulator here —
        it answers notcurses' startup interrogation and advertises truecolor
        regardless of TERM): a full frame rendered with only basic SGR 30-49 codes,
        zero `38;2`/`38;5`. Remaining `Interpolate` accents (echo-area dim, blame
        ages) still produce TrueColor and quantize on such terminals — accepted,
        they're per-cell accents, not the theme-wide wash-out. Follow-up hook: the
        ANSI pair could later be user-selectable/theme-file-expressible on capable
        terminals too (the serialization already round-trips `x:<n>` palette tokens).
  - [ ] Rich built-in theme set — **planned 2026-08-20** (user ask), five phases, each
        landing independently with tests:
        - [x] *Phase 0 — `ThemePalette` + derivation* (**shipped 2026-08-20**).
          `UI/ThemePalette.h/.cpp`: an ~18-slot semantic palette (background/foreground/
          subtle, the eight accent hues every published theme spec already provides in
          some form, and UI-chrome slots) plus `ThemeFromPalette(name, palette)` — the
          one place palette slots map to all ~70 `Theme` fields, so every derived theme
          assigns roles identically and only the hues differ. Rationale: hand-authoring
          70 fields × ~20 themes would drift and never finish; cloned themes become
          transcriptions of their official palettes. Derived shades (dim tab-bar text,
          disabled scroll bar, execution-line wash, the focused-gradient 60% accent
          pull DarkTheme documents) go through `Color::Interpolate`, not per-theme
          literals. Tests include an automated contrast floor (relative-luminance
          delta between `background` and every serialized `*_foreground` field) — the
          check that makes the high-contrast set mean something and catches the
          black-on-black class of bug for every future theme. The hand-built
          Dark/Light/ANSI themes stay untouched.
        - [x] *Phase 1 — registry + selection plumbing* (**shipped 2026-08-20**).
          `UI/ThemeRegistry.h/.cpp` (`ThemeByName`/`ThemeNames`, the
          `BundledModeFactories` pattern — deliberately a fixed compile-time table,
          no runtime registration until Janet-defined themes are a real ask);
          `ned/set-theme` Janet binding storing a *name only* in
          `Editor/ThemeSetting.h/.cpp` (mutex-guarded static, `TabWidth.h` pattern —
          resolution against the registry stays in main.cpp, keeping `Editor/` free
          of UI dependencies), read at main.cpp's selection point — precedence:
          init.janet `set-theme` > `--detect-theme` file > `DarkTheme`, with an
          unresolvable name reported via the status line and falling through. Plus
          the theme picker (user ask): `select-theme` (M-x only, no chord) rides the
          `HandleProjectFindFileKey` fuzzy-session shape with **live preview** — the
          highlighted theme is applied in place on every selection/rank change (the
          exact swap mechanism the ANSI fallback proved safe, routed through a
          main.cpp-wired applier callback — `WindowManager::SetThemeApplier`
          forwarded per-pane — which also keeps the limited-terminal
          `AnsiFallbackFor` gate in the loop for live switches), Enter applies+
          commits, Escape/C-g restores a full `Theme` snapshot taken at session
          start (a copy, not a name, so a `--detect-theme` file survives a
          cancelled browse). The session opens highlighting the *current* theme's
          name, so opening the picker previews no change until the user moves.
          Interactive choice deliberately not persisted — init.janet is the config
          surface, Emacs' own `load-theme` convention. Verified live in tmux:
          preview/revert/commit/startup-name/unknown-name all exercised. (The
          "tmux `send-keys M-x` gets swallowed" quirk observed during this
          verification turned out to be a real bug, not a tmux artifact — a
          user-reported "M-x doesn't trigger," root-caused and fixed the next
          session: Notcurses v3.0.14 records a legacy-terminal fast ESC-prefixed
          Alt+letter only in ncinput's *deprecated* `alt` bool, never syncing it
          into `modifiers`, so `ncinput_alt_p()` — all KeyTranslation checked —
          was false for the exact shape M-x actually arrives as outside the kitty
          keyboard protocol. Confirmed with a standalone notcurses keyprobe;
          fixed caller-side in `TranslateKey` by honoring both fields, matching
          the NUL-patch decision logic in reverse: patch Notcurses only when no
          caller-side fix exists, and here one did. `TestEvents::LegacyAlt` now
          constructs that exact shape so the regression is covered headlessly.)
        - [x] *Phase 2 — the original eight* (**shipped 2026-08-20**). major-dark/
          -light (vivid saturated), minor-dark/-light (muted/pastel),
          high-contrast-dark/-light (pure black/white backgrounds, held to a raised
          contrast floor — 90 luma delta vs. the standard 40 — by
          `BundledThemesTest`, making "high contrast" a tested property rather than
          a name), mono-dark/-light (grayscale). All eight are `ThemePalette`
          literals inside `ThemeRegistry.cpp`'s own anonymous namespace, reachable
          by name only — no exported factories, tests go through `ThemeByName` like
          every real consumer. One deviation from the plan above: monochrome needed
          *no* derivation variant — filling the eight accent hue slots with
          grayscale luminance shades sends a single-ramp palette through the
          standard `ThemeFromPalette` mapping unchanged (BrushFor's bold/italic
          traits carry what hue can't), and a grayscale-invariant test (every
          serialized color r==g==b, Default pass-throughs excepted) keeps it
          honestly monochrome. Shared test helpers factored into
          `Tests/ThemeTestSupport.h` for Phase 3's clones to reuse. Verified live
          in tmux: all eight committed via the select-theme picker, each frame
          painting exactly its palette's values.
        - [ ] *Phase 3 — clones (~14).* Solarized dark/light, Gruvbox dark/light,
          Nord, Dracula, Monokai, One Dark/One Light, Catppuccin Mocha/Latte, Tokyo
          Night night/day (stretch: Rosé Pine, Everforest, Zenburn, remaining
          Catppuccin flavors). All MIT-licensed palettes; keep the real names
          (universal editor practice) with an attribution comment + upstream URL per
          palette. Per clone: transcribe official hex → `ThemePalette`, then a
          scripted tmux `capture-pane -e` sweep for eyeball review against reference
          screenshots.
        - [ ] *Phase 4 — polish.* The sweep script kept in-repo, docs, and the
          `--detect-theme` precedence note.
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
