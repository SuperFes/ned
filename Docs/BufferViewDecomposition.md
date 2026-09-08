# BufferView Decomposition

A staged plan for breaking `Source/UI/BufferView.{h,cpp}` into a set of focused classes
under a new `ned::ui::bufferview` namespace.

Ground-truthed by measurement against the current tree, not estimated: every line count,
function count, and member count below came from parsing the actual files. Where a claim
is a judgement call rather than a measurement, it says so.

## 1. Current state

| | |
|---|---|
| `BufferView.h` | 4,470 lines — one class, one `public:`, one `private:` |
| `BufferView.cpp` | 16,823 lines — 377 member functions, 785 lines of anonymous-namespace helpers |
| Member variables | **265** |
| `InputMode` enum | **69** states |
| `StartInteractiveSession` | 1,419 lines, **175** `case` labels |
| Tests depending on it | 20 files, 17,358 lines |

### Where the 16.8k lines actually go

Every function classified by name against its cluster; percentages are of classified lines.

| Cluster | Lines | Fns | % | Representative members |
|---|---:|---:|---:|---|
| **D** Prompts / interactive sessions | 4,478 | 64 | 28.2% | `StartInteractiveSession`, `HandlePromptKey`, 40× `Handle*Key`, 15× `Refresh*Status` |
| **E** LSP feature broker | 2,935 | 85 | 18.5% | completion, hover, signature, code actions, definition, symbols, hierarchy, rename |
| **C** Rendering | 2,341 | 18 | 14.7% | `Paint` (1,598 alone), sticky scroll, inline diagnostics, prose callouts |
| **G** DAP integration | 1,085 | 39 | 6.8% | watches, memory, disassembly, pointer graph, hex toggle, thread select |
| **J** Key-dispatch core | 1,014 | 16 | 6.4% | `OnKeyEvent`, `DispatchChordNormally`, `RunCommandAndHandleOutcome`, vim, macro |
| **A** Gutter / derived-data caches | 1,002 | 27 | 6.3% | 14× `Ensure*Cache`, 8× `*GutterActive`, `GutterWidth` |
| **F** VCS integration | 792 | 45 | 5.0% | blame, diff, status, branches, commit, hunk stage/revert |
| **I** Mouse | 659 | 10 | 4.1% | `OnMouseEvent` (383), context menu, gutter clicks, drag |
| **H** Derived-buffer builders | 610 | 11 | 3.8% | results, agenda, clock report, diagnostics, messages, debug |
| **B** Viewport / geometry | 461 | 20 | 2.9% | `topLine_`, wrap, row counts, hidden lines, scroll-to |
| **K** Links | 182 | 3 | 1.1% | `OpenLinkAtPoint` tiers |
| **W** Wiring / `*ForTesting` | 114 | 28 | 0.7% | `Set*` hooks, test shims |

The headline: **prompts and LSP together are 47% of the file.** Rendering is only 15%
despite `Paint()` being the single largest function.

### The three repeated micro-patterns

Most of the 265 members are not 265 distinct ideas. They are three patterns copy-pasted
N times, and collapsing them is the highest value-per-risk work available:

**Pattern 1 — candidate list + selection index (22 instances).**
`acpAgentNameSelection_`, `acpPermissionSelection_`, `bookmarkSelection_`,
`codeActionSelection_`, `contextMenuSelection_`, `dapExceptionFilterSelection_`,
`dapThreadSelection_`, `definitionSelection_`, `documentSymbolSelection_`,
`executeCommandSelection_`, `hierarchySelectedIndex_`, `pathCompletionSelection_`,
`peekDefinitionSelection_`, `pointerGraphSelectedIndex_`, `projectFindFileSelection_`,
`recentFileSelection_`, `recoverChoice_`, `selectThemeSelection_`,
`switchProjectSelection_`, `switchToBufferSelection_`, `vcsSwitchBranchSelection_`,
`workspaceSymbolSelection_`.

