# Ned Roadmap

What's still open. Completed work is deliberately not tracked here — the detailed
per-feature design/decision records this file used to carry were pruned 2026-08-20,
2026-08-25, 2026-09-06, and again 2026-09-07; full history lives in git
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

### Release 0.6 — "a stranger can install it, learn it, and get equal language treatment"

Version is now real and reported: `project(Ned VERSION 0.5.0)` flows through a generated
`NedVersion.h` into `ned --version`, and `v0.5.0` is tagged. Before that the CMake version
was metadata nothing consumed, which is how it came to read 0.5.0 while the only tag read
v0.1.0 and the binary could report neither.

0.5 is usable *by its author*. The content of 0.6 is being usable by someone who is not —
which is one coherent claim rather than a pile of features, and is what the items below
are scoped against. Deliberately **not** in 0.6: the parsing engine and Theme v2. Both are
1.0-scale and reshape foundations (`Mode`, the whole theme surface); folding either in
would make the number mean nothing.

- [ ] **Split the docs.** `Docs/` is entirely developer-facing today — design records
      (`ParsingEngine.md`, `Translucency.md`, `BufferViewDecomposition.md`), capability
      audits, and key references. There is no user-side documentation at all, and the
      README is 70 lines against **275 registered commands** and **160 `ned/*` Janet
      bindings**. Split developer docs from user docs as separate trees with separate
      audiences, rather than continuing to let one directory serve both.
- [ ] **Generate the command/binding reference rather than writing it.** The highest-
      leverage user-doc item, because it cannot drift: a `Tools/` binary linking `ned_lib`
      walks `CommandRegistry` and the `ned/*` binding table and dumps the doc strings
      *already being passed to* `Register<Fn>` into Markdown. Spec is in "Documentation &
      Companion Tooling" below; this promotes it to a 0.6 blocker. An existing, invisible
      asset becomes the reference page.
- [ ] **Ship an install story.** `install(TARGETS ned ...)` exists, but CI produces no
      artifacts and there is no package, so a stranger must build 24 tree-sitter grammars,
      FetchContent nine dependencies, and already have a pkg-config-discoverable Janet. The
      `v0.5.0` tag is the trigger a release workflow hangs off. *In flight:* a Gentoo ebuild
      (author-side), and the Notcurses patches are now extractable — see below.
- [ ] **Notcurses as a system library.** `Patches/notcurses/` now carries the three fixes
      as real `git am`/`patch -p1` files generated from the `CMake/PatchNotcurses*.cmake`
      scripts that remain the source of truth, plus `regenerate.sh` (byte-stable output,
      dated from each script's own last commit) so they cannot silently go stale on a
      Notcurses bump. Verified against pristine v3.0.17: all three apply in order and the
      result is byte-identical to the tree the CMake scripts produce. What remains is the
      build-side option — letting `CMakeLists.txt` consume a system Notcurses that already
      carries these, instead of always FetchContent-ing and patching its own copy. Note the
      existing private-libdir install rule exists precisely because ned is built against a
      specific patched Notcurses a stock system package does not carry; a system-library
      path has to make that requirement explicit rather than silently falling back.
- [ ] **Tier A language parity (D2).** `c`/`cpp`/`python`/`javascript`/`typescript`/`tsx`/
      `php` have LSP root markers, formatter and test-runner config;
      `java`/`kotlin`/`csharp`/`go`/`rust`/`bash` have the grammars and none of it. Someone
      arriving with a Go project gets a visibly worse editor than someone arriving with C++,
      for no reason except nobody wrote the table. Pure config — `Lsp/ServerConfig.h`,
      `Dap/Config.h` and `TestRun/Config.h` already take this as data. See
      `Docs/LanguageCoverage.md` for the tier definitions.
- [ ] **Add Lua and CMake grammars.** Both are Tier A by usage and both are absent; CMake is
      ned's own build system and Lua is the configuration language of half the tooling
      world. Sourcing: `tree-sitter-grammars/tree-sitter-lua` (2026-06-19),
      `uyha/tree-sitter-cmake` (2026-07-08).
- [ ] **Add diff/unified-diff.** Cheapest high-value grammar in the catalogue: ned owns a
      VCS side panel, hunk-level staging (`Vcs/DiffPatch.h`) and a merge-conflict mode, all
      of which currently read diff output as untyped text.
      `tree-sitter-grammars/tree-sitter-diff` is live (2026-08-14).
- [ ] **Add the tree-sitter query language (`.scm`).** ned authors 79 of them and edits them
      with no highlighting at all.
- [ ] **Stability gate: don't ship a minor bump with a known-red preset.** `sanitize -j8`
      has one reproducible `[Performance]` failure. The fix named in the watch list below is
      to gate the budgets on the build being optimised (`NDEBUG`) rather than loosen them,
      since loosening gives up the regression signal the tests exist for. Close it, or
      downgrade it to explicitly-deferred with that reasoning recorded.

**1.0, for context, since branching starts near it.** For a scriptable editor 1.0 is a
promise about the *Janet surface*, not about features: 160 `Register<>` bindings and 275
command names, and declaring 1.0 makes breaking any of them a major-version event. That
surface is still moving (`TreeSitterQuerySources` became designated-initializers mid-flight;
the parsing engine would reshape `Mode` outright), which is the real reason 1.0 is not close
regardless of how usable ned feels. Candidate criteria, all already present below: Janet API
frozen · parsing-engine decision made either way · no known data-loss paths · release
artifacts · the mdBook docs site.

### Rendering & Keystroke Performance

What is left of the translucency/theme-v2 work is performance, not appearance: the
compositor and theme engine shipped, and these are the costs that surfaced while building
them. Compositing design and the measurements behind it: `Docs/Translucency.md`,
`Tools/NotcursesGradientProbe.cpp`, `Tools/TerminalImageAlphaProbe.cpp`.
`Tests/KeystrokeBench.cpp` is the instrument for all three — it exists because every CPU
measurement said "fine" while typing felt bad.

- [ ] **Highlighting is still parse-bound for large non-markdown files.** The windowing
      above bounds the *query*; the incremental re-parse underneath it is untouched, and
      for C++ that is what dominates -- 2000 lines still measures ~32ms per keystroke while
      markdown of the same size dropped to ~25ms. `IncrementalParseCache` reconstructs each
      edit by diffing its own last text, so every keystroke also walks the whole document
      twice before tree-sitter starts. Worth attacking the same way: give it the edit
      directly instead of making it rediscover one.
- [ ] **`mode.symbolKind` is O(document) per keystroke, and cannot simply be windowed.**
      Keyed on `ContentGeneration` like highlighting was. Re-measured after the windowing
      landed: ~5.3ms *marginal* (highlight alone 64.7ms whole-document, highlight then
      symbolKind 70.0ms), so roughly a fifth of the current ~25ms ROADMAP.md frame and the
      largest single line item left above the parse. Two different shapes behind one
      `std::function`: `TreeSitterModeFromLanguage`'s closure runs `Query::Matches` over the
      whole tree, while `MarkdownMode`'s walks the whole block tree in
      `CollectMarkdownSectionMarkers` -- the latter genuinely shares `sharedParse` as its
      comment claims, so its cost is the *walk*, not a second parse.

      The `HighlightWindow` trick does **not** transfer, and that is the interesting part.
      Highlighting only ever needed what is on screen; `symbolKind` has a second consumer,
      `Editor/StickyScroll.h`, which needs the *enclosing* definitions -- a class opened at
      line 1 while the viewport sits at line 900. A viewport window silently empties the
      sticky row. (Huge buffers already accept exactly that degradation via
      `structuralWindow_`; ordinary ones should not.)

      So split the two consumers rather than windowing the one call:
      - the **gutter** wants markers intersecting the viewport -- `ts_query_cursor_set_byte_range`,
        the same bound `Query::CapturesInRange` already added for highlighting;
      - **sticky scroll** wants an ancestor chain at one offset, which is a walk *up* from
        `Node::NamedDescendantForByteRange(viewportTopByte, ...)` via `Node::Parent()` --
        O(tree depth), not O(document), and it never needed the full marker list to begin
        with.

      `Tests/KeystrokeBench.cpp`'s `highlight then symbolKind` line is the measurement to
      watch; it exists to separate the query/walk from the parse it shares.

- [ ] **Dirty-region flush, and the animation question behind it.** `Screen::Flush` writes
      *every* cell of both planes every frame -- 14,400 `ncplane_putstr_yx` calls at 160x45
      -- and Notcurses then diffs that to decide what to emit. Fine for an editor that
      repaints when something happens, which is what ned was until the recency glow asked
      it to repaint on a clock: an animation frame is a full-screen redraw, ~36 a second,
      on the thread that also handles input. Reported live as an editor that felt slow to
      type in -- three times, across three different fixes, which is the point. Each was real: a
      per-cell mutex and clock read; a thread spawned *and joined* per animation tick, then
      per *keystroke* once start/stop straddled the 200ms effect; a background wash
      repainting a whole row per frame. None of them was the last one.
      What makes that hard to catch: the editor's own CPU stays *low* (forty keystrokes at
      160x45 measures two ticks). Every CPU measurement said "fine" while typing felt bad,
      which sent the investigation through three wrong culprits before
      `Tests/KeystrokeBench.cpp` timed the keystroke path itself and found the 45ms
      highlight above -- a cost the glow never contributed to and could not have.
      The lesson is about instruments, not about animation: measure the latency of the
      thing being complained about, not the CPU of the process containing it.
      Writing only cells that changed since the last frame would fix it at the source, let
      the glow default back on, and speed up every ordinary repaint too. Two known traps:
      the backing plane is cleared wholesale each frame (`ClearBacking`), so it needs the
      same treatment or it defeats the point; and a `Screen` is reconstructed on resize, so
      the first frame after one must write everything.