Roughly 15 of these come with a `Refresh<X>Status()` / `Handle<X>Key()` pair that is
byte-for-byte identical modulo names. Compare `HandleProjectFindFileKey` and
`HandleFindRecentFileKey`: same Enter branch, same `IsQuit` branch, same
`TryNavigatePromptHistory` branch, same Up/Down modulo-wrap branch, same
`HandlePromptEditingKey` tail. The only real differences are five values — history key,
candidate source, prompt title, cancel message, and the commit action.

**Pattern 2 — generation-stamped async request (19 instances, plus 8 `DeadlineTimer`s).**
`codeActionRequestGeneration_`, `completionRequestGeneration_`,
`definitionRequestGeneration_`, `documentHighlightRequestGeneration_`,
`documentLinkRequestGeneration_`, `documentSymbolRequestGeneration_`,
`hierarchyRequestGeneration_`, `hoverRequestGeneration_`,
`linkedEditingRequestGeneration_`, `lspFormatOnSaveRequestGeneration_`,
`onTypeFormattingRequestGeneration_`, `peekDefinitionRequestGeneration_`,
`pointerGraphRequestGeneration_`, `prepareRenameRequestGeneration_`,
`referencesRequestGeneration_`, `renameRequestGeneration_`,
`signatureHelpRequestGeneration_`, `switchHeaderSourceRequestGeneration_`,
`workspaceSymbolRequestGeneration_`.

Each is "bump on issue, compare on receipt, drop if stale," open-coded every time. The
`LspManager::ExpireStaleRequests` UAF was a bug in exactly this pattern's neighbourhood.

**Pattern 3 — buffer-stamped derived cache (18 buffer stamps + 29 generation stamps).**
`highlightCache*`, `foldGutterCache*`, `foldableBlocksCache*`, `unsavedChangeCache*`,
`diagnosticGutterCache*`, `symbolMarkersCache*`, `symbolGutterCache*`,
`conflictHunkCache*`, `testGutterCache*`, `coverageGutterCache*`,
`inlineDiagnosticCache*`, `blameGutterCache*`, `hiddenLineRangesCache*`,
`rowCountCache*`, `linkCache*`, `embeddedDocumentCache*`, `brushCache*`.

Each is validity stamps (buffer pointer + 1–3 generation counters + sometimes a
byte window) plus a payload, with a hand-written `Ensure*` that re-checks them. The
huge-file windowing correction and the mode-resync stamp-poisoning trap both live in
this pattern, duplicated per cache.

Collapsing these three patterns alone removes roughly **90 of the 265 members** without
moving a single line of business logic between files.

## 2. Target shape

New directory `Source/UI/BufferView/`, new namespace `ned::ui::bufferview`. This mirrors
what `Source/Editor/` already does for its integration subsystems (`ned::editor::lsp`
in `Source/Editor/Lsp/`, `::dap`, `::vcs`, ...). `BufferView` itself stays at
`Source/UI/BufferView.{h,cpp}` in `ned::ui` — it remains the widget, and its public API
does not change.

### Shared seam (built once, passed by reference)

Every extracted class needs the same handful of references. Without a seam, each one
takes 15 constructor parameters.

- **`EditorContext`** — the nine constructor references (`activeBuffer`, `killRing`,
  `registers`, `promptHistory`, `bufferList`, `dispatcher`, `statusMessage`, `mode`,
  `theme`) plus the manager pointers wired by `Set*` (`lspManager`, `dapManager`,
  `vcsRunner`, `taskRunner`, `testRunner`, `acpManager`, `projectUndo`, `eventLoop`,
  `janetEnv`, `leftDock`, `projectSidebar`, `vcsPanel`). Plain aggregate, no behaviour.
- **`ViewServices`** — the small set of `std::function` hooks pointing *back* at the
  view: `endSession`, `scrollToShowPoint`, `scrollToShowOffset`, `reportError`,
  `jumpToPathLine`, `clearBufferCaches`, `makeContext`, `pushPopupModel`. Matches the
  codebase's dominant `Set*`/`std::function` convention and is what makes each
  controller unit-testable with no `BufferView` at all.

Deliberately **not** an abstract interface. Per `CLAUDE.md`'s dispatch rule, the
implementation set here is closed and known at compile time; these are concrete members
of `BufferView`, called through direct or `std::function` dispatch.

### Reusable primitives (kill the three patterns)

| Type | Replaces | Effect |
|---|---|---|
| `CacheStamp` | 18 buffer stamps + 29 generation stamps + the window/width/wrap fields | **−33 members.** One key per cache, built once and used for both the check and the store. |
| `RequestSlot` | 21 generation counters | **No size change** — this is type safety. `Begin()`/`IsStale()`/`Cancel()`/`Current()` instead of a bare `!=` against whichever counter was typed. |
| `CandidateList` | 22 selection indices | **No size change either.** Deferred to Phase 4 — see below. |

The original estimate here was that these three would take ~90 members off the
class. That was wrong, and measurement corrected it: only `CacheStamp` collapses
several members into one. `RequestSlot` and `CandidateList` replace one member
with one member — they buy correctness and de-duplicated arithmetic, not size.
Real reduction from Phase 2 is **265 → 232**.

### Classes

| Class | Absorbs | Est. lines |
|---|---|---:|
| `Viewport` | **B** — `topLine_`/`leftColumn_`, wrap segments, row counts, hidden-line walk, `MaxTopLine`, `ScrollToShow*`, `ByteOffsetForPoint` | ~600 |
| `GutterModel` | **A** — the 14 `Ensure*Cache` methods as `CacheSlot`s, plus `*GutterActive`/`GutterWidth`/column layout | ~1,100 |
| `Renderer` | **C** — `Paint` split into `PaintGutter`/`PaintTextRow`/`PaintOverlays`/`PaintStickyScroll`, brush cache, the render helpers currently in the anonymous namespace | ~2,400 |
| `PromptController` | **D** — owns `inputMode_`, `prompt_`, prompt history index/stash; routes to the active session object | ~700 |
| ├ `FuzzyPromptSession` | ~15 candidate prompts, driven by a descriptor | ~200 + 15×~15 |
| ├ `ConfirmSession` | 7 y/n confirmations | ~120 |
| ├ `TextEntrySession` | 27 plain text-entry prompts (every `HandlePromptKey` case) | ~180 |
| ├ `SingleKeySession` | register / zap-to-char / org-capture / template-key reads | ~120 |
| └ bespoke stages | delete-file, rename-file, set-property, recover-file | ~350 |
| `LspFeatures` | **E** — every LSP request/response/popup path | ~2,500 |
| `VcsFeatures` | **F** — blame/diff/status/branches/commit/hunks | ~800 |
| `DapFeatures` | **G** — watches/memory/disassembly/pointer graph/thread + filter select | ~1,100 |
| `DerivedBuffers` | **H** + **K** — mostly free functions building read-only result buffers | ~800 |
| `MouseController` | **I** — `OnMouseEvent`, drag, gutter clicks, `ContextMenuSession` | ~700 |

**What stays in `BufferView`** (cluster **J** plus the public surface): `Paint`/`OnEvent`
entry points, `OnKeyEvent`, `DispatchChordNormally`, `RunCommandAndHandleOutcome`,
`HandleVimKey`, `HandleConflictQuickKey`, `HandleBulkPastedText`, macro replay,
`CursorPosition`, every `Set*` wiring hook, and the 18 `*ForTesting` shims (which become
thin forwarders). Target: **`BufferView.h` ~400 lines, `BufferView.cpp` ~1,200 lines.**