### Language Intelligence

- [ ] **Android device tooling** (the one part of the Java/Kotlin work below that
      didn't fall out of it). Editing, building and testing an Android project works
      today via Java/Kotlin modes + the generic task runner (`ned/set-task-command`
      pointed at `./gradlew ...`) + the bundled XML mode for layout files. What has no
      natural home in anything that exists: `adb logcat` streaming, and a one-click
      "install + run on device/emulator" flow. Deliberately left unscoped — worth
      building only if plain shelled-out `adb`/`gradlew` tasks prove too manual in
      practice, not speculatively.

Shipped here, one slug each for `git log --grep=`: `go-bundled-language`,
`csharp-bundled-language`, `java-kotlin-bundled-language` (all via `FetchContent` like
every bundled grammar — a system-installed `.so` never carries its own `queries/*.scm`,
which is why leaning on one is never the shortcut it looks like; Kotlin is the one
language whose grammar choice needed recording, see `CMakeLists.txt` beside
`ned_add_treesitter_grammar(tree-sitter-kotlin ...)`. Java/Kotlin LSP, test running and
debugging all fell out of existing machinery: jdtls and `kotlin-language-server` are
configured the ordinary `ned/set-lsp-command` way with new Maven/Gradle entries in
`RootResolver.cpp`, `TestOutputParser`'s existing `junit-xml` parser already reads
Surefire/Gradle reports, and `java-debug` needs no new `DapManager` work),
`resolver-gaps` and `lsp-document-link`
(go-to-file-at-point, LSP-first via `textDocument/documentLink`),
`protocol-stall-timeout-split`, `lsp-multiroot` + `lsp-multiroot-cache-scoping` +
`lsp-workspace-folders`, `listpopup-scroll`.

- [ ] Whether Markdown fenced code blocks / Org `#+BEGIN_SRC` blocks should get the same
      real-LSP-sync treatment HTML `<script>`/`<style>` embedded documents already have
      is an open question — spawning a live language server per code fence in an
      ordinary notes file could be noisy for illustrative/incomplete snippets.
- [ ] `Lsp/Manager.cpp`'s `PathToUri` doesn't percent-encode, while its `UriToPath` now
      decodes (`lsp-document-link`, after clangd's own encoded targets proved every
      URI-carrying response was missing paths outside the unreserved set). Nothing has
      needed the outgoing direction yet — it would change the URI every `didOpen` sends,
      so it was left alone deliberately rather than overlooked. Revisit if a project path
      with a space/`#`/`?` in it ever misbehaves.

**LSP completion fidelity** (scoped 2026-09-07 from a full survey of the path).
Shipped since, one slug each for `git log --grep=`: `completion-fidelity` (honor
`textEdit` — per-item replace ranges, both the TextEdit and InsertReplaceEdit shapes,
`itemDefaults`, and one replace-`[replaceStart, point)` accept rule for every source,
which also fixed a real line-corrupting bug against servers that fuzzy-match
server-side; plus incremental narrowing — `sortText`/`filterText`/`isIncomplete`
parsed, the item set kept across keystrokes and refiltered locally via `FuzzyMatch.h`,
the server re-asked only when it said `isIncomplete` or point left the word) and
`completion-popup-scroll`. Both live in `Editor/CompletionSession.h`, not the
`Source/UI/CompletionController.*` this file originally proposed — none of that logic
needs a terminal, so it follows `IncrementalSearch`/`SnippetSession`'s precedent
instead and is unit-tested without a `Screen`.

`completion-resolve` closed the rest of the wire surface:
`completionItem/resolve` (debounced on selection change, merging
`documentation`/`detail`/`additionalTextEdits` back into the live session),
`additionalTextEdits` (an accepted `std::vector` adds its `#include`, in the accept's
own single undo step), server-declared `triggerCharacters` with a real `triggerKind: 2`,
plus `preselect` and `commitCharacters`.

- [ ] `commitCharacters` and `preselect` are honored only where a server actually
      declares them — no default set is ever substituted. That turned out to be
      less protective than it sounds: verified live, `typescript-language-server`
      sends `{".", ",", ";", "("}` on *every* item, so `ned/set-lsp-commit-characters`
      (default on, VS Code's own default) is the switch for anyone who doesn't want
      `;` accepting whatever suggestion happened to be showing. Revisit the default
      if that proves annoying in practice.
- [ ] `additionalTextEdits` are applied against the buffer as it stands at accept
      time, not as it stood when the server computed them. Anything positioned
      *before* point is unaffected by the intervening keystrokes (typing a newline
      dismisses the session outright, so no line above point can shift), which covers
      every real case — an `#include`/`import` near the top of the file. An edit
      positioned after point on point's own line would be off by the characters typed
      since; no server has been observed to send one.
- [ ] Considered and deliberately *not* prioritized (recorded so it stays a conscious
      call): merging non-LSP candidates into the same popup. `RequestCompletionAtPoint`
      (`BufferView.cpp:5427-5438`) is a mutually-exclusive cascade — LSP if running, else
      Janet-binding completion in janet-mode, else dabbrev — so buffer words and snippet
      triggers are strict fallbacks that vanish the moment a server attaches, never ranked
      alongside server items. A real fix means a completion-source abstraction and a
      source-neutral candidate type. `completion-fidelity` moved this closer without
      doing it: `CompletionSession`'s own `CompletionCandidate` is already the wrapper
      such a type would grow out of, but its payload is still the LSP wire struct and
      the fallback sources still synthesize LSP items to fit it. Bigger than the
      fidelity work above and independent of it.

### Parsing Engine: Trait Vocabulary over Per-Language Queries (Design Sketch Only)

Full design in `Docs/ParsingEngine.md`, which carries the measurements this summary
compresses. Unstarted, and deliberately staged so each phase can be the last one.

The problem, measured against this checkout: a tree-sitter grammar carries structure and
no meaning, a `.scm` query carries meaning and no structure, and nothing connects or
validates the two — so ned's 79 query files are not queries, they are *the missing shared
vocabulary, hand-written once per language*. There are 114 distinct node names meaning
"a delimited body" across 18 grammars (52 for "parameter", 44 for "string", 43 for
"import"); 61% of the 1,391 visible rule names across all 30 grammars appear in exactly
one of them.

Two numbers carry the case:

- **96% of every fold rule is restated verbatim as an indent rule** (53 of 55 nodes
  identical across the 12 languages having both), and 78% of `@dedent` captures are
  mechanically `(X close-token @dedent)` for an `X` already in that file's `@indent` list.
  One fact — *this node is a delimited body* — currently gets stated two to three times
  per language, 21 languages over.
- `Queries.h` holds **113 embedded query constants over 29 languages x 8 driver kinds =
  232 cells, so 119 gaps (51%)**. Highlights is the only column at 29/29, and only because
  upstream ships `highlights.scm`; every column ned authors itself is 34–72% empty, each
  empty cell a language silently missing a feature.

The proposal is to declare structure and meaning in **one artifact per language**, so a
trait travels with the rule it is attached to and an unsatisfiable trait is a build error
rather than an empty runtime result. Three tiers: **Tier 0** infers delimited bodies,
token spans, nesting depth and matched delimiters straight from `grammar.json` with *no
per-language work at all* (a grammar for a language invented next year folds correctly the
day it is dropped in); **Tier 1** is ~30-40 composable declared traits (`Binding`, `Scope`,
`Callable`, `TypeDecl`, `Parameter`, ...), open rather than a closed enum; **Tier 2** is
first-class escapes — arbitrary predicates and Janet host callouts, which is where Org's
`*`-counting heading level and its runtime-configured TODO keywords belong instead of
forcing a hand-built C++ mode against a forked grammar. Drivers consume traits and never
node names, registering the way `Vcs/Provider.h` providers already do, so N x M becomes
**N mappings + M drivers** — 37 things to write instead of 232, with no gaps by
construction.

The risk worth arguing before starting: Tier 1 is where judgement lives, and a universal
vocabulary can degenerate into another hand-maintained translation table wearing a better
name. The early falsifiable test is **Lisp**, the case that already broke —
`queries/clojure-locals.scm` unrolls binding vectors by pair index and documents its own
cliff (*"the unrolling stops at eight pairs"*), so a ninth binding is silently
unrenameable. If Tiers 1+2 express `(let [a 1 b c] ...)` without a cliff the vocabulary is
real; if not, that is worth learning at language 3 rather than language 15.

- [ ] **Phase 0 — Oracle.** Checked-in snapshot of tree-sitter's tree plus all 8 fact
      kinds over a corpus of real files. Everything after validates against it; without it
      none of the rest is falsifiable.
- [x] **Phase 1 gate — Tier 0 inference proven.** The claim the whole design rests on
      (a delimited body is derivable from `grammar.json` with no per-language rules) is
      measured, not assumed: **55/55**, via `Tools/TraitInferenceProbe.py`. Needed three
      refinements, each a real property of how grammars are written — inline hidden rules
      (Clojure hides its parens one level down), allow optional members after a closer
      (JavaScript's `statement_block`), and accept an external token as a closer with no
      opener (Python's `block`). It also corrected the criterion itself: inference reports
      211 nodes beyond the 55, all genuinely delimited and none worth folding, so Tier 0
      infers **`Delimited`** and `Foldable` is a small Tier 1 policy on top. See
      `Docs/ParsingEngine.md`.
- [x] **Tier 0 inference is real code now.** `Editor/TreeSitter/TraitInference.h` —
      a pure function over parsed `grammar.json`, no `Parser`/`Buffer`/`Screen`, so the
      crafted-grammar cases pin each rule in isolation and name which shape broke.
      `Tests/TraitInferenceTest.cpp` enforces the 55/55 gate against the real grammars,
      so a grammar bump that breaks inference fails the build rather than being noticed
      much later. Verified the gate actually fails: disabling hidden-rule inlining
      reports the six Clojure nodes by name.
- [ ] **`Foldable` as the first Tier 1 policy over `Delimited` — partly answered, and it
      moved the question.** Two static filters were measured against the corpus. "The
      opener must be the node's first member" is **refuted**: it drops 16 of the 55
      hand-written fold nodes, because plenty of real ones carry content before the
      brace. "The interior must be list-like (a REPEAT or CHOICE between the
      delimiters)" is safe — keeps 55/55 — but only removes 65 of the 211 extras.
      The more interesting finding is that a large part of the answer is **not static at
      all**: most of the remaining extras (`parenthesized_expression`, `index_expression`,
      `string_literal`) are single-line in practice, and a fold that cannot span a line
      is meaningless regardless of grammar. So `Foldable` ≈ `Delimited` + list-like
      interior + spans more than one line, with the last term per-instance rather than
      per-language — which still supports N + M, just not purely by static policy.
      Remaining: decide whether `argument_list`/`parameter_list` are genuinely
      unwanted (most IDEs fold them, and the hand-written queries call themselves
      "deliberately minimal"), and whether `cpp`'s missing `declaration_list` is an
      omission inference just found.
- [ ] Fold semantics for indentation languages need their own pass, surfaced by the
      above and deliberately not bundled into it: a Python one-statement body is a block
      whose start and end land on the same line, so the "spans more than one line" rule
      that is right for C would drop it, and `[startLine + 1, endLine + 1]` does not
      obviously name the right rows for it either. Needs deciding on its own terms rather
      than inheriting the brace-language rule.
- [ ] **Phase 1 remainder — the language-definition format and compiler.** With inference
      in place, the rest: the Janet trait-declaration format, a build-time compiler
      emitting one artifact per language, and `Foldable` as the first Tier 1 policy over
      `Delimited` (inference reports 211 nodes beyond the 55; the policy excluding
      `parenthesized_expression`/`argument_list`/`string_literal` is what turns structure
      into a feature). Wiring the inferred facts into `Tests/Oracle` is the natural join
      with Phase 0 — the 55-vs-211 gap should be visible as a diff, not a number in a doc.
- [ ] **Phase 2 — Trait-driven structural drivers.** Fold, indent, dedent, structural
      selection, sticky scroll, brace match. Exit criterion is *deletable files*: roughly
      half the `.scm` corpus, and the Folds/Indents columns at 29/29 with no adapter
      authored.
- [ ] **Phase 3 — Semantic drivers.** Scopes/bindings/references as a real resolution
      layer rather than query captures. Exit criterion: the Lisp eight-pair cliff is gone,
      or Tier 1 is proven insufficient and the design is revised before any engine work.
- [ ] **Phase 4 — The engine, a separate decision.** Only once the vocabulary is proven
      against 29 real languages. This is where the remaining tree-sitter complaints live:
      stable node identity across a reparse (a red-green tree, so a node handle is
      storable and `Node::Id()`'s byte-range-collision workaround goes away); incremental
      per-subtree facts (today `symbolKind` is O(document) per keystroke and cannot be
      windowed, because `StickyScroll` needs enclosing definitions); an edit-driven API
      (today `IncrementalParseCache` walks the whole document twice per keystroke to
      rediscover an edit ned already knew); native ranged parsing (removing
      `HugeStructuralWindow`'s two empirically-found corrections); injected subtrees as
      real children (removing `EmbeddedDocuments.cpp`'s width-preserving whitespace
      padding); and GLR error recovery with anchor sets plus missing-token insertion.
      Packaging follows for free — `grammar.json` ships in all 24 repos, the parse tables
      are already data, and only the lexer DFA and the external scanners are code. The 19
      upstream external scanners (~10,600 LOC of C) keep working unmodified behind a shim:
      `TSLexer` is 7 function pointers and the scanner vtable is 5 slots. Conformance is
      free — upstream ships 235 corpus files, ~109,000 lines.
- [ ] Decided: **ned's own language definitions are authored in Janet, not in a
      tree-sitter-style `.scm`.** Janet is already the extension language and is
      homoiconic, so the declarative trait tier is plain Janet *data* while the escape
      tier is Janet *code* — which is what buys arithmetic, real quantifiers and access
      to runtime host state, the three things tree-sitter's predicate system
      structurally cannot express. An `.scm` reader stays, but only to consume upstream:
      ned uses 32 query files it does not write (20 `highlights.scm`, 9 `tags.scm`, 3
      `injections.scm`), and highlighting is at full coverage precisely because upstream
      ships it. Two disciplines make it hold: keep the declarative tier pure data (no
      evaluation to *load* a language), and *declare* escapes rather than embed them, so
      they live in the user's `init.janet` and the grammar corpus stays data — Org's
      `TodoKeywords` is already this shape. Full reasoning in `Docs/ParsingEngine.md`.
- [ ] Recorded as a conscious call rather than a default: **keeping `grammar.json`
      ingestion is a hard constraint**, and it permanently forecloses the resilient-LL
      path (matklad's) that would give better error recovery, because LL means
      hand-written grammars and therefore losing every language nobody here personally
      writes a grammar for.
- [ ] Also a conscious call: **not** extracting this as a standalone library. Phases 2-3
      have legitimate pull into ned specifics (Janet host callouts, `Mode`'s capability
      surface, `SyntaxClass`); designing library-first would make it worse at the job it
      exists for. Extract later if it earns it.

**Language coverage** — full catalogue in `Docs/LanguageCoverage.md`: the depth ladder
(D0 structural / D1 navigational / D2 integrated / D3 bespoke), Tier A flagship through
Tier D parked, a graveyard with revisit triggers, and an 8-point grammar admission policy.
The reframe that makes it affordable: **a tier is a commitment to a depth, not a decision
about whether a language works at all** — D0 falls out of Tier 0 inference for free, so
"basically every known language" becomes a real target rather than a boast, and the honest
answer to a request for an obscure DSL becomes "yes, next release".

The concrete near-term grammar and config items this implies — Tier A's D2 gap, Lua and
CMake, diff, `.scm`, and SQL as the acceptance test for Tier 1 rule inheritance — are
**independent of the engine work** and are tracked under "Release 0.6" at the top of this
file rather than duplicated here. SQL is the one worth restating, because it is the
strongest demo of the architecture rather than a line item: ned has none today, and
upstream is four grammars for one language family precisely because tree-sitter cannot
express "T-SQL is ANSI SQL plus these deltas", so every dialect forks the whole grammar and
drifts. `DerekStride/tree-sitter-sql` (2026-09-10, 245★) as the core plus per-dialect trait
deltas would be better than anything upstream currently offers. Graveyarded en route:
`dhcmrlchtdj/tree-sitter-sqlite` (archived 2023), `m-novikov/tree-sitter-sql` (stale
2024-03).

- [ ] Admission policy worth knowing before adding any grammar: **prefer
      `tree-sitter-grammars/*` over the original personal repo, and never use star count as
      a health signal.** Measured 2026-09-11 — `alemuller/tree-sitter-make` is 51★ and stale
      since 2024-01 while `tree-sitter-grammars/tree-sitter-make` is 17★ and current;
      `MunifTanjim/tree-sitter-lua` is a 1★ fork against the org's 104★. Use `pushed_at` and
      `archived`, pin by tag, and record ABI version + external-scanner LOC + corpus presence
      at admission.

### Refactoring

JetBrains-class rename/move refactoring, scoped the way an IDE scopes it: a local
variable renames locally, a symbol renames across everything that actually references it,
and moving a file fixes up what pointed at it. Ned already owns most of the substrate --
`lsp-rename` (`C-c C-M-r`) drives `Manager::RequestRename`, whose multi-file `WorkspaceEdit`
lands through `ApplyProjectEdit` as one `ProjectUndoManager` transaction; `rename-file` and
the sidebar's own rename already send `workspace/willRenameFiles`/`didRenameFiles` and apply
the resource operations a server sends back, and `rename-symbol` now resolves a local
binding from the mode's own `locals.scm` with no server involved at all, routing the result
through a review buffer rather than applying it blind, and a file rename now fixes up the
imports that named it in both directions. What is still missing is the file/class
relationship an IDE keeps in sync.

Shipped here, one slug each for `git log --grep=`: `class-file-sync` (a file's name and
the single type declared in it kept in agreement, both directions:
`rename-file-to-match-type`/`rename-type-to-match-file` on M-x, plus an unprompted y/n
after a rename lands. Two strictness tiers, and the split is the design --
`Editor/ClassFileSync.h`'s pure half is strict for what ned volunteers (exactly one
top-level type, and the file was demonstrably named after it a moment ago, which is why
arming happens BEFORE the edits land) and best-effort for what the user asks for (the
outermost of several, compound suffixes preserved so `Thing.class.php` stays
`*.class.php`). Direction A routes through `PerformProjectRename`, so the import fixup and
`willRenameFiles` come along free. `ned/set-class-file-sync` gates only the offers.
Three real bugs found on the way: `definition.module` was classed `TypeLike` though every
grammar emitting it emits it for a real namespace, which made every namespaced PHP file
read as two top-level types; tree-sitter-typescript's `tags.scm` is a delta on
JavaScript's and carries no `class_declaration` at all, so every TypeScript class and
function had NO symbol marker whatsoever; and `InvalidateModeDependentCaches` cleared what
the symbol cache feeds but not the cache itself, so a buffer visited after one in another
language kept that language's answers until the next edit),
`file-rename-propagation` (renaming or
moving a file now rewrites the imports that named it and the relative imports it wrote
itself, with no server involved -- `Editor/ImportFixup.h`'s pure specifier arithmetic plus
a planner over `Mode::importTargets`, routed through the same editable review multibuffer
a rename is, default on via `ned/set-import-fixup`. A server that answered
`workspace/willRenameFiles` with edits of its own still wins outright. The other direction
landed with it: `FileWatch` now masks `IN_MOVED_FROM` and pairs inotify's rename cookie,
so a `git mv` made outside ned becomes a real "this moved" signal -- the open buffer
follows the file, the server is told, and the same import review comes up, resolved
backwards through `MatchMovedTarget` since the file is already gone by then),
`rename-review` (both rename tiers hand
their edits to an editable `*rename*` review multibuffer before anything lands, default on
via `ned/set-rename-review`; every hit classified against its own file's highlighter, the
comment/string occurrences the rename never asked for listed and excluded until `M-a`
includes them, `Editor/RenameReview.h` holding the pure half), `lisp-shell-locals` (fish, janet and
clojure/jank locals queries, closing the coverage gap `scope-aware-rename` left. HTML and
CSS were dropped from that gap rather than filled: both have a binding construct, but a
CSS custom property is scoped to matching elements *and their descendants*, which is DOM
containment, so the byte containment `LocalScopes.h` resolves by would produce a rename
that silently missed every descendant use. The full list of languages with no locals
query, and why each is on it, is recorded beside the declarations in
`Source/Editor/TreeSitter/Queries.h`) and `scope-aware-rename` (`rename-symbol` on
`C-c C-M-r`, tiered -- a name that resolves to a binding this file wholly owns is renamed
in-buffer as one undo step with no request sent, and anything else falls through to the
`prepareRename`/`rename` flow, which `lsp-rename` still reaches directly from `M-x`.
Twelve hand-authored `*-locals.scm` queries, a `Mode::localScopes` capability, and
`Editor/LocalScopes.h`'s pure resolver; `TreeSitterMode`'s six positional query-source
parameters became a designated-initializer `TreeSitterQuerySources` on the way past).

- [ ] A Lisp binding vector's names are captured by unrolled per-pair-index patterns
      (`clojure-locals.scm`, `janet-locals.scm`), because the whole-vector form a query
      would naturally express captures a bare-symbol *value* as a definition and turns a
      rename of the outer binding it names into a partial one. The unrolling stops at
      eight pairs, and destructuring (`[{:keys [x y]} m]`) is not captured at all; both
      degrade to "declines" rather than to a wrong rename. Lifting either needs something
      the query language cannot say, so it would mean a capture kind `LocalScopes.h`
      understands positionally -- not worth it until a real file hits the cap
      (`lisp-shell-locals`).
- [ ] Fish's `set -l -x count 0` (a scope flag not adjacent to its own target) and
      `read -l line` are not captured as definitions -- the first because the pattern
      anchors the name to the flag before it, the second because there is no `set`
      command node to hang off. Both decline rather than mis-resolve
      (`lisp-shell-locals`).
- [ ] A use that textually precedes its own binding in a whole-scope-binding language
      (Python's function scope, JavaScript `var` hoisting) is detected and *declined*
      rather than resolved -- `LocalBinding::usedBeforeDefinition`, the one case where
      the resolver's position rule knowingly gives up. Resolving it properly means
      knowing per language whether binding is declaration-point or whole-scope, which is
      a real per-language fact this deliberately did not invent a place to record
      (`scope-aware-rename`).
- [ ] A huge buffer (`ITextStorage::IsHuge()`) never gets the scope-aware tier at all.
      Unlike the fold/symbol/test gutters beside it, this one cannot window: a binding's
      occurrence set is only complete if the whole file was parsed, so a windowed answer
      would be a partial *rename*, not a partial display (`scope-aware-rename`).

- [ ] A rename carrying filesystem resource operations (a `documentChanges` edit that also
      creates, deletes or renames a file) is applied directly rather than reviewed -- a
      multibuffer has no way to represent a resource op, and reviewing half an edit is
      worse than reviewing none of it. Same for a huge source among the touched files, and
      for a server whose `newText` isn't the new name verbatim (a qualified or re-cased
      replacement, which the review has no way to propose). All three say so in the echo
      area rather than behaving differently in silence (`rename-review`).
- [ ] An excerpt mixing a real reference with a comment/string hit on the same line is one
      row with three states, not two independently togglable hits -- `M-a` steps it from
      original to references-only to every occurrence. Per-hit granularity would need the
      review to carry sub-excerpt ranges, which nothing else in the multibuffer does
      (`rename-review`).
- [ ] The comment/string candidate scan covers only the files the rename already touches.
      A name that appears in a comment in a file with no code reference at all is never
      offered -- that would be a project-wide search, which `project-replace` already is
      (`rename-review`).

- [ ] An import fixup only ever rewrites a specifier in the style it was already written
      in, and declines anything that style cannot express: an angle-form system include, a
      PHP `use` namespace, a Rust `mod` declaration, a bare package specifier, and a target
      that moved out of the root its specifier counts from (a project-root-relative
      `"Editor/Widget.h"` moved outside the project). Declines are counted and named in the
      review's status line rather than guessed at. Lifting any of them means teaching the
      rewriter a second style to write in, which is what the "never restyle" rule exists to
      refuse (`file-rename-propagation`).
- [ ] An externally detected move is only seen inside a directory ned already watches --
      the parent of an open buffer -- and only paired when BOTH ends are such a directory.
      A rename inside one always pairs (the moved file itself need not be open); a move
      into a directory nothing is open in delivers inotify's `IN_MOVED_FROM` half alone,
      which still triggers the existing sweep but has no destination to name, and none is
      guessed at. Watching the whole project tree would fix both and cost a watch per
      directory, which is the trade that was declined (`file-rename-propagation`).
- [ ] An externally detected move opens its review unprompted, switching the focused pane
      to it. That is the point -- the imports are broken either way and the review is
      non-destructive -- but a branch switch that moves many files will interrupt whatever
      was on screen. A "N imports need fixing (C-c i to review)" status line instead, with
      the review built on demand, is the obvious alternative if it turns out to grate
      (`file-rename-propagation`).
- [ ] The import scan runs synchronously on the main thread, inside the rename itself:
      one `SearchDirectory` pass for the moved file's own name, then a tree-sitter parse of
      each file that matched. Measured at ~1.2s for the worst case in ned's own tree
      (renaming `Mode.h`, whose stem half the codebase mentions) and far less for an
      ordinary file. Bounded by `ned/set-import-fixup-max-files` and skipped outright for a
      move with no end inside the project root. Worth moving off-thread only if a real
      repository makes the pause visible (`file-rename-propagation`).

- [ ] The automatic offers ride on a SERVER rename landing, or on a `*rename*` review being
      committed -- never on the no-server in-buffer rewrite, because renaming a top-level
      type is always cross-file and `rename-symbol`'s scope-aware tier declines it by
      construction (`LocalBinding::scopeIsFile`). With no server configured for a language,
      `rename-file-to-match-type`/`rename-type-to-match-file` are the whole feature; they
      need none (`class-file-sync`).
- [ ] `rename-type-to-match-file` with no server renames the declaration and its whole-word
      occurrences **in that file only**, and says so. The "top-scoped name pair" is the
      reliable half -- this file's one top-level type is named after this file -- and the
      dependency tree is the half it cannot follow. The obvious lift is a project-wide
      whole-word scan into the same review (`Project/Search.h` plus `ClassifyHit`, which is
      what `project-replace` already is), deliberately not built into this: a rename that
      silently rewrote matches across a repository on a name match alone is a different,
      riskier feature than the one asked for (`class-file-sync`).
- [ ] Java/C#/PHP/TypeScript tags coverage is now upstream's file plus a repo-local delta
      (`ned_embed_treesitter_query_concat`). The deltas are small and current; the thing to
      watch is a grammar bump making one redundant, which shows up as a duplicate marker
      rather than a wrong one (`class-file-sync`).

- [ ] **Change signature (the hard one, scoped honestly).** LSP has no request for this --
      JetBrains does it from its own index, and no server offers an equivalent -- so it is
      ned's own transform or nothing. Renaming a parameter falls out of the `locals.scm`
      item above and needs nothing else. Adding, removing or reordering parameters means
      rewriting call sites, which needs a per-language structural query (a `calls.scm`
      alongside `tests.scm`, mapping a call expression to its argument list) plus an edit
      planner that maps old positions to new ones and drops or defaults the rest. First cut
      should be one language family and the review buffer above making the blast radius
      visible before anything lands; a signature change that silently rewrote fifty call
      sites would be the least trustworthy feature in the editor.

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

Shipped here, one slug each for `git log --grep=`: `context-menu` (the right-click sweep
across `BufferView` content+gutter, `TabBar`, `ProjectSidebar`, `VcsPanel`, plus
double/triple-click select, gutter click, middle-click paste, scrollback click-drag),
`mouse-hover` (hover tooltips), `hunk-context-menu` (hunk stage/unstage/revert, and
`vcs-revert-hunk` as a genuinely new capability), `sidebar-drag-drop`.

- [ ] Sidebar drag-and-drop leaves the dragged file open in whichever pane was already
      focused, in addition to the drop target — the row's own press-time preview-open
      fires before any drag is known to be one. Fixing it means moving long-tested
      click-vs-double-click timing off Pressed and onto Released, which is why it was
      accepted as a v1 trade-off rather than restructured (`sidebar-drag-drop`).

### Navigation & Search

Shipped here, one slug each for `git log --grep=`: `full-commit-diff-view`,
`auto-collapse-on-build`,
`live-buffer-search` (project search, project replace, find-references' no-LSP fallback
and the MCP search tool all read an open buffer's live content in place of its file) and
`project-replace-review` (project-wide replace is now an editable review multibuffer
committed with `C-c C-c` into live source buffers as one undoable project transaction,
replacing the old flat preview plus whole-batch y/n disk rewrite; `CommitExcerptChanges`
records a `ProjectUndoManager` transaction for every wgrep-style commit now, not just
this one), `project-replace-apply-targets` (one replace path, not two: `C-c C-c` asks
whether the reviewed text goes into the open buffers or straight to the files, `M-c` asks
the same for just the file under point, and the old `ReplaceMatches` disk-rewrite
endpoint was deleted with its file-preservation half folded into `CommitExcerptChanges`;
plus the review's own `M-n`/`M-p`/`M-r`/`M-R` minor-mode layer and `undo-buffer-only`),
`live-search-huge-buffers` (a huge modified buffer is line-chunk scanned through its own
storage instead of falling back to a stale disk read, and an excerpt whose source is huge
says `[huge]` in its header), and `multibuffer-gaps` (the excerpt cap plus the byte-exact,
column-preserving jump-to-source, and a results line whose path resolves to nothing
now reporting a miss instead of creating an empty buffer named after it; also fixed a
real `Buffer::ExcerptRange` relocation bug it surfaced — undoing an edit inside an
excerpt body left the range one byte long, which `CommitExcerptChanges` would have
written back into the user's source file as a spurious blank line; and every excerpt
body now reads an open buffer's live content first, so what an excerpt shows and the
source range it names can't disagree over unsaved edits), `multibuffer-scoped-search`
(isearch and query-replace confine themselves to excerpt bodies inside a multibuffer, so
neither stops on an excerpt's own header path nor offers a replacement the buffer would
then silently refuse — `ned/set-multibuffer-scoped-search` turns it off) and
`multibuffer-search-in-results` (`search-in-results` on `C-c s`: a fresh search over just
the files the current results buffer already names, resolved from a multibuffer's own
excerpt sources or from a flat `path:line:` buffer's own lines).

- [ ] Excerpt-scoped search covers isearch and query-replace only. A multibuffer's
      chrome is also visible to `next-error`, dabbrev completion and Vim-mode `/`
      search, none of which consult `multibuffer::ExcerptBodyRanges`
      (`multibuffer-scoped-search`) — none has been annoying enough in practice to
      chase, and each would need its own scope plumbing rather than sharing one.
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

Shipped here, one slug each for `git log --grep=`: `terminal-panel-scrollback`,
`jumplist-ring`, `changelist-ring`, `dot-repeat-count-override`, `vim-global-marks`,
`vim-magic-translation`, `vim-macro-register`, `dap-round-3` through `dap-round-5`,
`session-persistence-round-2`, `snippet-expansion-gaps`, `bundled-snippets`,
`multiple-terminal-tabs`, `unified-left-dock`, `test-runner-gaps` (gutter-click
run-this-test plus a pre-run `▸` affordance, the failures-only degradation surfaced
instead of silently degrading, and go's basename-only `file:line` resolved through the
import path it already reports).

- [ ] **A determinate progress bar, and a huge save that can paint one.** The mode line's
      spinner is the right answer for indeterminate work and stays; what has no answer at
      all is a long operation whose end *is* knowable. Saving a multi-GB buffer is the
      motivating case, and the blocker is not the widget:
      `save-buffer` calls `Editor/BufferSave.h`'s `WriteBufferToDisk` **synchronously,
      inside command dispatch, on the main thread** (`Commands.cpp`'s `saveBufferBody`),
      so a huge save freezes the event loop outright — nothing repaints, and a progress
      bar added today would never be drawn during the one operation it exists for. The
      `Editor/Backup.h` pre-save version write (huge buffers included, 64GiB cap) is on
      that same blocking path, so the wait is roughly doubled.
      Two pieces, in order:
      - **Make the huge save yield.** `Buffer::SaveToFile`'s huge branch is already a
        streaming `ITextStorage::ForEachChunk` pass, so the chunk boundary is exactly
        where a progress callback and a yield point belong. The shape to copy is the
        load side, which already solved this: `UI/AsyncFileLoader.h`/`HugeFileLoader.h`
        run off-thread and marshal back via `EventLoop::Post`, with `Buffer::IsLoading()`
        gating what may touch the buffer meanwhile. A symmetric `IsSaving()` is the
        obvious counterpart — `save-buffer` already refuses a still-loading buffer, so
        the precedent for refusing during the inverse exists too. The real design
        question is what an edit *during* a save should do, which the load side never had
        to answer (a loading buffer has no user content yet).
      - **Then the widget.** Determinate, 0..1 plus a label, painted through a themed
        surface like every other chrome. Two consumers already have a real fraction the
        moment one exists: `Buffer::CurrentLoadProgress` (huge load, currently rendered
        as `Loading... 45%` text in the mode line) and LSP `$/progress`, whose
        `percentage` `Lsp/Manager.cpp` already parses and then discards into a string.
      Deliberately *not* the "state-driven mode-line fills" idea listed under Translucency
      phase 5 — that one washes the whole bar and was set aside as decoration; this is a
      real widget for real determinate work.

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
- [ ] `TestSourceResolver`'s basename search picks the shallowest candidate when
      nothing disambiguates (no go package hint, no directory components in the
      reported path, several same-named files) — deterministic, but it can be the
      wrong file. A prompt-to-choose would be the honest answer; not worth it until
      the silent wrong pick is actually seen.
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
- [ ] **`libned` as a real shared library** — `ned_lib` (static today) exists solely so
      `ned_tests` can link real editor code without pulling in `main()`; a static lib
      already does that job. Worth revisiting only if a second real consumer shows up
      (an embedding use case, a separate CLI tool) — would need symbol-visibility
      curation and SONAME/ABI-versioning discipline that don't pay for themselves yet.
      Note the likeliest second consumer, a headless remote agent, does *not* force this:
      it links the existing static `ned_lib` fine, scripting runtime and all left behind
      (measured — see "Remote Development"'s toolchain-choice item).
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

Shipped here, one slug each for `git log --grep=`: `merge-conflict-resolution` (the
`Text/ConflictHunk.h` model, the `merge-take-*` commands on a `C-c x <letter>` prefix,
hunk navigation, the themed ours/theirs/base tint) and `conflict-quick-keys` (the
conflict-scoped `M-o/t/b/d/k/n/p` layer). No modal state was introduced at all, a
deliberate simplification over the original scoping — resolution is ordinary commands
over ordinary buffer text, so "never a modal trap" fell out for free.

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
      (`Lsp/Client.h`, `Dap/Client.h`, `Acp/Client.h`): background `jthread`
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

Local-only slice shipped (`Editor/Project/Registry.h`, `switch-project`/`open-project`
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

### Remote Execution & Server Protocol

- [ ] **Design ned's own client/server protocol** (raised 2026-09-08 — unstarted, no design
      committed yet; this entry records the shape of the problem and what's already known,
      not a spec). The motivating idea: rather than a remote ned shipping buffers back and
      forth, send *the operation* to where the files are and return only the result — a
      project-wide search, a refactor, a script evaluation. Round-trip count, not bandwidth,
      is what makes remote editing feel bad, so this is likely faster as well as simpler.

    **The load-bearing design decision — local is the degenerate case.** The protocol
    should be the *only* interface, with in-process execution as one transport behind it
    rather than a bypass around it. Two things follow. It can't rot: every local keystroke
    exercises the same path a remote session uses, so remote stops being a bolt-on that's
    broken every time it's picked back up. And it makes "where does this script run"
    a transport question rather than an architectural one — the same request answered
    in-process, by a local subprocess, or by a host across a socket.

    That last point interacts directly with the jank analysis above: if scripts execute
    where the files are, the heavy runtime (jank + Clang/LLVM + a 68 MB PCH, ~237 MB RSS)
    lives on whichever side actually runs them. A remote session's client could then be
    genuinely thin — and, per the same analysis, a headless binary already links
    `libned_lib.a` cleanly with no scripting runtime at all (verified: `Text/` +
    `ProjectSearch` + `GitIgnore`, 8.7 MB, `-ljanet` removed entirely). Only 12 of 316
    objects in `ned_lib` touch anything named "janet", three of those are the tree-sitter
    *grammar* rather than the runtime, and the whole UI-side coupling is one call
    (`Environment::BindingNamesWithPrefix`, for binding-aware completion). The split is
    already there to be taken.

    **Compression must be negotiated, and "none" must be first-class.** Nothing
    compression-related is linked into ned today — `ldd build/ned` shows no zstd, zlib,
    lzma or brotli — so any codec is a new dependency, and assuming one is present on
    both ends is exactly the trap to avoid. Compress per-frame payloads, not the stream,
    or the encoding can't change after the handshake; advertise available codecs at
    handshake and fall back to identity. LSP's own `capabilities` exchange is the model,
    and this codebase already understands it well.

    **This must inherit four protocol bugs already paid for, not rediscover them.** Ned
    has built four framed clients — LSP (`Content-Length` JSON-RPC), DAP (a `seq`/`type`
    envelope over LSP's framing), ACP (newline-delimited JSON), and the broker (an
    `AF_UNIX` relay) — and each of these was a real, root-caused, user-visible failure:
    - an unbounded blocking `connect()` froze a live editor when the daemon's backlog
      filled (`Lsp/BrokerConnect.cpp` now does the non-blocking-connect + `poll` dance);
    - joining a reader thread under a held mutex wedged the daemon for hours;
    - `poll()` on a `-1` fd with a `-1` timeout parks forever — the root cause of the
      `ctest -j8` timeouts;
    - an unbounded `WriteAll` hung the UI, fixed by a per-client writer thread in
      `LspClient`/`DapClient`/`AcpClient`.
    A new protocol gets timeouts on every blocking call, a non-blocking connect, an
    asynchronous write queue, and no lock held across a join — by construction, on day
    one. `EventLoop::Post` is the existing, proven way results come back to the main
    thread; the protocol layer should not invent a second one.

    **Open questions worth settling before any code:** framing (length-prefixed binary vs.
    reusing the `Content-Length` shape already implemented three times); whether requests
    are JSON (nlohmann is already vendored) or something denser; how a long-running remote
    operation streams partial results and gets cancelled (LSP's `$/progress` and
    `$/cancelRequest` are the obvious prior art, already handled in `LspManager`);
    versioning and forward compatibility; and authentication/transport (bare `AF_UNIX`
    locally vs. stdio-over-ssh vs. TCP — the broker's socket-path and trust conventions
    in `BrokerSocketPath.cpp` are the local precedent).

    **Security is not a later concern here.** "Execute this script over a socket" is a
    remote code execution surface by definition. Ned already gates project-local
    `.ned/init.janet` behind `ProjectTrust`'s content-hash registry precisely because
    opening a directory shouldn't run arbitrary code; the same discipline has to extend
    across a transport, where the threat model is strictly worse. Decide the trust model
    alongside the framing, not after it.

- [ ] **One connection class instead of three copies of it** (raised 2026-09-08 —
      prerequisite for the protocol work above, and worth doing on its own merits).
      `LspClient`, `DapClient` and `AcpClient` each hand-roll the same machine, and the
      headers say so outright: *"Threading, lifetime, and member-declaration order all
      mirror LspClient"* (`Dap/Client.h`), *"mirrors LspClient's own stderrThread_ exactly"*
      (both), *"see LspClient.h's own comment on writeCv_"* (both). All three carry the
      identical member set — `readThread_`/`stderrThread_` declared *before* `transport_`
      so its destructor's fd close unblocks them, then `writeThread_` with
      `writeMutex_`/`writeCv_`/`writeQueue_`/`drainQueueOnStop_` declared *after* it for
      the mirror-image reason. That ordering is a load-bearing correctness invariant
      currently defended by a comment repeated in three files: reorder two members in one
      of them and you get a hang, not a compile error.

    **The seam is clean, because only the top of the stack actually differs.** Framing
    differs (LSP and DAP share `Content-Length` via `Lsp/Transport.h`; ACP is
    newline-delimited with its own). Envelope and dispatch differ (JSON-RPC id matching;
    DAP's `seq`/`type` request-response-event; ACP's genuinely bidirectional,
    async-capable handlers). Handshake gating differs (LSP queues until `initialized`).
    Everything *below* "turn bytes into one frame" — process spawn, the read loop, the
    stderr loop, the write queue and its thread, `EventLoop::Post` marshalling, shutdown
    ordering — is byte-for-byte the same idea three times.

    **Shape — composition, not an interface, and for a design reason rather than a cost
    one.** Nothing here ever holds a heterogeneous collection of clients: each one knows
    its framing at compile time and is named concretely at every call site, so there is
    no runtime type choice for a vtable to express (unlike `ITextStorage`, where `Buffer`
    genuinely cannot know whether it holds a `Rope` or a `PieceTable`, or `Widget::Paint`,
    or `VcsProvider`, whose implementation set is opened at runtime by Janet plugins).
    An abstract `ProtocolClient` base with a virtual `DispatchFrame` would be paying for
    a decision that is never made. So: `std::function` callables, which is also already
    the house idiom — the whole `Set*`/register-then-connect convention across `UI/` and
    the managers works exactly this way.
    - `Transport` becomes a concept with concrete implementations rather than one class:
      child-process pipes (today's `Process/ChildProcess`), `AF_UNIX`
      (`Lsp/BrokerConnect.cpp`'s non-blocking-connect + `poll` dance, currently 326 lines
      living alone), and later TCP/stdio-over-ssh for the remote protocol.
    - `FramedConnection` owns the threads, the queue, the member order and the shutdown
      sequence exactly once, parameterized by a read-a-frame callable and an on-frame
      callable. `LspClient`/`DapClient`/`AcpClient` each *own one* instead of
      reimplementing it, keeping only their envelope, dispatch and handshake logic.

    **The payoff compounds with the protocol item above.** The four bugs already paid for
    — unbounded blocking `connect()`, join-under-mutex, `poll(-1, -1)` parking forever,
    unbounded `WriteAll` — get fixed in one place, and any new client (the server
    protocol, a future MCP or nREPL endpoint) inherits all four by construction instead
    of re-earning them. It also deletes the "reorder these members and it hangs" hazard
    from two of the three files.

    **Honest risk:** this is a pure refactor of the most concurrency-sensitive and most
    historically bug-prone code in the tree, all of which currently works. It is only
    worth doing behaviour-preserving, one client at a time, leaning on the existing
    safety net — `LspClientTest`/`DapClientTest`/`AcpClientTest`/`LspTransportTest`/
    `AcpTransportTest`/`ChildProcessTest`/`TaskProcessTest`/the three broker tests, ~4000
    assertions across the three clients — kept green at every step, and re-run under the
    `sanitize` preset rather than just `default`. Do it *before* the server protocol, so
    the new protocol is the first consumer rather than a fourth copy.

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
- [ ] Transport/protocol for talking to the agent — now covered in full by "Remote
      Execution & Server Protocol" directly above, including the local-is-the-degenerate-
      case framing, compression negotiation, and the four protocol bugs a new client must
      inherit rather than re-earn. Note the earlier framing here ("a fourth following the
      same precedent is low-risk") is exactly what that section argues against: fold
      `LspClient`/`DapClient`/`AcpClient`'s triplicated machinery into one connection class
      *first*, so the agent transport is its first consumer rather than a fourth copy.
- [ ] Auto-deployment: detect the agent is missing or stale on the remote host and
      self-install it (`scp`/`sftp` push + `chmod +x`), the VS Code Remote-SSH/JetBrains
      Gateway precedent, with a version handshake so a stale cached copy gets replaced
      rather than silently mismatching.
- [ ] **Language/toolchain choice for the agent is genuinely open.** Reusing `ned_lib`'s
      own C++ `ProjectSearch`/`GitIgnoreMatcher`/RE2/`ChildProcess` code as a headless,
      UI-free build gives identical search/gitignore semantics locally and remotely for
      free and avoids a second implementation to keep in sync. **The "but `ned_lib` doesn't
      separate cleanly from Notcurses/Janet" objection recorded here earlier turns out to
      be false — measured 2026-09-08.** A headless program linking `libned_lib.a` with
      `-ljanet` removed from the link line entirely compiles, links and runs: 8.7 MB,
      opening a real `Buffer` and returning real `ProjectSearch` hits. Static-archive
      member granularity does the work — objects are only pulled in when referenced.
      Only 12 of 316 objects in `ned_lib` reference anything named "janet", three of those
      want the tree-sitter *grammar* rather than the runtime (`Languages`/`Mode`/
      `ModeOverrides`), `WindowManager` only forwards a `const*` through setters, and the
      entire remaining UI-side coupling is one call: `Environment::BindingNamesWithPrefix`,
      for binding-aware completion. No "split `ned_lib` first" prerequisite exists; a
      separate `add_executable` target is enough (note the existing headless modes —
      `--lsp-broker`, `--mcp-stdio-relay` — are *modes of `ned`*, so they do link
      `-ljanet` today despite never using it; a separate target is what lets the linker
      drop it). Rust or Go therefore look much weaker than they did: they would buy easier
      cross-compilation at the cost of reimplementing and hand-syncing gitignore/search
      semantics a second time, against a C++ path whose cost is now known to be near zero.
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
- [ ] Host-key verification / first-connection trust prompt — `Project/Trust.h`'s
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

Shipped here — see `git log --grep=ACP` for the panel's own long tail (interrupt/
spinner, thought/text split, streaming debounce, collapsed tool calls, composer
word-motion/history, auto-reconnect, word-wrap, checkpoint/rewind, Markdown rendering,
@-mention autocomplete) and `--grep=panel-dock` for the tabbed bottom dock that now hosts
Terminal/ACP/Debug Console on one tab strip.

- [ ] **AI-assisted editing (ACP) gaps** (validated live 2026-08-26 against Claude
      Code's own ACP adapter): no scrollback in the panel; `terminal/*`
      tool-call support and `elicitation/create` structured forms are undeclared as
      client capabilities; no multiple concurrent agents/sessions (still one at a time,
      `Dap/`'s own precedent); no `session/load` history replay;
      `session/set_config_option`/`session/set_mode` aren't surfaced to the user; no
      per-agent environment-variable override (`ChildProcess`'s `posix_spawn` always forwards the
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

Shipped here, one slug each for `git log --grep=`: `acp-mcp-tool-bridge` and
`acp-mcp-tool-bridge-remainder` (`Editor/Mcp/`, `ned/set-acp-mcp-bridge`, ~20 tools plus
`capture_note`; the original transport question resolved to a real local MCP server,
forced by the spec — stdio is the only transport every agent must support, so
`ned --mcp-stdio-relay` is a dumb byte pump into the live process's own Unix socket where
`LspManager`/`VcsRunner`/`TestRunner`/open `Buffer`s actually live), `dap-acp-bridge`
(structured DAP tools + `dap-ask-agent`), `acp-context-auto-attach` (`@buffer`/
`@selection` composer mentions, `ask-agent-about-line`), `acp-composer-prose-check`, and
`Text/LineDiff.h`'s `SplitLines`/`DiffLines`/`UnifiedDiff` with the agent-edit diff
preview built on it.

Deliberately cut from those slices, still open:
- [ ] **Real rename/code-action apply + `goto(file, line)` navigation** — all three
      need a live `WindowManager`/`BufferView` (`ApplyProjectEdit`'s multi-file
      transaction machinery for the first two — `ProjectUndoManager` recording, file
      create/rename/delete via `DocumentChangeOp`; `BufferView::JumpToPathLine` for the
      third) that `McpToolRegistry` doesn't have and doesn't currently reach. Not a
      thin wrapper the way everything shipped so far is — a real follow-up, not
      attempted here.

- [ ] **A `dap_get_pointer_graph`-shaped MCP tool** — the pointer-graph/memory/
      disassembly/thread/function-and-exception-breakpoint surface stayed out of the DAP
      tool set on purpose (not part of the ask/step/inspect loop that slice targeted),
      but the graph one specifically needs real work rather than another thin wrapper:
      `BufferView::ExpandPointerGraphNode`'s cycle-detection traversal would have to be
      extracted into a UI-free helper first (`Editor/PointerGraphNode.h`'s data shape is
      already reusable, the traversal isn't).

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

The mechanical barrier is gone now: `Patches/notcurses/` carries all three as real
`git am`-able files with proper commit messages, generated from the CMake scripts by
`Patches/notcurses/regenerate.sh` and verified to apply against pristine v3.0.17. Opening
a PR is a `git am` and a push rather than a re-derivation.

- [ ] **`CMake/PatchNotcursesNulKey.cmake`** (Ctrl+Space/Ctrl+@ swallowed as a NUL byte)
      and **`CMake/PatchNotcursesMouseWheel.cmake`** (SGR wheel-right, Cb=67, misdecoded
      as a motion+release) are both still-reproducible bugs against upstream
      `dankamongmen/notcurses` `master` as of 2026-09-06 (verified live against the
      current `src/lib/in.c`, not just our pinned `v3.0.17`) — checked GitHub issues for
      both ("Ctrl+Space", "NUL", "0x00", "wheel") and found nothing matching, so these
      look like genuinely novel, unreported bugs. Both patches are already small,
      root-caused, and general (not ned-specific workarounds), so they're close to
      PR-ready as-is whenever we decide to open them.
- [ ] **`CMake/PatchNotcursesBracketedPaste.cmake`** — shipped in ned 2026-09-06
      (`git log --grep=paste-perf-and-drag-drop`); it is the *upstreaming* that is still
      open, same as the two above. Upstream issue
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

As of 2026-09-08: `ctest -j8` is clean under the `default` preset, and so is the
single-process `./build/ned_tests` (see the build/test note at the end of this file for
why that is a separate check worth making). The `sanitize` preset has one reproducible
failure, below. Two flakes, one resolved entry kept for the lesson, and one documented
behavioral limitation:

- **The `[Performance]` tests flake under `ctest -j8`** (seen repeatedly 2026-09-09:
  `Point navigation across a huge (piece-table-backed) buffer stays fast`). They budget
  wall-clock while eight test processes compete for the machine, so a loaded run can miss a
  budget the same binary clears comfortably on its own. Each one passes standalone and on
  the next parallel run. Worth either gating them behind a serial ctest fixture or moving
  them to a `RUN_SERIAL` property rather than continuing to eyeball each occurrence.

- **`ResolvePsr4Namespace strips a leading fully-qualified backslash` aborted once
  under `ctest -j8`** (seen 2026-09-09, passed on immediate rerun and on a full clean
  re-run of the suite). "Subprocess aborted", not an assertion failure, so it's a crash
  in the test process rather than a wrong answer. Unreproduced since; noted so the next
  sighting is a second data point rather than a first.

- **`UnsavedChangeRanges` can drift by a byte across undo/redo inside a run of
  identical characters.** Same root cause as the `ExcerptRange` bug `multibuffer-gaps`
  fixed: `Buffer::UpdateUnsavedRangesForRestore` relocates across a `ChangedByteRange`
  diff, which is free to report any of several equivalent change positions when the
  edited byte sits inside a run of identical ones. Cosmetic here (a status-gutter mark
  one byte wide, on a line already marked touched), unlike the excerpt case where a
  stale range fed a real write-back — which is why only the latter got the exact
  per-undo-node snapshot treatment. If this is ever worth fixing, that snapshot
  mechanism (`Buffer::SnapshotExcerptRangeOffsets`) is the shape to copy.

- **"Point navigation across a huge (piece-table-backed) buffer stays fast" fails under
  `ctest -j8` on the `sanitize` preset**, reproducibly, and passes on `--rerun-failed`
  in isolation. Confirmed pre-existing by stashing a working branch and re-running
  against a clean tree, so it is not attributable to whatever is in flight. This is the
  perf-budget-under-ASan problem in general: the budgets are calibrated for an optimised
  `NDEBUG` build, and ASan plus seven other test processes competing for cores is a
  different machine entirely. The fix is to gate the `[Performance]` budgets on the
  build being optimised rather than to loosen them — that would give up the regression
  signal the tests exist for. Until then, treat a `[Performance]` failure under
  `sanitize -j8` as noise, and confirm any real perf work against the `default` preset.

- ~~**Two `BufferViewHugeStructuralGutterTest` flakes under `ctest -j8`**~~ — *root-caused
  and fixed 2026-09-10.* The fold-gutter one failed plainly (2026-09-08, twice on
  2026-09-10) and its sibling symbol-gutter one died with SIGBUS (2026-09-09); both passed
  standalone every time, and both were logged here as load-dependent timing with a
  shared-tmpfs theory. Neither was about timing or headroom.
  All three `TEST_CASE`s in that file built their huge buffer from one shared path,
  `/tmp/ned_huge_structural_gutter.c`. ctest runs each case in its own process and several
  at once under `-j8`, so three processes wrote different content to the file the others
  were reading through a live `mmap`. Both symptoms fall straight out of that: a reader
  seeing another case's content fails an assertion, and one whose backing file is replaced
  underneath its mapping takes SIGBUS. "Never reproduces standalone" was the tell, and it
  is exactly what a cross-process race looks like.
  Fixed by giving each case its own filename — the convention every other temp-file test
  here already follows. Five consecutive clean `-j8` runs after, against two failures in
  the six runs before. The general lesson for this list: a test that passes alone and fails
  in parallel is a *shared resource* question first and a timing question second.

One documented behavioral limitation, not a flake:

- A save of a file with more than one hard link writes that file's own inode in place
  (`Text/FilePreservation.h`'s `ShouldWriteInPlace`) rather than taking the usual
  temp-file-then-rename path, because a rename always produces a new inode and would
  leave every other link on the stale content. That trade costs the atomic path's crash
  protection for those files specifically: a crash or a full disk mid-write can leave one
  truncated, recoverable only from the `Editor/Backup.h` version written moments earlier.
  `ned/set-preserve-hard-links-on-save false` opts back into the atomic path (and back
  into breaking the link). Fixing this properly would need a write-then-relink scheme that
  POSIX doesn't really offer; not worth building until someone actually hits it.

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
- **Raw terminal I/O** — the `termios`/`poll` raw-mode work now lives in
  `Editor/Clipboard.h`'s OSC 52 path and `UI/EventLoop.cpp` (the earlier
  `UI/TerminalColorProbe.*` this used to name was removed with `--detect-theme`). Needs
  the Win32 console API, or VT passthrough on a recent-enough Windows Terminal.
- **Notcurses itself** would need to build and run against the Win32 console/Windows
  Terminal target — worth checking Notcurses' own upstream platform support before
  committing, since ned's `UI/` layer sits directly on it with no abstraction gap.
- A PowerShell-flavored bundled theme would be a small addition once the port exists —
  `UI/ThemeRegistry.h`'s fixed name→factory table is exactly the extension point.
- **LSP broker self-staleness detection** (`Editor/Lsp/BrokerMain.cpp`'s executable-
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

- [ ] **Jank replaces Janet** — swapping the scripting layer for
      [jank](https://github.com/jank-lang/jank), a Clojure dialect on LLVM. Full
      measured feasibility record: `Docs/JankFeasibility.md` (investigated 2026-09-08
      against a real local install, not from documentation). Verdict: embedding works
      today and per-form cost after warm-up is negligible, but three things block it.
      **Peak RSS ~237 MB** against ned's current ~28 MB with a file open — a 10x floor
      before a single buffer loads, which sits badly beside the huge-file work done
      specifically to keep memory bounded. **Eval forces the dynamic runtime**, so ned
      would ship a hard dependency on a matching Clang/LLVM (`libclang-cpp.so` 84 MB +
      `libLLVM.so` 182 MB) where Janet is a ~1 MB library with no toolchain dependency.
      And **BDWGC is statically baked into both jank archives**, so every `std::thread`
      in ned needs explicit `GC_register_my_thread` bracketing — about eight lines of
      RAII, but at 39 call sites. Upstream was reportedly easing embedded builds around
      the time this was measured; re-check before investing in any workaround.
- [ ] **ned in Carbon, eventually.** Speculative and deliberately unscoped, but worth
      remembering: ned is C++23 throughout, and Carbon's entire pitch is C++ interop as a
      migration path for large existing C++ codebases — which describes this one. Carbon is
      pre-0.1 (its own roadmap says end of 2026 is the *soonest* 0.1 could ship) and no
      admissible tree-sitter grammar exists — six `tree-sitter-carbon` repos, all zero
      stars, one archived — so the correct action is to watch, not wire. Full entry with
      the admission trigger in `Docs/LanguageCoverage.md` ("Ahead of the catalogue").
      Relevant to the parsing-engine work rather than separate from it: Carbon names *"low
      context-sensitivity"* as an explicit design principle, which is exactly the property
      Tier 0 structural inference relies on, so it is an easy case rather than a hard one.

- [ ] **Configurable indicator glyphs** — the five render-indicator characters
      (`Source/UI/BufferView/Internal.h`) are `constexpr char32_t`: `…` fold ellipsis,
      `»` truncation, `│` indent guide, `↳` wrap continuation, `¬` no-trailing-newline.
      Their *colours* already follow the theme — the wrap glyph paints in
      `indentGuideForeground`, which `ThemeFromPalette` defines as
      `Interpolate(0.5, subtleForeground, background)`, so it recedes correctly on light
      and dark alike (verified live across `light`/`high-contrast-dark`/`mono-light`) —
      but the characters themselves are fixed. Raised when the wrap indicator moved into
      the gutter (2026-09-11); deliberately not done for that one glyph alone, since a
      bespoke `ned/set-wrap-glyph` beside four hardcoded siblings is exactly the
      one-off this codebase avoids. The shape if it happens: one small module in
      `TabWidth.h`'s mutex-guarded-static pattern covering all five, a
      `ned/set-glyph "wrap-continuation" "⤷"`-style binding, and validation that the
      replacement is a single-width codepoint — a double- or zero-width glyph in the
      digits column shifts the whole gutter. A maybe rather than an open item because
      nobody has yet wanted a different character, only a different weight, and the
      theme already provides that.

- [ ] **Split BufferView's prompt/session machine out, with its LSP pickers** — the
      BufferView decomposition (`Docs/BufferViewDecomposition.md`) shrank `BufferView.cpp`
      from 16,823 lines to ~1,700 and `Paint` from 1,597 to 459, but `BufferView.h` is
      still ~4,000 lines and 198 members. Its plan's phase 5 said to fix that by
      extracting `LspFeatures`. That boundary was measured and rejected: 9 of the LSP
      functions *set* `inputMode_` and 17 read it, because most LSP features **are**
      prompts (code-action select, go-to-definition select, peek, document/workspace
      symbols, rename). Extracting them by protocol would bounce callbacks across a
      boundary for what is one interaction, and the sub-clusters that genuinely are
      ambient (hover, document highlight, signature help — 61 of 83 functions never touch
      session state) are ~4 members each and do not justify a class plus a callback
      struct.
      The boundary that would work is by *role* rather than by protocol: lift the
      prompt/session state machine (`inputMode_`, `prompt_`, the four shared drivers, the
      per-session state) into its own type and take the LSP pickers with it, leaving
      BufferView with the widget and its key/mouse entry points. That is a bigger piece
      of design than the plan describes and is the reason this is a maybe rather than an
      open item — it is worth doing only if the header actually starts costing time, not
      merely because it is long.

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
      gap rather than assuming ACP already covers "AI in the editor." Sketched further
      2026-09-07, still unscoped:
      - **The backend is the real fork.** Reusing `Editor/Acp/` costs no new
        infrastructure and already talks to a live agent, but ACP is conversational
        request/response — latency is seconds, which is fine for an explicit
        "predict my next edit" command and wrong for anything that fires as you type. The
        alternative is a dedicated fast path (a small HTTP client, or a local model), which
        gets the latency but adds a subsystem plus a credentials/config story ned doesn't
        have today. Likely order: prove the predictions are worth anything via the cheap
        explicit-command-over-ACP version *before* building latency infrastructure for
        them.
      - **Two pieces already exist.** `Text/LineDiff.h`'s `UnifiedDiff` (built for the ACP
        edit preview) is exactly the recent-edits-as-a-diff prompt input this needs, and
        `UndoTree` supplies the history behind it.
      - **The genuinely new work is rendering.** Nothing draws a multi-line inline
        suggestion — ghost text was removed from completion in favor of `ListPopup`
        (`completion-popup`), so a multi-line ghost overlay is a new `BufferView` paint
        path, not a revival of the old one.
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
      (`Lsp/BrokerMain.cpp`'s `kWholeDaemonIdleTimeout`), specifically so a stale process
      never outlives a `ned` binary rebuild for long: `BrokerRouter` caches one real
      `initialize` handshake result — success *or* failure — per `(root, language)` key
      for its own process lifetime, and a live bug showed this can otherwise strand every
      future attacher on a failure cached from a client-capabilities bug that was already
      fixed and rebuilt. A real "server mode" (deliberately kept warm regardless of
      client presence — e.g. a systemd user service, so a fresh `ned` launch never pays
      even the broker's own startup cost) would disable or greatly lengthen that idle
      timeout, which reopens exactly this staleness risk on a much longer timescale.
      The prerequisite this entry used to list — the daemon noticing its own on-disk
      executable changed and restarting itself — is **already built** (`/proc/self/exe`
      device/inode/mtime, re-checked on the idle sweep; watched firing live 2026-09-07),
      so server mode no longer needs it designed, only kept working. Not scoped further
      than that; no server-mode design exists yet.

Also shipped, one slug each for `git log --grep=`: `broker-reader-deadlock` (that same
daemon deadlocking in its own idle sweep — the bug is why `Tests/LspBrokerDaemonTest.cpp`
and the daemon's ASan coverage exist at all, both of which any server-mode work should
build on), `code-coverage-gutter`.

- [ ] Raw per-file `.gcov` output has no parser — `Editor/Coverage/OutputParser.h`
      handles lcov's `.info` format only (which covers `lcov`, `llvm-cov export
      -format=lcov`, and `gcovr --lcov`). `.gcov` is a directory-scan problem rather than
      the single-document parse every other `Editor/*OutputParser.h` does, so it was left
      out deliberately; revisit only if `.info` proves insufficient in practice.

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
- When you finish an item above, delete it (or replace it with a one-line pointer) in
  the same commit — don't leave a `[x]` writeup behind. Keeping this file short is the
  point.