Sessions that already have their own types elsewhere — `IncrementalSearch`,
`QueryReplace`, `ProjectReplace`, `PrefixArgumentReader`, `SnippetSession`,
`CompletionSession`, `LinkedEditingSession`, `VimEngine` — are not touched. They stay in
`Editor/`; `PromptController` only routes keys to them.

## 3. Phase plan

Each phase is independently buildable, independently shippable, and ends with a full
`ctest` green plus an ASan run of the `BufferView*` tests. No phase depends on a later
one. If a phase turns out wrong, it reverts alone.

### Phase 0 — File split only, no class changes — **done**

Split `BufferView.cpp` into eight translation units along the cluster boundaries above.
Same class, same members, same signatures — pure motion, verified by the linker.

| File | Lines | Defs | Holds |
|---|---:|---:|---|
| `BufferView.cpp` | 1,609 | 57 | **J** — construction, event entry, key dispatch, macro replay, `Set*` wiring |
| `BufferView/Paint.cpp` | 2,337 | 18 | **C** — `Paint` and its row/overlay/sticky-scroll helpers |
| `BufferView/Gutter.cpp` | 1,297 | 47 | **A** + **B** — the `Ensure*Cache` family, gutter layout, viewport geometry |
| `BufferView/Prompts.cpp` | 4,423 | 65 | **D** — the interactive-session state machine |
| `BufferView/Lsp.cpp` | 2,767 | 83 | **E** — the LSP feature broker |
| `BufferView/VcsDap.cpp` | 1,823 | 84 | **F** + **G** — VCS and DAP |
| `BufferView/Buffers.cpp` | 384 | 11 | **H** + **K** — derived-buffer builders, links |
| `BufferView/Mouse.cpp` | 746 | 13 | **I** — mouse, drag, gutter clicks, context menu |
| `BufferView/Internal.h` | 1,590 | — | the file-local helpers, now shared |

All 377 member definitions moved, none lost or duplicated: the set of
`BufferView::<name>` definitions across the eight files is identical to the original's,
and a line-by-line content diff of the split against the original comes back empty apart
from the new per-file banner comments. `BufferView.h` was not touched at all.

**What the split actually ran into.** The original file had **fourteen** anonymous
namespaces, not one — a top block of 785 lines plus thirteen more interleaved between
function definitions. Splitting by cluster stranded most of them: helpers defined inside
one cluster's span but called from another (`SymbolGlyphFor` from both Paint and Lsp,
`DiagnosticSeverityColor` from Paint, `FormatDebugVariableLine` from Prompts and VcsDap,
`kVcsStatusBufferName` from VcsDap, and more). Rather than adjudicate each one, all
fourteen blocks were hoisted wholesale into `BufferView/Internal.h` under a `detail`
namespace, in source order, with each function marked `inline`. Every part then does
`using namespace detail;`, so **no call site changed**.

Three things made that safe, each checked rather than assumed:

- **No mutable namespace-scope state.** Every non-function entity in those blocks is
  `constexpr`/`const` or a type, so nothing became a per-TU copy of something that used
  to be shared. Verified by parsing all fourteen blocks before hoisting.
- **Source order preserved**, which is what keeps the one forward declaration
  (`FormatDebugVariableLine`, declared near `HandlePromptKey` and defined near
  `ShowDebugInfo`) ahead of its definition.
- **`inline` over internal linkage** — identical codegen, and it keeps the helpers out of
  any public surface. This matters for the ones in `Paint`'s hot per-character loop
  (`CodepointColumns`, `IsUnprintableControl`, `SpanAtOffset`, `VisualColumn`): defining
  them out-of-line in a `.cpp` would have cost inlining and put the `[Performance]` tests
  at risk. None of them regressed.

`BufferView/Internal.h` also carries the original's whole 84-line include block, kept
intact so the split moved no includes around and so the eight parts don't duplicate it.
That is scaffolding: as each cluster becomes a real class it takes the includes it needs
with it, and this list shrinks to what the remaining helpers use.

`clang-format` was deliberately **not** run — every moved line is already formatted, and
reformatting whole files would have buried the motion in noise.

Verification: `cmake --build build` clean, `ctest` **3837/3837** (identical to the
pre-split baseline), and the same under the ASan/UBSan `sanitize` preset.

**This phase is the class-boundary preview.** Each later phase converts exactly one of
these files from "BufferView methods that happen to live here" into "a real class,"
which is what gives the traceable regression path: a bug after phase *N* is bounded to
one file's conversion.

### Phase 1 — `EditorContext` — **done**

`BufferView/EditorContext.h` is the aggregate every extracted class will take instead of
a dozen separate arguments: the nine collaborators the view is constructed with as plain
references, and the nine managers wired later through `Set*` as references *to the
pointers*, so a `SetLspManager` long after the struct was built is visible through it
with no second update path. `BufferView` owns one as its last member and `MakeContext()`
builds from it.

Because those manager members bind to `BufferView`'s own siblings, the view must not be
moved; it was already non-copyable with no move declared, and a `static_assert` in
`BufferView.h` now keeps it that way.

Two deliberate departures from the plan as written:

- **The constructor was left alone.** Taking an `EditorContext&` instead of nine
  arguments was explicitly on the table, tests included. It was measured and rejected:
  construction is already centralised behind a per-file `Fixture::View()` factory in the
  test suite, so the nine arguments cost little, and a caller-built context would still
  have to name the same nine collaborators — the churn moves rather than disappears, and
  callers pick up a lifetime obligation they do not have today.
- **`ViewServices` was not written.** The set of callbacks the parts need back into the
  view (`endSession`, `scrollToShowPoint`, `reportError`, ...) is guesswork until a real
  consumer exists. It lands in Phase 3 alongside `GutterModel`, whose needs define its
  shape; writing it now would be a dozen speculative `std::function`s with no caller.

### Phase 2 — The primitives, migrated in place — **done**

`CacheStamp` and `RequestSlot`, each with its own unit tests, migrated onto every call
site without moving any code out of `BufferView`. 265 members → 232; suite 3837 → 3852
(the 15 new primitive tests), clean under ASan/UBSan.

Consolidating the fifteen hand-written cache checks turned up two things worth naming:

- **A latent staleness bug, now fixed.** The ineligible paths in
  `EnsureFoldableBlocksCache`/`EnsureSymbolMarkersCache`/`EnsureTestGutterCache` marked
  the emptied cache current for buffer+generation only, leaving the window fields stale.
  Turning the fold gutter (or symbol/test discovery) off and back on with no edit and an
  unmoved viewport could then satisfy the *eligible* guard and leave the gutter reading an
  empty cache. Those paths now stamp a strictly narrower key than the eligible guard
  compares, so the two can never match and it rebuilds. The fix falls out of the primitive
  rather than being bolted on.
- **A duplicated store.** `EnsureSymbolGutterCache` wrote its four cache fields twice in a
  row, verbatim. Idempotent, so nothing rode on it; collapsed to one.

`RequestSlot`'s counter starts at 1 rather than 0, so no token it hands out can collide
with a default-initialised `Token{}` — found by a unit test asserting the opposite, and
worth keeping because one site (completion resolve) legitimately reads `Current()` before
anything has been issued.

**`CandidateList` deliberately deferred to Phase 4.** The 22 selection indices are not a
single pattern: ~15 belong to fuzzy candidate prompts that Phase 4 collapses wholesale
into `FuzzyPromptSession`, and migrating them to an interim type first means doing that
work twice. The remainder (code actions, definitions, DAP threads, context menu, ...)
pair with differently-typed vectors and keep their own index either way. The repeated
wrap-around and clamp arithmetic is real and worth removing — in Phase 4, once, where the
sessions that own it are being written anyway.

### Phase 3 — `GutterModel` and `Viewport`

The first real extractions, and where `ViewServices` lands: `GutterModel` is the first
class that needs to call back into the view, so its needs decide that struct's shape. Both are pure computation over `ITextStorage` + fold state +
mode + width; both are trivially unit-testable headlessly. `Renderer` will consume them,
so they come first.

### Phase 4 — `PromptController` and the session types

The biggest maintenance win. Order within the phase:

1. `CandidateList` (candidates + selection + wrap-around navigation + popup model),
   deferred here from Phase 2 so the ~15 fuzzy prompts get it once rather than twice.
2. `FuzzyPromptSession` + descriptors — converts ~15 prompts, ~1,200 lines to ~400.
3. `ConfirmSession` — 7 confirmations.
4. `TextEntrySession` — the 27 `HandlePromptKey` cases.
5. `SingleKeySession` — register/zap/capture.
6. `PromptController` takes ownership of `inputMode_` + `prompt_`; the 175-case
   `StartInteractiveSession` switch splits into three tables: session starts,
   one-shot forwards to a `std::function`, and one-shot direct actions.

### Phase 5 — `LspFeatures`, `VcsFeatures`, `DapFeatures`, `DerivedBuffers`

Independent of each other; can land in any order or in parallel. `DerivedBuffers` first
is the cheapest — most of it is already free-function-shaped.

### Phase 6 — `Renderer`

Last of the extractions and the riskiest, because `Paint()` reads a great deal of
`BufferView` state implicitly. Prerequisite: a `FrameState` struct that names every input
`Paint()` currently reaches for. Doing this after phases 2–5 means most of those inputs
are already owned by a named class rather than a loose member.

### Phase 7 — `MouseController` and final cleanup

Extract mouse handling and `ContextMenuSession`, then trim `BufferView.h` to its final
shape and update this document with what the split actually cost.

## 4. Risks and constraints

- **Test surface.** 17,358 lines across 20 test files depend on the current public API,
  including 18 `*ForTesting` shims. Those shims are the contract: they stay on
  `BufferView` as thin forwarders into the new controllers. No test file should need to
  change in phases 0–3. Phases 4–6 may need test edits; if a phase requires broad test
  rewriting, that is a signal the boundary is wrong, not that the tests are.
- **`statusMessage_` is a shared output channel.** It is a `std::string&` into the
  echo area, written from essentially every cluster. It goes in `EditorContext`, and no
  attempt is made to formalise it further in this project — that is a separate change.
- **Callback lifetime does not improve on its own.** Controllers become members of
  `BufferView`, so `this`-capturing lambdas handed to `LspManager`/`DapManager`/
  `VcsRunner` are exactly as safe (or unsafe) as today — no worse, but the
  `ExpireStaleRequests` UAF class of bug is not fixed by this refactor. `RequestSlot` is
  where a real fix would go later, if wanted.
- **No new virtual interfaces.** Per `CLAUDE.md`, the implementation set here is closed
  and compile-time-known. Controllers are concrete members; polymorphism, where needed,
  is `std::function`. The one place a variant is warranted is the prompt session type
  inside `PromptController`.
- **No big-bang.** Phase 0 alone is worth shipping. Every phase after it is optional and
  reversible.

## 5. Expected outcome

| | Before | After (target) |
|---|---:|---:|
| `BufferView.h` | 4,470 | ~400 |
| `BufferView.cpp` | 16,823 | ~1,200 |
| Member variables | 265 (232 after Phase 2) | ~40 |
| Largest function | 1,598 (`Paint`) | ~250 |
| Files | 2 | ~24 |

Total line count will not drop by much beyond the prompt-pattern collapse (~800 lines)
and the cache/request-pattern collapse; the work is about locality and testability, not
volume. The measurable win is that a change to, say, DAP memory inspection touches one
1,100-line file with a named owner instead of a 16,823-line one shared with 376 other
functions.
