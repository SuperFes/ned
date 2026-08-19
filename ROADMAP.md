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

**A real, actively-maintained tree-sitter grammar already exists** — verified via the
GitHub API, not assumed: `nvim-orgmode/tree-sitter-org` (part of the real, popular
`nvim-orgmode` Neovim project, not a one-off), tagged releases through `v1.3.2`,
pushed as recently as 2026-05, ships a pre-generated `src/parser.c`/`src/scanner.c`
(same "no tree-sitter-CLI-at-build-time" convention every other bundled grammar here
already follows) and a real `queries/highlights.scm`. Its `grammar.js` node coverage
was checked directly and already includes `headline`/`stars`, `tag_list`,
`property_drawer`/`drawer`, `table`/`row`/`cell`, `checkbox`, `timestamp`/`date`/`plan`
(Org's `SCHEDULED`/`DEADLINE` lines), `block` (`#+BEGIN_SRC`/etc.), `list`/`item`,
`latex_env`, `directive` (`#+TITLE`/etc.), `formula`, and `fndef` (footnotes) — real
coverage for everything in the v1 scope below. (An older, more-starred
`emiasims/tree-sitter-org` also exists but is archived/unmaintained since 2024 —
`nvim-orgmode`'s is the one to actually use.) Given this, hand-writing an Org grammar
from scratch is very unlikely to be needed — the user's own freshly-installed
tree-sitter CLI is still worth keeping around for local grammar testing or a small
patch if a real gap turns up, just not for a from-scratch build. Recommend prototyping
against it via the dynamic-grammar-loading mechanism first (`ned/register-language-grammar`,
no rebuild needed to iterate), promoting it to a real bundled grammar in `CMakeLists.txt`
only once Org-mode support is a committed, real feature rather than still being designed
— the same "prove it, then commit" discipline this project already applies elsewhere.

### Org: headline/outline model (first slice) — done

The first real slice of work, started once the Phase 8 "Must" items above all landed.
Headline structure is the one piece every other v1 item below ultimately keys off
(fold/unfold, tag display, an eventual agenda-shaped view), so it came first, ahead of
checkboxes/tables/links/highlighting — none of which need it, but all of which are more
useful once it exists, and none of which are started yet.

New `Source/Editor/Org.h/.cpp` (`ned::editor::org` namespace): pure functions over plain
buffer text, the same "UI-agnostic, string_view/struct in and out" shape `ProjectTree.h`/
`ProjectSearch.h` already establish, and specifically *not* over `Buffer&` — nothing here
needs live point/mark/undo, only the text, matching `Mode.h`'s `HighlightFunction` shape
for the same reason. `ParseOutline(bufferText, todoKeywords = DefaultTodoKeywords())`
scans every line for real Org's own headline rule — one or more `*` at column 0 (no
leading whitespace — that's what actually distinguishes a headline from an indented list
item), followed by a mandatory space — and, once a headline is found, peels off an
optional leading TODO keyword (checked against a configurable list, defaulting to
`{"TODO", "DONE"}`; `"TODOING"` is never misread as keyword `"TODO"` + title `"ING"`,
since a keyword match requires a following space or end-of-line), an optional `[#A]`-style
priority cookie, and an optional trailing `:tag1:tag2:` block — parsed via one
`std::regex` (`^(.*?)\s*((?::[A-Za-z0-9_@#%]+)+:)\s*$`, ECMAScript syntax, matching this
codebase's existing `QueryReplace`/`ProjectSearch` convention) rather than hand-rolled
backward scanning, since the lazy `.*?` combined with anchoring both ends is what
guarantees a stray `"Note: something"`-style colon in the middle of a title is never
misread as the start of a tags block (a real case this file's own tests cover).
`NextTodoKeyword(current, todoKeywords)`/`NextPriority(current)` are the two pure
state-transition helpers ("" -> `"TODO"` -> `"DONE"` -> "", and `nullopt` -> `'A'` ->
`'B'` -> `'C'` -> `nullopt`); an unrecognized/stale current value is treated the same as
"none" and restarts the cycle rather than silently no-op'ing, since a headline with a
keyword no longer in `todoKeywords` (e.g. after a user reconfigures it) shouldn't get
permanently stuck.

**What's deliberately not here yet, and why:** any `Buffer`-mutating operation (actually
rewriting a headline's TODO keyword/priority in place, wired to real editing commands)
— this slice is the structural model only, proven with 19 new `Tests/OrgTest.cpp` cases
(585 total, clean) but not yet reachable from a keybinding. Checkboxes, tables, and links
are separate, independent follow-up slices, not started. **A real open design question,
flagged rather than guessed at:** where per-buffer fold state (which subtrees are
collapsed) should live once fold/unfold lands. `Buffer` already carries some non-text
state (`Point_`/`Mark_`/`narrowedRange_`), which argues for keeping fold state there too
(a navigation/view concern, but tied to *this specific buffer's* content) — but a naive
`ProjectSidebar`-style `set<Buffer*>`-keyed cache owned by `BufferView` would repeat a bug
class this codebase has already hit and fixed twice (dangling-buffer-pointer bugs in
window-splitting's `ActiveBuffer` retargeting, and the reason `RegisterTable`'s point
registers resolve a buffer *by name* via `BufferList::Find` rather than holding a raw
`Buffer*`). Left for the slice that actually implements fold/unfold rather than decided
speculatively here.

### Org: TODO/priority cycling, checkboxes, and a real org-mode — done

Second slice, landed the same session. Extends `Org.h/.cpp` with a Buffer-mutating layer
on top of the pure structural model above, and wires it into a real, keybindable
`Mode` for the first time.

**Checkboxes** (new to this slice, not just the headline model's own follow-up): a
`Checkbox{indent, state, text, lineNumber, stateByte}` struct, `-`/`+` bullets only (not
Org's numbered-list checkboxes — matches the ROADMAP's own stated v1 syntax), nesting
inferred purely from indent depth at use time, never stored as a real tree — the same
"flat list, depth is a field" shape `ProjectTreeEntry` already establishes for the
unrelated project file tree. `ReflectParentCheckboxStates` recomputes every checkbox
with children from its *direct* children only (processed bottom-up, in reverse file
order, so a multi-level chain propagates correctly): all checked -> `'X'`, all
unchecked -> `' '`, anything else -> `'-'` (partial).

**The Buffer-mutating layer**: `HeadlineAtPoint`/`SetHeadlineTodoKeyword`/
`SetHeadlinePriority`/`CycleTodoKeywordAtPoint`/`CyclePriorityAtPoint`/
`ToggleCheckboxAtPoint` all edit an already-parsed `Headline`/`Checkbox`'s own byte
range in place via `Buffer`'s existing public `DeleteRange`/`InsertAt` — `Buffer` gains
zero Org-specific knowledge, the same "no new text-manipulation primitives" precedent
`Rectangle.h`/`ProjectReplace.h` already set. A shared `ReplaceOptionalToken` helper
handles the one genuinely fiddly bit both the TODO-keyword and priority-cookie mutators
need: a present token is always followed by exactly one separating space *unless* it
runs to the line's own end (nothing after it) — inserting a brand-new token always adds
its own trailing space, removing an existing one also consumes that trailing space if
one exists, so title/tag text already on the line never ends up with a stray leading
space either way. `SetTodoKeywords`/`TodoKeywords()` (`Org.h`) is a new process-wide,
mutex-guarded setting mirroring `TabWidth.h`'s exact pattern, defaulting to
`DefaultTodoKeywords()` — deliberately kept separate from `DefaultTodoKeywords()` itself
so the pure parsing functions' own default argument stays decoupled from any global
mutable state. **No `ned/set-org-todo-keywords` Janet binding yet** — `Value.h` has no
`std::vector<std::string>` marshalling to build one on top of, a real (if mechanical)
piece of follow-up work, not attempted here; same "hardcoded C++ for now" scope cut this
codebase has made repeatedly (the page-scroll fraction, initial `Theme` selection).

**`Mode::OrgMode()`** (`Mode.h/.cpp`) is the first `Mode` in this codebase to actually
construct a *non-empty* `Keymap` — every other `*Mode()` factory still builds a plain
`Keymap()`. Binds `org-cycle-todo` to real Org's own `C-c C-t`, `org-toggle-checkbox` to
`C-c C-c` (matching real Org's context-sensitive `C-c C-c`), and `org-cycle-priority` to
`C-c C-p`. `C-c C-p` deliberately **shadows** the global `toggle-project-sidebar` binding while an org-mode buffer is
active — confirmed intentional, not a bug: `KeymapStack` was built from Phase 2 onward
specifically so a mode layer can override the global layer per buffer (real Emacs major
modes do this constantly, e.g. `C-c C-c` means something different in every major mode),
this is simply the first `Mode` to actually exercise that with a real conflicting
binding rather than only adding bindings the global map never had. Manually verified via
a `screen`-based pty smoke test that `toggle-project-sidebar` is completely unaffected
in a non-org buffer. `main.cpp`'s `ModeForPath` gained `.org` -> `OrgMode()`.

**The three new commands** (`Commands.cpp`) act directly on `context.buffer`, *not*
through `InteractiveRequest` the way rectangle/register/narrowing commands do — a
deliberate difference, not an inconsistency: those need state that only lives on
`BufferView` (`RectangleClipboard`, `RegisterTable`, or narrowing's own post-edit
viewport scroll), while `org::Cycle*AtPoint`/`ToggleCheckboxAtPoint` need nothing beyond
the buffer itself, so routing them through an interactive session would add a layer of
indirection for no reason — same direct "do the work, report through `context.message`"
shape `save-buffer` already uses. Each reports `"Not on a headline."`/`"Not on a
checkbox."` via `context.message` when point isn't on the relevant kind of line.

Verification: 32 new test cases (`Tests/OrgTest.cpp`'s checkbox/mutation coverage, plus
4 new `Tests/CommandsTest.cpp` cases exercising the registered commands and `OrgMode`'s
own keymap), 617 total, clean — plus a `screen`-based pty smoke test against the real
binary: opened a real `.org` file, confirmed the mode line reads `(org-mode)`, drove
`C-c C-t`/`C-c C-p`/`C-n` + `C-c C-c` to cycle a headline's TODO keyword and priority and
toggle a checkbox, all landing exactly as expected in the actual rendered buffer content
— and confirmed `C-c C-p` still opens the project sidebar normally in a plain-text
buffer, proving the shadowing is genuinely scoped to org-mode buffers only.

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

### Org: subtree fold/unfold — done

Third slice, resolving the fold-state-ownership design question the first two slices
left open, per the user's own explicit direction: asked whether to ship a simplified
binary fold now and extend to Org's real 3-state cycle later, the user rejected that
framing outright ("no need to half-ass something just to whole-ass it later... consider
only [total work]"), so this landed the real thing directly — `Collapsed ->
ChildrenVisible -> Expanded`, matching real Org's own TAB behavior, not a placeholder.

**Where fold state lives, finally answering the open question**: generalizing `Buffer`'s
existing `Point_`/`Mark_`/`NarrowedRange_` precedent rather than a `BufferView`-owned
`set<Buffer*>` cache (which would have repeated the dangling-buffer-pointer bug class
this codebase already hit and fixed twice — see the first slice's own note above).
`Buffer::FoldMarker` (`Text/Buffer.h`) is a 2-value enum (`Collapsed`,
`ChildrenVisible`) plus a `std::map<std::size_t, FoldMarker> FoldMarkers_` — fully
Org-agnostic, `Buffer` has no idea these represent headline folds, the same "generic
position bookkeeping" role `Mark_` already plays. Same session, before adding this: the
user separately flagged that `Point_`/`Mark_`/`NarrowedRange_` already had three
hand-duplicated copies of the same edit-relocation shift logic across
`InsertAtPoint`/`DeleteBackwardAtPoint`/`DeleteForwardAtPoint`/`DeleteRange`/`InsertAt`
(inconsistently — the two `*AtPoint` delete methods never adjusted `NarrowedRange_` at
all, a real latent gap, fixed here as a side effect), and asked for the general
mechanism to be fixed now rather than `FoldMarkers_` becoming a fourth ad-hoc copy —
`Buffer::RelocateForInsert`/`RelocateForDelete` (two private static helpers) are what
all four now route through. Justified by `ROADMAP.md`'s own Phase 9 wishlist already
naming more features that will need the identical primitive later (multi-cursors, git
diff-gutter hunks, LSP diagnostic markers) — not speculative, the same duplicated shape
was already about to be written a fourth time regardless.

**`Source/Editor/Org.h/.cpp` additions**: `BuildHeadlineTree` turns `ParseOutline`'s
flat, level-tagged list into a real parent/child tree (a straightforward recursive
partition-by-level build, chosen over a pointer-juggling single pass specifically to
keep correctness obvious rather than cleverly minimal — an O(n²) worst case against a
pathologically monotonic-level chain was accepted as irrelevant, outline depth is
human-typed and inherently bounded, unlike buffer size). `SubtreeEndLine` finds where a
headline's subtree ends (next headline at `level <=` its own, or EOF).
`FoldedLineRanges(const Buffer&, ...)` walks the tree once, purely node-local (no
propagated "forced" flags): a `Collapsed` node hides `[its own line + 1, subtree end)`
and simply isn't recursed into — descendants' own markers are never even consulted while
hidden, left exactly where they are for whenever an ancestor reopens; a `ChildrenVisible`
node hides only its own leading body text and recurses per each child's *own* marker.
Empty-fast-paths on `buffer.FoldMarkers().empty()` without even re-parsing the outline.
`CycleFoldAtPoint` is the actual TAB command body encoding the 3-state transition's real
*appearance* at each step (the render function alone can't produce it): `Collapsed ->
ChildrenVisible` force-collapses every **direct** child (deterministic "list of child
headlines, bodies closed" look, rather than trying to preserve nuanced prior interior
state across a full collapse/reopen); `ChildrenVisible -> Expanded` recursively clears
every descendant's marker, a real full-subtree expand. **A planned point-safety clamp
turned out to be dead code, caught during implementation, not shipped**: the plan called
for snapping point back to the headline's own line if a fold hid the line point was on —
but `HeadlineAtPoint` already requires point to sit exactly on the headline being cycled,
and every transition only ever hides lines strictly *after* that line, so point can never
end up hidden by its own headline's fold. Removed rather than left in as inert defensive
code once this was noticed, with a test confirming the reachability argument itself
rather than just asserting the absence of a bug.

**`Source/UI/BufferView.h/.cpp`**: the larger, unavoidable part of this slice — nothing
in `BufferView` had ever needed to treat "buffer line" and "rendered row" as different
things before folding existed; `Paint()`'s row loop, `CursorPosition()`,
`ScrollToShowPoint()`, `TopLine()`/`SetTopLine()`/`MaxTopLine()`, and
`ByteOffsetForPoint()` (mouse click translation) all used to reason in raw,
flat `topLine_ + row`-shaped buffer-line arithmetic. Five new fold-aware primitives
(`IsLineHidden`/`NextVisibleLine`/`AdvanceVisibleLines`/`VisibleLineCountBetween`/
`EnsureHiddenLineRangesCache`) are the one shared vocabulary all of the above now goes
through — `EnsureHiddenLineRangesCache` mirrors `highlightCacheBuffer_`'s own
generation-gated caching shape exactly (recomputes only when the buffer pointer, its
`ContentGeneration()`, or a new `Buffer::FoldGeneration()` — a plain counter bumped by
`SetFoldMarker`, mirroring `ContentGeneration()`'s own pattern — have changed), plus the
same empty-fast-path `FoldedLineRanges` itself has, so every buffer that's never had a
fold touched pays zero extra cost per frame, not just an amortized-cheap one. A folded
headline's own line gets a short `…` indicator painted after its content (reusing
`theme.lineNumberForeground` rather than a new `Theme` field — deliberately minimal).

Verification: 646 total test cases (up from 625), clean — new coverage across
`Tests/BufferTest.cpp` (`FoldMarkers_` relocation across every mutator, clamping on
undo/redo, plus the incidentally-fixed `NarrowedRange_` gap), `Tests/OrgTest.cpp`
(`BuildHeadlineTree`/`SubtreeEndLine`/`FoldedLineRanges`/`CycleFoldAtPoint`'s full
3-step transition), `Tests/CommandsTest.cpp` (`org-cycle` + its `TAB` binding), and
`Tests/BufferViewTest.cpp` (`Paint()` skipping hidden lines and rendering the indicator,
fold-aware `CursorPosition()`, fold-aware mouse click) — plus a `screen`-based pty smoke
test against the real binary on a real nested `.org` file: TAB cycled a top-level
headline through all three states with the rendered content exactly matching
expectations at each step (verified against `screen -X hardcopy`, whose own dump
mangles non-ASCII glyphs regardless of source — confirmed via a plain `printf` control
test in the same tool before trusting the ellipsis indicator's presence rather than its
exact glyph), and a plain non-`.org` buffer's `TAB`/rendering/scrolling confirmed
completely unaffected.

**What's still not here:** tables, links, and real tree-sitter-org highlighting.

### Org: tags editing — done

Fourth slice, landed the same session as fold/unfold. `Headline.tags` was already parsed
(`ParseOutline`) since the first slice, but nothing could ever write it back — this adds
the interactive side, real Org's own `C-c C-q` (`org-set-tags-command`).

**Why a prompt, not a cycling command like TODO/priority**: tags are open-ended user
text, not a small fixed set — there's no "next tag" to compute the way
`NextTodoKeyword`/`NextPriority` do. `org-set-tags` (`Commands.cpp`) checks
`HeadlineAtPoint` synchronously and reports `"Not on a headline."` directly on failure
(matching `org-cycle`'s own shape), but on success just sets
`InteractiveRequest::SetHeadlineTags` rather than mutating anything itself — a new
`BufferView::InputMode::SetHeadlineTags` + `HandlePromptKey` branch is what actually
drives the prompt, joining the current tags with `:` to pre-fill it
(`StartInteractiveSession`'s own case re-resolves `HeadlineAtPoint` fresh rather than
threading anything new through `CommandContext`, since point can't move between a
command's dispatch and `StartInteractiveSession` running) and splitting the submitted
text back into a `vector<string>` on Enter (discarding empty tokens, so leading/
trailing/doubled colons the user might type are forgiving, the same tolerance the
buffer's own on-disk tags-block parsing already has via `SplitTagBlock`).

**`Headline` gained a `tagsStartByte` field** (`Org.h`) — where the tags block, plus its
own leading separator and any trailing whitespace after it, begins; equal to
`lineEndByte` when there's no tags block at all, so `SetHeadlineTags`'s delete-then-
insert logic is identical either way (an empty `[tagsStartByte, lineEndByte)` range is
just a no-op delete). Computed inside `ParseHeadlineLine` from the already-matched regex
group's own `.position()`/`.length()` against `rest`'s offset within the line (`rest`
stays a view into the same underlying buffer throughout, `remove_prefix` only ever moves
its start pointer forward, so this works regardless of how much keyword/priority parsing
already consumed) — line-relative there, shifted to absolute by `ParseOutline` the same
way `lineStartByte`/`lineEndByte` already are. `SetHeadlineTags` itself is a plain
delete-then-insert, deliberately not routed through the existing `ReplaceOptionalToken`
helper (`SetHeadlineTodoKeyword`/`SetHeadlinePriority`'s own shared helper) — that
helper's "exactly one separating space, added/removed symmetrically" rule is built for a
fixed-position token right after the stars; `tagsStartByte` already covers the tags
block's own separator and trailing whitespace, so a plain delete-then-insert is both
correct and simpler here.

Verification: 654 total test cases (up from 646), clean — `Tests/OrgTest.cpp`
(`SetHeadlineTags` add/replace/remove, and preserving an existing TODO keyword/priority
cookie on the same line), `Tests/CommandsTest.cpp` (`org-set-tags` setting
`interactiveRequest` + its `C-c C-q` binding), and `Tests/BufferViewTest.cpp` (the full
prompt flow driven via M-x — mode-specific commands aren't in the fixture's own global
keymap, so this reuses the same "M-x chains straight into the command's own prompt"
mechanism `find-file`'s own M-x test already established, rather than needing a bespoke
Org-layered `KeymapStack` just for this) — plus a `screen`-based pty smoke test against
the real binary: `C-c C-q` on a real headline correctly pre-filled the prompt with its
existing tags, and submitting a new colon-separated list correctly rewrote the tags
block in place, leaving the title/TODO/priority/body untouched.

### Org + Markdown: table parsing and column alignment — done

Fifth slice. The user asked for tables next, then — before any Org-specific code got
written — asked to generalize it to also work for `.md` files, since GFM tables need
the identical column-width/alignment engine even though their exact separator-row
syntax differs from Org's. Landed as a new, explicitly shared toolkit rather than an
Org-only implementation, per this session's own established precedent (the
`Buffer::RelocateForInsert`/`RelocateForDelete` consolidation during the fold/unfold
slice): a concrete second consumer was named directly by the user, not speculative, so
the shared version was built now rather than an Org-only one now and a
duplicate-with-drift later.

**New `Source/Editor/Table.h/.cpp`** (`ned::editor::table` namespace) — a small, format-
agnostic toolkit, not a generic templated engine (matches this codebase's own
established preference for small reusable helpers, e.g. `Org.cpp`'s
`ReplaceOptionalToken`, over a framework): `SplitRow` (tolerates both `"| a | b |"` and
`"a | b"` — real Org and real GFM both allow edge pipes to be optional), `PadCell`,
`ComputeColumnWidths` (codepoint-count width, not grapheme/East-Asian-Wide-aware — a
stated v1 simplification), `CellByteSpans` (byte positions, not text, for point-tracking
— was originally written Org-specific, promoted here once Markdown needed the identical
logic), and `FindTableBlockLines` — the first *multi-line block* detection this codebase
parses (every existing `Org.h` scanner, headlines and checkboxes alike, is a single-line
pattern).

**`Org.h/.cpp`** gained `OrgTable`/`FindOrgTableAtPoint`/`AlignOrgTableAtPoint`. Org's
own separator rows carry no per-column alignment marker (every column aligns `Left` in
this v1 — real Org's content-type auto-alignment, e.g. numbers right-aligned, isn't
reproduced, the same "curated v1 subset" cut this file already made for priorities
capping at A-C) and can appear anywhere, zero or more times (handled via a parallel
`isSeparatorRow` vector, not a fixed position) — a data row that happens to consist
entirely of `-`/`+`/`|`/whitespace is indistinguishable from a real separator row by
this same rule, an ambiguity inherent to Org's own table syntax, not a gap specific to
this parser. **A real, live-terminal-confirmed bug was caught and fixed here**: the
first implementation rendered a realigned separator row starting with `+`
(`"+-------+-----+"`) — cosmetically plausible, and every width/alignment unit test
still passed, since none of them re-fed already-aligned output back through
`AlignOrgTableAtPoint` a second time — but `table::FindTableBlockLines`'s own detection
rule only recognizes lines starting with `|`, so a *second* align pass on the same table
would silently see only a truncated, wrong block. Found by a wrap-around test
(`TAB` past the last cell back to the first) that specifically exercises a second
alignment pass, not by inspection; fixed by matching real Org's own actual convention
(leading/trailing `|`, `+` only at internal column intersections), which was the right
fix on its own correctness merits, not just a workaround for the detection rule.

**New `Source/Editor/Markdown.h/.cpp`** (`ned::editor::markdown` namespace) — this
codebase's first Markdown-specific Editor-layer file, deliberately scoped to *only*
table support, not a general Markdown-editing grab-bag. GFM's delimiter row is
mandatory and always the block's second line; `FindTableAtPoint` validates every one of
its cells (`:`-optional dashes) before treating a `|`-prefixed block as a real table at
all — a block that merely starts with pipes but has no valid delimiter row returns
`nullopt`, not a best-effort guess. `:---`/`---:`/`:---:`/`---` set each column's real
`Left`/`Right`/`Center`/`Default` alignment (unlike Org, genuinely used when padding).

**Interactive surface, no new `BufferView`/`InteractiveRequest` plumbing needed**:
realign-and-advance-to-next-field is fully computable from `Buffer`+`Point()` alone, so
this is a plain direct-action command, the same shape `org-cycle-todo`/
`org-toggle-checkbox` already use. `org-cycle`'s existing body gained a second branch —
`AlignOrgTableAtPoint` as a fallback once `CycleFoldAtPoint` reports point isn't on a
headline — rather than a new competing binding, mirroring real Org's own architecture
directly (`org-cycle`/TAB *is* a single, context-dispatching function in real Emacs Org).
`org-table-align` stays registered separately too (M-x / explicit binding / Janet
scripting). `markdown-table-align` is bound to `TAB` in `MarkdownMode()`'s keymap —
previously always empty, confirmed via exploration before writing any code; this makes
it the **second** `Mode` in this codebase to ever construct a non-empty `Keymap`
(`OrgMode()` was the first). Advancing wraps from the table's last cell back to its
first (Org: first data cell; Markdown: the header's own first cell) — auto-inserting a
new row when tabbing past the last cell of the last row (real Org's own behavior too) is
explicitly deferred, not attempted: a real, separate piece of buffer mutation (inserting
a whole new line, not just realigning existing ones), not silently dropped.

**Mid-slice, the user stated a standing principle** ("any feature available to any
mode... should be available to all modes, perhaps sometimes manually invoked, certainly
configurable"), prompted by this very feature spanning two modes. Checked against the
actual architecture rather than assumed: `CommandRegistry` is already one global
namespace, `Mode` only gates *default keybindings* (confirmed by reading
`Dispatcher`/`KeymapStack`), and none of `org::`/`markdown::`'s own functions gate on
`mode_.name` — so "manually invoked, available to all modes" already holds for every
command in this codebase by construction, this one included. **A real, separate gap was
found and left unfixed on the user's own instruction**: `ned/define-key`
(`Source/Janet/EditorBindings.cpp`) only binds into the single global `scriptKeymap`
layer — there is no way today to Janet-configure a binding scoped to one specific mode.
Flagged as a follow-up rather than actioned mid-feature; see the companion-tooling-
adjacent open item below.

Verification: 691 total test cases (up from 654), clean, including under
`-DNED_ENABLE_SANITIZERS=ON` — new `Tests/TableTest.cpp` (the shared toolkit, including
`FindTableBlockLines` boundary cases), `Tests/OrgTest.cpp` (multi-hline tables, the
wrap-around case that caught the separator-rendering bug above), new
`Tests/MarkdownTest.cpp` (delimiter-row validation and rejection, all three alignment
markers), and `Tests/CommandsTest.cpp` (`org-cycle`'s new table fallback branch,
`org-table-align`, `markdown-table-align`, `MarkdownMode`'s new `TAB` binding) — plus a
`screen`-based pty smoke test against the real binary: a real, deliberately-misaligned
`.org` table realigned correctly on `TAB`, and repeated `TAB` presses walked through
all 9 real cells and wrapped back to the exact starting cell after a full lap
(period-9, confirmed by column position in the live terminal, not just unit tests); a
real `.md` table with `:---`/`---:`/`:---:` markers rendered left/right/center-aligned
exactly as specified, and the mode line correctly read `(markdown-mode)` throughout.

**Follow-up, not started**: per-mode Janet keybinding configuration (the `ned/define-key`
gap above) — worth its own slice once picked up, not folded into this one.

### Links: generic cross-mode follow-at-point, plus Org's own descriptive links — done

Sixth slice. Closes both the Org v1-must checklist's last open line and the "universal
clickable in-buffer affordances" wishlist item recorded during the second Org slice
(above) — the user's own framing this time named the split directly: link-following
should work in *every* mode, but Org's own `[[target][description]]` bracket syntax is
"local and magical" to Org specifically. Asked to disambiguate "magical" before sizing a
plan: confirmed it means real Org's actual **descriptive-link rendering** — the raw
bracket markup visually collapses down to just its description, revealing the full raw
text only while point sits inside it — not merely parse-and-follow with the brackets
always left visible.

**Generic layer, new `Source/Editor/Link.h/.cpp`** (`ned::editor::link` namespace,
deliberately its own file — mirrors the `Table.h`/`Org.h` split from the Tables slice,
except here `link::` never needs to know Org exists at all, even more cleanly separated
than the table toolkit was): `DetectLinkAtPoint` scans only the current line for a bare
`https?://` URL (trimmed of one trailing punctuation character so a sentence-ending
period isn't swallowed) or, failing that, the whitespace-delimited token under point —
classified as a file-link candidate only if it *looks* path-shaped (contains `/`, or has
a plausible extension) — a bare word like `TODO` is never treated as one, since a real
`TODO` file at a project's root would otherwise make the plain word resolve and open
unexpectedly. `ResolveFileLink` checks absolute, then relative to the buffer's own
directory, then `ProjectRoot()`, returning `nullopt` — never creating anything — if the
target doesn't exist anywhere, unlike `find-file`.

**URL opening defaults to `xdg-open`, not unset** — a deliberate difference from
`FormatOnSave.h`'s own "unset, nothing built-in ever sets one" precedent: the wishlist
entry that originated this feature explicitly named `xdg-open`-style opening as the
intended behavior, and opening a URL (unlike running an arbitrary formatter) is safe and
non-destructive. Overridable via the new `ned/set-url-open-command`.

**A real command-injection risk was identified and designed around, not an
afterthought**: `RunFormatCommand` already splices a *user-configured* command string
into a raw `std::system` shell invocation, justified by its own comment as safe only
because that string is the user's own trusted configuration, never file content. A link
target is exactly the opposite trust profile — it comes from *buffer content*, which can
be an untrusted cloned repo or downloaded note. `OpenUrl` therefore never touches a shell
at all: `fork` + `execlp` with the url passed as its own `argv` element. Fire-and-forget
(a detached `std::thread` reaps the child via `waitpid` on that one pid) rather than a
process-wide `signal(SIGCHLD, SIG_IGN)`, which would have silently broken
`RunFormatCommand`'s own `std::system`-based child-reaping elsewhere in the same process
(POSIX: `wait`/`waitpid` return `ECHILD` once `SIGCHLD` is globally ignored, even for a
caller's own explicit children) — found by reasoning through the interaction before
writing either mechanism, not discovered as a regression afterward.

**Org's own bracket-link model, `Org.h/.cpp` additions**: `Link{target, description,
startByte, endByte}` + `ParseLinks` (one regex per line, `Org.h`'s established
`std::regex`/ECMAScript convention, a line may hold more than one link) + `LinkAtPoint`
(the usual read-only `*AtPoint` wrapper). `FindHeadlineByTitle` backs real Org's internal
`[[*Some Headline]]` link form via an exact-match search over `ParseOutline`'s own
`Headline.title` — internal links via `[[#custom-id]]` against a property drawer are
explicitly out of scope, property drawers don't exist in this codebase yet (see the
"v2+ maybe" list below), not silently dropped.

**Follow-on-activate reuses the `VisitSearchResult` shape exactly** (project-search
follow-up) rather than inventing new plumbing: a new `InteractiveRequest::OpenLinkAtPoint`
+ `open-link-at-point` command that only signals intent, acted on immediately by
`BufferView::StartInteractiveSession` as a one-shot direct action — no new `InputMode`.
`BufferView::OpenLinkAtPoint()` tries `org::LinkAtPoint` first in an org-mode buffer (an
internal `*Heading` target jumps point in-buffer; anything else is classified and handed
to the same open/report tail the generic path uses — an Org link to a URL or file path is
still just a URL or file path once its brackets are stripped), falling back to
`link::DetectLinkAtPoint` — the same "Org-specific first, generic fallback" chain
`org-cycle`'s own body already established for fold-cycle → table-align. Bound globally
to `C-c C-l` (reachable from every mode, per the user's own "any feature available to any
mode should be available to all modes" principle from the Tables slice) — `OrgMode()`
*additionally* binds real Org's own `C-c C-o` to the same command, deliberately
**shadowing** the global `find-scratch` binding while an org-mode buffer is active, the
same kind of intentional, documented mode-over-global shadow already established (and
smoke-tested) for `C-c C-p` in the second Org slice.

**Descriptive-link rendering — genuinely new territory in `Source/UI/BufferView.cpp`**:
everything `Paint()` did before this either hid whole *lines* (fold/unfold) or expanded
one codepoint into *more* columns (tab, binary hex placeholder); nothing collapsed a
multi-byte range down to *fewer* columns than its raw text. A new link cache
(`org::ParseLinks`, mirroring `highlightCacheBuffer_`'s generation-gated shape exactly)
is skipped entirely whenever `mode_.name != "org-mode"` — a single string compare,
cheaper even than `FoldMarkers().empty()`'s own check, so every non-Org buffer (the
common case across the whole editor) never runs the parse at all. A `RenderedLink`
(`.cpp`-local) is any link whose own span does **not** contain point — the one line of
logic that makes point-entry reveal the raw markup for free, since every consumer just
falls through to its existing plain-codepoint path whenever a byte offset isn't a
collapsed link's start. Three consumers share this: `Paint()`'s own render loop (paints
the link's description — or target, if none was given — with a new `theme.linkForeground`
brush, then jumps straight to the link's end byte, skipping the raw bytes entirely);
`CursorPosition()`'s `VisualColumn` helper (gained a `lineLinks` parameter, defaulted so
nothing outside this file needed to change); and `ByteOffsetForPoint` (click
translation), which used to delegate entirely to `Buffer::ByteOffsetForLineAndColumn` —
that method must *stay* entirely link-oblivious, `Buffer` has zero Org-specific
knowledge by hard, repeated project convention (fold markers are a generic
`{offset -> marker}` map for exactly this reason) — so a new `.cpp`-local
`ByteOffsetForColumnInLine` reimplements its same tab-aware walk locally instead of
extending it, written to behave byte-for-byte identically when no links are present
(verified by a unit test) so the call site needn't branch on mode name at all. A click
landing anywhere on a collapsed link's own rendered text resolves to the link's own start
byte — clicking a link un-collapses it on the very next render, a small, free affordance
that fell out of the design rather than being separately built.

**Explicit, documented scope cut**: `Buffer`'s own vertical motion
(`MoveDownLines`/`MoveUpLines`, goal-column tracking) stays entirely link-oblivious —
extending it would mean teaching `Buffer` about Org syntax, which this codebase has
refused to do at every prior Org slice. `C-n`/`C-p` through a line with a collapsed link
may land the cursor a little off from the pre-motion goal column — the same category of
accepted approximation `kMaxTabAwareColumnScan`'s own fallback already documents, not a
new kind of imprecision this project hasn't already shipped elsewhere.

Verification: 725 total test cases (up from 691), clean, including under
`-DNED_ENABLE_SANITIZERS=ON` — new `Tests/LinkTest.cpp` (bare-URL detection and its
trailing-punctuation trim, path-shaped vs. bare-word token classification,
`ResolveFileLink`'s three-tier lookup, `OpenUrl`'s no-command-configured case, never a
real spawn), `Tests/OrgTest.cpp` (`ParseLinks` incl. multiple links per line and a
link with no description, `LinkAtPoint`, `FindHeadlineByTitle`), `Tests/CommandsTest.cpp`
(the `C-c C-l`/`C-c C-o` bindings), and `Tests/BufferViewTest.cpp` (collapsed vs.
point-revealed rendering, click-to-start-byte on a collapsed link, the internal-link
jump, the Org-bracket-to-generic fallback chain, the non-Org-buffer path, an existing
relative file link switching buffers, and the never-collapses-outside-org-mode case) —
plus a `screen`-based pty smoke test against the real binary: a real `.org` file's
`[[https://example.com][a website]]` rendered as just "a website", moving point onto it
revealed the raw brackets, `C-c C-o` on an internal `[[*Some Heading]]` link jumped to
that headline, a bare URL and a real relative file path both opened via the generic
`C-c C-l` path in a non-Org buffer, and `C-c C-o` still opened `find-scratch` normally in
a non-Org buffer (mirroring the existing `C-c C-p` shadow-scope smoke test).

### Org: real syntax highlighting, including inline emphasis, via a forked grammar — done

Seventh slice, and the last line on the v1-must checklist below. Started as "wire up
`nvim-orgmode/tree-sitter-org` the same way every other bundled grammar's `Mode` already
works" — ended up bigger, for two real reasons found during the slice itself, not
assumed going in.

**First**: `Source/Editor/TreeSitter/Query.cpp`'s `Query::Captures` calls
`ts_query_cursor_next_capture` directly and never evaluates a single tree-sitter
predicate (`#eq?`/`#match?`/etc.) — confirmed by reading it. Both the raw grammar's own
example `queries/highlights.scm` and the real, production `nvim-orgmode/orgmode` Neovim
plugin's `queries/org/highlights.scm` (fetched directly from GitHub, not assumed) depend
on predicates — some standard, some Neovim-Lua-only custom ones no C++ consumer could
run regardless — for their most important distinctions (headline level, TODO vs. DONE).
With predicates silently ignored, every headline-level pattern and every TODO/DONE
pattern in either file structurally matches the same node, and this codebase's own
"later capture wins" rule means every headline would render as whatever the last cyclic
pattern happens to be and every TODO/DONE keyword would render identically — worse than
no highlighting at all. **Second, and the reason this became a bigger build**: the
grammar has no dedicated node for inline emphasis markup at all (`*bold*`, `/italic/`,
`_underline_`, `=verbatim=`, `~code~`, `+strikethrough+`) — every marker character is
just an ordinary symbol inside its own `expr` token. Initially proposed deferring
emphasis for exactly that reason; told directly this was the wrong call — the fix for a
grammar with a real gap isn't to skip the feature, it's to fix the grammar, and not to
chase nvim-orgmode's (or any vim-ecosystem tool's) own conventions as a constraint while
doing it — Ned's own Org format was never meant to be a byte-for-byte match to real
Emacs Org either, see this phase's own opening paragraph above.

**Part 1 — a real fork, `/Development/NED/tree-sitter-ned-org`** (a separate local git
repository, `main` branch, `upstream` remote pointing at the real
`nvim-orgmode/tree-sitter-org` for reference/future pulls — not a live dependency).
Based on the same `v1.3.2` tag already verified against this project's own
node-types.json/corpus research. Adds six external-scanner token pairs (open/close per
marker) to the existing `src/scanner.c` (which already had an external scanner for list
indentation/bullets, extended rather than replaced) plus matching grammar rules,
deliberately using **only forward lookahead** — no backward-context tracking, unlike
`tree-sitter-markdown-inline`'s own `LAST_TOKEN_WHITESPACE`-phantom-token approach for
the same class of problem — by exploiting where these new tokens are structurally
offered: `bold`/`italic`/etc. sit alongside `$.expr` as alternatives only at
`_inline_content`'s own outer choice point (`item`/`_expr_line`/`_multiline_text`), never
spliced into `expr`'s own internal immediate-chained continuation, so "not mid-word" for
an opening marker falls out of grammar position rather than needing the scanner to
detect it.

That design needed three real, test-driven fixes before it actually worked, each caught
by a genuine failing parse, not predicted in advance:
1. **Every marker preceded on its own line by other content silently failed to open at
   all** — traced (via `tree-sitter parse --debug` and direct `parser.c`/state-table
   reading) to the scanner-dispatch call being placed too early in `scan()`: the
   existing leading-whitespace-skip loop runs *before* it and, on hitting a non-space
   byte, simply stops without giving the emphasis check a second look at the position it
   stopped on. Fixed by moving the check to run immediately after that loop, threading
   through whether it actually skipped anything (`precededByWhitespace`) so a close
   marker's own "immediately preceded by non-whitespace" rule — which, unlike an internal
   token's `token.immediate`, external tokens get no automatic enforcement of — could be
   checked explicitly too.
2. **`"2*3"` incorrectly opened bold** — proof that "not mid-word" wasn't actually free
   from grammar position alone in every case; external tokens could still be offered
   immediately after an ordinary `expr`'s own continuation in states this project
   couldn't fully map by hand. Fixed with an explicit `precededByWhitespace ||
   get_column() == 0` guard.
3. **Nesting (`"*bold /italic/*"`) produced a parse ERROR** — the closing marker of a
   nested span sitting directly against the *enclosing* span's own closing marker (`/`
   immediately followed by `*`) wasn't recognized as a valid boundary, since the boundary
   character set was real Org's own `org-emphasis-regexp-components`, which doesn't
   include other emphasis markers. Fixed by adding "any other emphasis marker character"
   to the boundary set — Ned's own addition, not real Org's.
4. **A genuine regression against the grammar's own pre-existing corpus**: `'+'`
   overlaps with real Org's list-bullet character the exact same way `'*'` overlaps with
   headline stars, and the first version of the fix for (1) broke an existing upstream
   test (`"  - a\n  + a\n"`, a bullet change meant to end one list and start another) by
   swallowing the second list's own `+` bullet as strikethrough instead. Fixed with the
   same defer-to-structural-use-first, fall-back-to-emphasis-second pattern already used
   for `'*'` vs. headline stars.

All 140 of the original grammar's own corpus tests still pass; 11 new
`test/corpus/emphasis.txt` cases cover all four fixes above plus multi-word content,
verbatim/code staying literal, and the headline-vs-bold column-0 disambiguation.

**Part 2 — bundling + `Mode`/`Theme` wiring, the originally-scoped-sized part.**
`CMakeLists.txt`'s `ned_add_treesitter_grammar(tree-sitter-org ...)` points at the local
fork path — a plain local filesystem path is a normal git remote as far as
`FetchContent`'s own `GIT_REPOSITORY` is concerned, no change needed to the function
itself. New, hand-written `Source/Editor/TreeSitter/OrgHighlights.scm` (embedded via the
same `ned_embed_treesitter_query` mechanism every bundled grammar uses, just pointed at a
repo-local path) — no predicates anywhere, one pattern per construct so there's never a
same-node multi-pattern ambiguity to begin with. Covers headlines, tags, checkboxes,
comments, property/drawer names, directives, block/dynamic-block delimiters, table
`hr`s, timestamps, and all six emphasis markers (now genuine grammar nodes, not a
delimiter-character-matching workaround). Most captures (`@tag`/`@checkbox`/`@comment`/
`@attribute`/`@keyword`/`@punctuation`/`@constant`/`@strong`/`@emphasis`/`@underline`/
`@strikethrough`) map straight through the *existing* generic `CaptureTable()`
(`Mode.cpp`) with a handful of new entries; `verbatim`/`code` reuse the existing `String`
class. Two captures — `"org.headline.stars"` (cyclic heading level from star count,
curated to 3 rather than real Org's 8, matching this project's own repeated
curated-v1-subset precedent) and `"org.keyword.candidate"` (checked against
`org::TodoKeywords()`'s own configured list — last index is "done," everything earlier
is "still open," the standard single-sequence Org convention, no new config surface) —
are deliberately *not* generic captures, resolved instead in `OrgMode()`'s own custom
`HighlightFunction` (built by hand, not via the shared `TreeSitterModeFromLanguage()`
template every other bundled grammar's `Mode` uses), in three ordered passes so a later,
narrower span (a tag, a TODO keyword) visually wins over an earlier, broader one (the
whole headline line's own level color) via `HighlightSpan`'s own documented "later wins"
rule. Eight new `SyntaxClass` members (`Mode.h`) and matching `Theme` fields, `Brush`
itself gaining real `underlined`/`strikethrough` bools (confirmed real `ftxui::Cell`
fields by reading `cell.hpp` directly, not assumed) since nothing needed them before.

**Explicit scope cuts, not oversights**: priority cookies (`[#A]`) get no special color
from tree-sitter — still correctly parsed/cycled by `Org.cpp`'s own regex-based logic,
unaffected — since the grammar has no dedicated node for them and the only structural way
to match one is an anonymous-token pattern the grammar's own upstream author already
flags as fragile. TODO/DONE coloring only recognizes an *exact* match against
`org::TodoKeywords()`, matching `Org.cpp`'s own established "exact match or nothing" rule
elsewhere.

Verification: 738 total test cases (up from 725), clean under
`-DNED_ENABLE_SANITIZERS=ON` — new coverage in `Tests/ModeTest.cpp` (headline-level
cycling and its wraparound, TODO/DONE against both the default and a custom configured
keyword list, no span for a non-keyword first word, tags/checkboxes/comments/
directives/blocks/table-`hr`/timestamps, all six emphasis markers including the
mid-word-never-opens and verbatim-stays-literal cases, and nesting) and
`Tests/ThemeFileTest.cpp` (the eight new round-tripped keys) — plus, independently, the
forked grammar's own 151 `tree-sitter test` corpus cases (140 original + 11 new), and a
`screen`-based pty smoke test against the real binary: a real `.org` file combining
headlines at multiple levels, TODO/DONE, tags, checkboxes, a source block, a table, a
comment, a `SCHEDULED:` timestamp, and nested emphasis all rendered without corruption or
crashes, mode line correctly reading `(org-mode)` throughout, and point movement/redraw
across all of it staying stable across multiple frames.

**v1 must** (the actual daily-use core of Org, per the user's own likely usage pattern
of notes/outlines/task lists, not the whole feature list above):
- [x] Headline/outline structure (`*`/`**`/`***` stars) with subtree fold/unfold.
- [x] TODO keyword cycling (`TODO` -> `DONE`, a configurable keyword set).
- [x] Tags (`:tag1:tag2:`) — parsed since the first slice (`Headline.tags`), editable
      since the fourth (`org-set-tags`, `C-c C-q`).
- [x] Priorities (`[#A]`/`[#B]`/`[#C]`).
- [x] Checkboxes (`- [ ]`/`- [X]`), including a parent item auto-reflecting its
      children's checked state, the one piece of Org's structured editing that goes
      beyond plain markup.
- [x] Tables — parsing and column alignment (fifth slice, also shared with Markdown/GFM
      tables via `Source/Editor/Table.h`); formula support explicitly deferred, see
      below.
- [x] Links (`[[target][description]]`) with follow-on-activate (sixth slice) — plus a
      generic, cross-mode bare-URL/file-path "follow link at point" that isn't Org-
      specific at all, and real Org's own descriptive-link rendering (raw markup
      collapses to just the description, revealing it only while point sits inside).
- [x] Real syntax highlighting (seventh slice) via a real fork of
      `nvim-orgmode/tree-sitter-org` (`/Development/NED/tree-sitter-ned-org`), the same
      `Mode`/`HighlightSpan` pipeline every other language already uses — including real
      grammar-level inline emphasis markup, which the upstream grammar has no support
      for at all.

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

## Save-time final newline — done

Unrelated, small, user-requested follow-up landed the same session as the Org-mode work
above: "always having a newline at the end of the file should be configurable, but by
default on" — matching Emacs' `require-final-newline`/VSCode's `files.insertFinalNewline`.

New `Source/Editor/FinalNewline.h/.cpp`: `SetEnsureFinalNewline`/`EnsureFinalNewline`,
process-wide mutex-guarded bool mirroring `ScratchPad.h`'s `SetScratchAutoSaveEnabled`
pattern exactly, default **on**. `Buffer::SaveToFile`/`Save` (`Text/Buffer.h/.cpp`) gained
an `ensureFinalNewline` parameter, default `true` (a plain literal, not a read of the
`Editor`-layer setting — `Text/` still has zero dependency on `Source/Editor/`, the same
layering `tabWidth` parameters already preserve) — `Commands.cpp`'s `save-buffer` is what
actually passes the real, Janet-configurable value through, via `EnsureFinalNewline()`.
`ned/set-ensure-final-newline` (`EditorBindings.cpp`) is the Janet binding, real this
time (unlike Org's still-missing `ned/set-org-todo-keywords`) since `Value.h` already
marshals `bool` — no new marshalling work needed.

**Deliberately disk-only, not a live buffer edit** — the one real design decision here,
found by actually tracing what the "obvious" alternative would break rather than assumed:
a version that called `Buffer::InsertAt` to make the added newline a real, visible,
undoable edit was tried first, then rejected after walking it through this codebase's own
pre-existing `"Undo/redo mark the buffer modified..."` test (`Tests/BufferTest.cpp`) — that
approach pushes a save-time-only edit onto the undo tree the user never typed, meaning the
very next `Undo` after a save would undo the newline instead of the user's actual last real
edit, silently breaking that test's own documented assumption about what a single `Undo`
lands on. Fixed by keeping the whole thing disk-only: `SaveToFile` builds its own local
copy of the content (already computed for the write, regardless), appends `'\n'` to *that*
copy only if non-empty and not already newline-terminated, and never touches `Rope_`
itself — `Point_`/`Mark_`/`Modified_`/`ContentGeneration_`/the undo tree are all completely
unaffected by a save. The tradeoff, stated plainly rather than glossed over: the live,
still-open buffer's own `Text()` doesn't visibly gain the newline until closed and
reopened (a `Buffer::FromFile` reload of the just-saved file will show it) — accepted as
the right v1 call given the undo-pollution alternative, not treated as equivalent options.

Ripple effects, all confirmed and fixed rather than merely anticipated: existing tests
asserting exact on-disk byte content after a save (`Tests/BufferTest.cpp`'s round-trip and
atomic-save-failure cases, `Tests/CommandsTest.cpp`'s format-command cases,
`Tests/ScratchPadTest.cpp`'s auto-save case) all needed either an explicit
`ensureFinalNewline=false` argument (for the two tests that are really about round-trip
fidelity/atomicity, not this feature) or an updated expected string with a trailing `\n`
(for the ones going through the real `save-buffer` command/`AutoSaveScratchBuffers`, which
now genuinely do write one). Found by grepping every test file for a disk-content
assertion after a `.Save(`/`.SaveToFile(` call (direct or indirect, e.g. through
`AutoSaveScratchBuffers`), not by waiting for `ctest` to report failures one at a time.

Verification: 7 new test cases (`Tests/FinalNewlineTest.cpp`, 5 new `Tests/BufferTest.cpp`
cases, 1 new `Tests/EditorBindingsTest.cpp` case), 624 total, clean — plus a `screen`-based
pty smoke test against the real binary: `C-x C-s` on a file with no trailing newline
correctly appended one on disk by default, and `(ned/set-ensure-final-newline false)` in
`init.janet` correctly suppressed it on a second real save.

## Phase 10 — Structural selection expansion + fuzzy finder — done

Pulled out of Phase 9's aspirational wishlist and sequenced, in this order:

1. **Structural/AST-aware selection expansion** (`expand-selection`/`shrink-selection`,
   `M-=`/`M--`) — done. A natural fit given the tree-sitter foundation already in place
   (real parse trees per buffer via `Source/Editor/TreeSitter/`), flagged as such in the
   keybinding-audit follow-up's "Want" bucket (`Docs/KeybindingAudit.md`). `Node` gained
   `Parent()`/`IsNamed()`/`NamedDescendantForByteRange()`; `Mode` gained a third
   `expandSelection` closure (alongside `highlight`/`fold`, sharing the same parse cache),
   built generically by `TreeSitterModeFromLanguage` for all 12 tree-sitter-backed modes —
   `FundamentalMode`/`OrgMode` stay unsupported, a documented scope cut (`OrgMode`'s own
   separate, non-shared highlight closure would need its own follow-up). `BufferView` owns
   the expansion-history stack (session/UI state, not `Buffer` state — `Buffer::SetPoint`
   unconditionally resets its own transient run-state, `GoalColumn_`/`CanAmend_`, on every
   call, which would erase the history the instant expand/shrink's own repositioning ran),
   staleness-checked by buffer identity + `ContentGeneration()` the same way
   `highlightCacheBuffer_` already is, and cleared by `RunCommandAndHandleOutcome` whenever
   a *genuinely invoked* command (`Dispatcher::Outcome::Invoked`, not `Pending`) isn't
   expand/shrink itself — the `Pending`-vs-`Invoked` distinction was a real bug caught
   during testing: the bare `Escape` half of the `ESC =`/`ESC -` two-chord fallback binding
   was clearing the history before the second chord ever arrived.
2. **Fast fuzzy file finder** (`project-find-file`, `C-c C-f`) — done. Turned out to need
   far less new machinery than scoped: `Source/Editor/FuzzyMatch.h` (generic
   subsequence-match scorer) and the M-x `execute-extended-command` flow's entire
   interaction shape (type-to-narrow-and-reset-to-top, arrow-key selection, one-line
   `EchoArea` rendering via `FormatFuzzyCandidates` — renamed from
   `FormatExecuteCommandCandidates` once it turned out to be fully generic already) were
   reused as-is, not rebuilt; `editor::BuildProjectTree` (already powering `ProjectSidebar`)
   supplied the file walk. The one real new piece: `BufferView` caches the candidate list
   itself (`projectFindFileCandidates_`, project-relative path strings) for the duration of
   one session, populated once when the session starts rather than re-walking the project
   on every keystroke the way M-x's cheap in-memory `Registry().Names()` lookup can afford
   to — a real recursive directory walk is not free at project scale. No separate "command
   palette" work was needed at all: it already existed as `execute-extended-command`.

**fuzzy-candidate-list-styling follow-up** (post-Phase-10 polish, same session): the shared
`FormatFuzzyCandidates` candidate list, used by both M-x and `project-find-file`, had two
real, user-reported problems. First, the selected entry's leading `*` read as noise rather
than a clear grouping marker — replaced with real `[brackets]`, plus actual bold
(`EmphasizeForEchoArea`) on the selection and a dimmed foreground
(`DimForEchoArea`, blended halfway toward the background via the same
`ftxui::Color::Interpolate` mechanism `ModeLine`'s gradient already uses) on every other
visible candidate — the "already bold" the user expected turned out not to actually exist;
`EchoArea` applied exactly one uniform brush to the whole row. `EchoArea` (`Source/UI/
EchoArea.h/.cpp`) gained a small, closed, private rich-text mechanism to make this possible
without turning `statusMessage_` into a rich-text type every other writer (isearch,
`save-buffer`, error messages, ...) would then have to participate in: `EmphasizeForEchoArea`/
`DimForEchoArea` wrap a substring in a pair of C0 control-byte sentinels (never legitimately
present in any real status message), and `EchoArea::Paint` strips them at render time,
applying the style to the span between. Second, and the more substantive bug: the visible
window used to cap at a *fixed count* (`kMaxVisibleCandidates = 6`, sized for ~10-25-character
command names) rather than the real terminal width — fine for M-x, but `project-find-file`'s
typically-longer path candidates could overflow a real (narrower, or just candidate-heavy)
terminal well before 6 items were shown, silently clipping mid-filename with no `+K more`
even visible. `FormatFuzzyCandidates` now grows a window around the selected candidate by an
actual *column budget* (`BufferView::AvailableCandidateColumns`, derived from this widget's
own live `size().width` — an approximation, since `EchoArea` spans the full terminal width
while `BufferView`'s own box is narrower once a sidebar/scrollbar/gutter are subtracted, but
a safe one: it can only under-, never over-, estimate what's really available) instead of a
hardcoded item count, reserving headroom up front for the `+K more` suffix so that
reservation itself can't cause an overflow. The scroll-with-selection behavior itself already
existed (the window already followed `selected` as arrow keys moved it) — this fixed *how
much* fits in it, not whether it followed the selection. Raised alongside a broader question
about this codebase's total lack of any floating/popup/overlay widget concept (`ProjectSidebar`'s
own context-menu descoping note) — the user's view: worth reconsidering later, once other
overlay-shaped needs exist (an LLM-integration panel, in-editor debugging tooling), rather
than treated as settled; flagged here for that future discussion, not resolved by this
follow-up, which deliberately kept `EchoArea` a single row.

## LSP client — slice 1: core plumbing + diagnostics — done

The first real slice of the LSP client (Wishlist's "Language intelligence" group flagged
it as "likely a prerequisite for most of the rest of that group"). Genuinely new
infrastructure, not an extension of anything: nothing in this codebase previously spoke to
a long-lived subprocess over pipes (`Editor/FormatOnSave.cpp` is one-shot `std::system()`
through temp files; `Editor/Link.cpp`'s `OpenUrl` is a detached `fork`+`exec` with no pipes
at all), and no JSON library existed anywhere (every prior "json" hit in the tree was the
tree-sitter-json *grammar*, for `.json` syntax highlighting only).

**Architecture**, under a new `Source/Editor/Lsp/` directory (mirroring how tree-sitter got
its own `Source/Editor/TreeSitter/` subdirectory):
- `Lsp/Transport.h/.cpp` — raw process + pipe mechanics, no JSON/LSP semantics. Spawns via
  `posix_spawn` (the user's own explicit preference: least overhead/greatest throughput for
  spawning from a large parent process — avoids `fork`'s full address-space duplication) with
  a manual `$PATH` search done *before* spawning (`ResolveExecutable`), not `posix_spawnp` --
  confirmed by reading glibc's real spawn internals that a missing-executable failure isn't
  reliably reported synchronously by `posix_spawn(p)` itself (the child's failed `execve`
  only surfaces later via its exit status), so this makes a missing language server binary
  fail immediately with a clear message instead. `WriteFrame`/`ReadFrame` implement LSP's
  `Content-Length: N\r\n\r\n` framing directly over the pipes.
- `Lsp/LspClient.h/.cpp` — one JSON-RPC 2.0 connection. A background `std::jthread` runs a
  blocking read loop, marshaling each parsed frame onto the main FTXUI thread via
  `ftxui::ScreenInteractive::Post` — the same mechanism `WindowManager::StartAutoSaveTimer`
  already established, just event-driven instead of timer-driven. `pending_`/
  `notificationHandlers_` deliberately have no mutex: everything that touches them, including
  `DispatchFrame`, only ever runs on the main thread (the background thread only calls
  `Transport::ReadFrame`, which shares no state with these maps). Member declaration order
  (`readThread_` before `transport_`) is load-bearing: C++ destroys members in reverse
  declaration order, so `transport_`'s destructor (closes fds, kills+reaps the child) runs
  *before* `readThread_`'s own destructor tries to join it — which is what actually unblocks
  the otherwise-permanently-blocked `ReadFrame()` call sitting in that thread. A `stop_token`
  alone cannot interrupt a blocking `read()`; this ordering is the real interruption
  mechanism, confirmed the hard way (see Bugs below).
- `Lsp/LspServerConfig.h/.cpp` — a mutex-guarded map (language name → argv), mirroring
  `Editor/ModeOverrides.h`'s shape (a map, not `FormatOnSave.h`'s single-scalar shape, since
  an LSP command is inherently per-language). `ned/set-lsp-command` (`Janet/
  EditorBindings.cpp`) is the only way to configure one, e.g.
  `(ned/set-lsp-command "c" ["clangd"])`. Nothing is bundled, and `ned` never installs or
  updates a language server itself — same trust boundary `ned/set-format-command` already
  established, deliberately not re-litigated (raised and confirmed explicitly this session:
  auto-install was considered and rejected for the same non-portable-system-layout reason
  dynamic-grammar-loading's own tooling already rejected it for grammars).
- `Lsp/LspManager.h/.cpp` — owns the running `LspClient`s (keyed by language, one server per
  language — matches `ProjectRoot()`'s own single-root, process-wide model, no multi-root
  workspace concept anywhere in this codebase). `SyncBuffer` lazily spawns +
  `initialize`/`initialized`-handshakes a server, sends `textDocument/didOpen` once per
  buffer then `textDocument/didChange` (whole-document sync, not incremental — simplest
  correct choice for v1, matching this project's own repeated "prove it needed before
  optimizing" discipline) whenever `Buffer::ContentGeneration()` has advanced. Called once
  per frame from `BufferView::Paint()` for the *active* buffer only — a background buffer
  won't get live diagnostics until viewed again, a straightforward widening of the same
  mechanism if ever needed, not a design change. `textDocument/publishDiagnostics` is
  registered as a notification handler at client-construction time, resolving the
  notification's URI back to an open `Buffer` via the *existing*
  `BufferList::FindByPath` (already used by `ProjectSidebar`'s click handling for exactly
  this URI/path → open-`Buffer` lookup) and converting each LSP `Diagnostic` — a UTF-16-
  code-unit range, a real, easy-to-get-wrong detail handled via `Rope::CodepointAt`-based
  code-unit counting, not assumed to be byte offsets — into `Buffer::SetDiagnostics`.
- **`Text/Buffer.h`** gained a small, editor-agnostic `Diagnostic` struct (byte range +
  severity + message) plus `SetDiagnostics`/`Diagnostics`/`DiagnosticsGeneration`, the same
  "structured per-position metadata that happens to live on Buffer" role `FoldMarker`/
  `NarrowedRange_` already have — no dependency on `Lsp/`/JSON at all, keeping `Text/`'s
  zero-dependency-on-`Editor/` layering intact. Unlike `FoldMarkers_`, never relocated across
  edits: `SetDiagnostics` always replaces the set wholesale, matching `publishDiagnostics`'
  own "here is the full current set" semantics.
- **`Source/UI/BufferView.h/.cpp`** gained its own dedicated 1-column gutter slot
  (`kDiagnosticWidth`, between the status column and the line-number gap — layout is now
  `[status][diagnostic][gap][digits][gap][fold]`), cached the same generation-gated way
  `foldGutterEntries_`/`unsavedChangeLineRanges_` already are, and a new `lsp-show-diagnostic`
  command (`C-c C-e`) reporting whatever diagnostic covers point via the shared status
  string — the same "direct action on `context.buffer`, report through `context.message`"
  shape `org-cycle-todo` already uses, no new UI surface needed. `Theme` gained four new
  severity colors (`diagnosticError`/`Warning`/`Information`/`Hint`), persisted by
  `ThemeFile.cpp` alongside every other standalone `Color` field.
- **Window-splitting integration**: `LspManager` is wired through `WindowManager`'s existing
  `SetProjectSidebar`-style "forwarded to every pane, present and future" convention
  (`WindowManager::SetLspManager`, threaded through `Pane`'s constructor and `MakePane`) —
  window-splitting already existed by the time this landed, which the original slice-1 plan
  hadn't accounted for; discovered and correctly integrated during implementation rather than
  bolted on afterward. Buffer-close notifications (`textDocument/didClose`) are sent from
  both real close paths this codebase already has — `WindowManager::HandleBufferClosed`
  (`BufferView::SetOnBufferClosed`, a pane-driven close) and `NotifyBufferClosing`
  (`ProjectSidebar`'s own preview-buffer-swap close, not pane-driven at all) — rather than
  inventing a third, since both already existed as real, distinct "a buffer is really about
  to be gone" entry points.
- **New dependency**: `nlohmann/json` (`v3.12.0`), via the plain `FetchContent_Declare`/
  `FetchContent_MakeAvailable` pattern already used for FTXUI/utf8proc/Catch2, not the
  tree-sitter-specific `ned_add_treesitter_grammar*` functions (which exist only to work
  around grammar repos' Node-oriented build files). Chosen over `simdjson`/`Glaze` — all
  three confirmed actively maintained this session, not assumed — because an LSP client
  hand-writes/reads dozens of small, heterogeneous, mostly-optional-field JSON-RPC message
  shapes, where `nlohmann::json`'s dynamic, ergonomic `operator[]`-based API matters far more
  than either alternative's raw parse throughput (`simdjson`) or reflection-based fixed-struct
  binding (`Glaze`, a worse fit for LSP's numerous, spec-evolving, mostly-dynamic message
  shapes).

**Deferred, explicitly, not oversights**: syncing every open buffer, not just the active one;
incremental (vs. whole-document) sync; idle-timeout server teardown; multi-root workspaces.
Structured JSON-RPC/lifecycle error visibility (spawn failure, disconnect, error responses)
shipped in the "error visibility" follow-up, below — **raw subprocess stderr capture remains
deferred** (still redirected to `/dev/null`; a materially separate piece of work, see that
follow-up's own entry for why). Hover and ghost-text completion shipped in slice 2; code actions
in slice 3; go-to-definition and rename in slice 4 — see each entry below. (This paragraph
originally also listed go-to-definition/code actions/rename themselves as deferred; left
inaccurate for a while as later slices shipped without this note being updated alongside them —
corrected here, not a claim any of the underlying work is new.)

**Real bugs found during implementation, not hypothetical**:
- `ftxui::ScreenInteractive` is a type alias (`using ScreenInteractive = App;`), not a
  forward-declarable class — an initial `namespace ftxui { class ScreenInteractive; }`
  forward-declare (mirroring how other headers forward-declare types) compiled in isolation
  but produced a genuine conflicting-declaration error once the real header was also included
  transitively, with GCC's post-error recovery misattributing later, unrelated errors to the
  wrong namespace entirely — confusing enough to chase down properly rather than paper over.
  Fixed by including the real header directly in both `Lsp/LspClient.h` and `Lsp/
  LspManager.h`, matching `WindowManager.h`'s own existing convention for the same type.
- A real end-to-end `Transport` test spawning `/bin/cat` as a stand-in echo "server" hung
  indefinitely on its very first run: `cat`'s stdout is fully buffered (not line-buffered)
  whenever it isn't a real tty, which a pipe never is, so it silently sat on the test's small
  payload forever instead of ever echoing it back. Fixed by routing through `stdbuf -o0`.
- `LspClientTest.cpp`'s own test fixture (an `LspClient` wired to a raw, test-driven pipe
  pair, no real subprocess) hung on teardown for the same class of reason `Transport`'s own
  member-ordering trick exists to solve, but in a shape that trick doesn't cover: in
  production, killing the real child process is what closes the pipe's write end and
  generates the `EOF` that unblocks the background read thread. In the test, that write end
  was a plain fd held by the *test itself*, never closed automatically by anything — so the
  background thread's blocked `read()` (and therefore `LspClient`'s own destructor, which
  joins that thread) hung forever. Fixed by giving the test fixture its own explicit
  destructor that closes the test-owned write end first.
- Adding the new diagnostic gutter column shifted every existing gutter-column pixel
  position in `BufferViewTest.cpp` by one, breaking 36 pre-existing, unrelated tests at
  once — not a regression in what they were actually testing, just a stale mirror of
  `BufferView::GutterWidth()`'s own formula living in the test file itself
  (`Tests/BufferViewTest.cpp`'s own local `GutterWidth()`/`ContentRowText()` helpers), plus a
  few tests hardcoding absolute pixel columns/viewport widths directly rather than deriving
  them. Fixed by updating the test-local formula and the handful of hardcoded expectations to
  match, once identified as the actual, single root cause rather than 36 independent problems.

Verification: full suite (917 test cases) and a clean `./test-asan.sh` pass (one pre-existing,
already-confirmed-flaky ASan-timing performance test at its threshold, unrelated to this
work) — no sanitizer findings at all, notable given how much of this slice is real fds,
pipes, a spawned subprocess, and background-thread lifecycle code, exactly the class of bug
ASan/UBSan is best at catching. Manually smoke-tested against a real, installed `clangd`
(v22.1.8) via a standalone scratch program driving `Transport` directly against a real spawned
process (not a test-mocked pipe pair): `initialize`/`initialized` handshake, `textDocument/
didOpen` on a C file with a deliberate error, and a real `textDocument/publishDiagnostics`
came back with the exact expected message ("Use of undeclared identifier
'undeclared_identifier'") on the exact expected `uri`. Also confirmed, incidentally: clangd
sends an unrelated `publishDiagnostics` for its own `~/.config/clangd/config.yaml` on
startup, which `LspManager::HandlePublishDiagnostics`'s `BufferList::FindByPath`-based URI
lookup already handles correctly (silently dropped, since it's not an open buffer) —
matches the real behavior observed, not just the intended design.

## LSP client — slice 2: hover + completion (ghost text) — done

The two deferrals slice 1 flagged as "new `SendRequest` call sites plus their own UI wiring" —
no change to `LspClient`'s public surface was needed, exactly as predicted.

- **Hover** (`lsp-hover`, `C-c C-j`): sends `textDocument/hover` for point, reports the result
  through the shared status string, the exact same "direct action, report via
  `context.message`" shape `lsp-show-diagnostic` already established — the response arrives
  async (well after the command function itself returns), so the command captures the raw
  `std::string*` (`context.message`) directly into `LspManager::RequestHover`'s callback rather
  than trying to write synchronously; valid for as long as the owning `BufferView` is, the same
  accepted lifetime shape the diagnostics-publish handler and the scratch-autosave thread
  already rely on.
- **Completion** (ghost text): automatic while typing, debounced (`~350ms` default,
  `ned/set-lsp-completion-debounce`), or forced manually (`lsp-complete`, `C-M-i` — not `M-/`,
  which is already bound to `redo`). Both configurable from Janet
  (`ned/set-lsp-auto-complete`). Rendered as dimmed italic inline text right after point
  (`Theme::ghostTextForeground`, added the same way `binaryForeground` was); `Tab` accepts,
  `M-n`/`M-p` cycle candidates, any other key dismisses. Debounce timing reuses
  `ScrollArrowButton::OnAnimation`'s exact shape (`ftxui::animation::RequestAnimationFrame`,
  self-perpetuating, no dedicated thread) rather than `WindowManager::StartAutoSaveTimer`'s
  `std::jthread` — a sub-second, cancel-by-overwrite delay fits the animation-frame idiom better
  than a free-running background timer.
  - **Suppression heuristic** (the user's own explicit ask: stop firing on numeric literals or
    mid-string edits): before scheduling, checks the tree-sitter `SyntaxClass` immediately
    before point (via the same `highlightCacheSpans_`/`SpansForLine`/`ClassAtOffset` machinery
    `Paint()` already uses for highlighting) for `String`/`StringEscape`/`Comment`/
    `DocComment`/`Number`, **and**, independent of whether highlighting is even available
    (`FundamentalMode` and any other mode with no highlighter), whether the ASCII
    word/identifier token immediately before point is purely digits — the same
    `WordPrefixStart` helper both this check and ghost-text suffix computation share. Also
    gated on the triggering keystroke being a plain, unmodified codepoint (no
    Control/Meta/Special) and the buffer's `ContentGeneration()` actually having advanced, so
    it only ever fires for organic self-insert typing, never macro replay or M-x-invoked
    commands (`RunCommandAndHandleOutcome` takes an optional `triggeringChord`, non-null only
    from `OnKeyEvent`'s own normal-dispatch call site).
  - **Suffix computation deliberately trusts the server**: no client-side prefix-matching
    against the completion list (the user's own specific complaint about other editors --
    "as I prepend something to a string it only takes the chars I've typed into
    consideration") -- the server already re-filters its own candidate list against the
    buffer's current, just-synced content on every request. `GhostSuffixFor` only computes
    *how much of the already-typed prefix to not re-display* (subtracting it from
    `insertText` when it's a real prefix; showing `insertText` in full otherwise -- a
    documented v1 gap for a server response using a `textEdit` range instead).
- **`LspManager`** gained `RequestHover`/`RequestCompletion`, both resolving purely from
  `bufferState_` (populated by `SyncBuffer`'s prior `didOpen`) rather than taking a language
  param — a buffer with no sync state, or no running client, resolves to "no results"
  synchronously rather than spawning a server just to answer one request. Response parsing
  (`ExtractHoverText`/`ExtractCompletionItems`, handling every shape the LSP spec allows for
  each — bare string/MarkupContent/MarkedString-array for hover, bare array/`CompletionList`
  for completion) was factored out to a new, namespace-scope `Lsp/LspContent.h/.cpp` —
  directly unit-testable against crafted JSON without a live client — the same "extract a pure
  conversion into its own declared, testable header" precedent `Lsp/LspPosition.h` (itself
  extracted from `LspManager.cpp`'s original diagnostics-only helpers, now shared by both
  directions of position conversion) already established. Server-advertised capabilities
  (`hoverProvider`, `completionProvider.triggerCharacters`) are still not captured from
  `initialize`'s response — an unsupported request just resolves to "no results," handled
  identically to any other empty response; a real gap if capability-gated triggering (e.g.
  honoring the server's own trigger characters instead of this heuristic) is wanted later.
- **Testing**: `LspManager` had no dedicated test file at all before this (a pre-existing gap,
  not something this slice introduced) — `Tests/LspManagerTest.cpp` fills it, covering hover/
  completion/diagnostics together via a new `LspManager::SetClientForTesting` seam (public,
  "primarily for tests," mirroring `LspClient::DispatchFrame`'s own identical precedent) that
  injects an already-constructed `LspClient` — wired to a raw pipe pair via the same
  `Transport`-based constructor `LspClientTest.cpp` already uses, no real subprocess — bypassing
  `ClientForLanguage`'s normal spawn path entirely. Discovered and fixed along the way: the
  injection path originally left `textDocument/publishDiagnostics` unrouted (only
  `ClientForLanguage`'s real spawn path wired the notification handler) — factored into a
  shared `WireNotificationHandlers` so an injected test client behaves identically to a real
  one. The same fixture pattern, reused in `Tests/BufferViewTest.cpp`, drives real ghost-text
  accept/cycle/dismiss behavior end-to-end (a real `textDocument/completion` request sent, a
  real canned JSON-RPC response delivered via `DispatchFrame`) rather than needing a live
  language server — `C-M-i` itself is fed as the real raw byte sequence
  (`ESC` + `0x09`) a terminal would actually produce, exercising the same `TranslateKey` path a
  live keystroke would, not a hand-built `KeyChord`.

Verification: full suite (945 test cases) and a clean `./test-asan.sh` pass, no findings.

## LSP client — slice 3: code actions (quick fixes) — done

The "fix available" gap — `textDocument/codeAction` (server-suggested fixes, each a named
action carrying a `WorkspaceEdit`), reachable via `lsp-code-action` (`C-c C-a`).

- **Picking among actions**: not a fuzzy-typed filter like `M-x`/`lsp-complete` — code actions
  are few and short, so a plain numbered list (`Up`/`Down` to move, a digit `1`-`9` to jump
  directly, `Enter` to confirm) needs no `MinibufferPrompt` at all, closer in shape to the
  register-name-entry commands' "read one more keystroke and act" than to
  `HandleExecuteCommandKey`'s fuzzy-filtered one. Always ends in a y/n confirm on the action's
  own title (`HandleCodeActionConfirmKey`, mirroring `HandleDeleteFileKey`'s Confirming stage
  exactly) — no inline diff/highlight-then-confirm, matching how every other LSP-adjacent
  command in this codebase already reports through the status line rather than needing new
  rendering infrastructure.
- **Async, no "waiting" input mode**: `StartInteractiveSession`'s `LspCodeAction` case is a
  fire-and-forget one-shot (`RequestCodeActionsAtPoint`, mirroring `RequestCompletionAtPoint`'s
  own shape exactly, including its generation + buffer/point identity staleness guard) —
  `inputMode_` only actually changes (`LspCodeActionSelect`/`LspCodeActionConfirm`) once the
  response arrives, inside the callback, so a stale response from a request the user has since
  moved on from (different point, different buffer) is silently dropped rather than suddenly
  yanking focus into a selection prompt.
- **Range sent to the server**: the diagnostic covering point if there is one (same lookup
  `lsp-show-diagnostic` already does against `Buffer::Diagnostics()`), else a zero-length range
  at point.
- **Scope cut, explicit**: only edits touching the *current buffer's own URI* are applied. A
  `WorkspaceEdit` reaching into other files (`"changes"` naming more than one URI, or the more
  general `"documentChanges"` form entirely — renames/file-creation, unparsed) is refused
  wholesale ("edits other files — not supported yet"), not partially applied. A bare
  `Command`-only action (no `"edit"` at all — the server wants to run its own logic) is refused
  too; executing arbitrary server-side commands is out of scope. Multi-edit application sorts
  descending by start byte before applying (`Buffer::DeleteRange`+`Buffer::InsertAt` per edit)
  so an edit not yet applied keeps a valid offset as an earlier one shifts positions — LSP
  guarantees edits within one `WorkspaceEdit` don't overlap, so a plain sort suffices, no
  interval-merging needed. Undo is **not** coalesced across a multi-edit fix
  (`Buffer.h`'s own documented "deletes always start a fresh step" policy) — an accepted
  multi-edit fix needs multiple `undo` presses to fully revert; a real, harmless (still fully
  reversible either way, just not in one step) v1 gap, not silently assumed away.
- **`Lsp/LspContent.h`** gained `WorkspaceTextEdit`/`CodeAction`/`ExtractCodeActions` alongside
  the existing `CompletionItem`/`ExtractCompletionItems` — a `WorkspaceTextEdit`'s own
  start/end deliberately stay `LspPosition`s, not byte offsets, resolved against the buffer's
  *current* content only at the moment the user actually accepts the fix (which can be well
  after the response arrived), the same reasoning `GhostCompletion`'s own suffix computation
  already established for LSP positions vs. byte offsets. `LspManager` gained
  `RequestCodeActions` (same "resolve purely from `bufferState_`" shape as
  `RequestHover`/`RequestCompletion`) and a `DiagnosticToLsp` helper — the reverse of
  `HandlePublishDiagnostics`'s own severity/range conversion — for building the request's
  `context.diagnostics`.
- **Testing**: `ExtractCodeActions` unit-tested directly against crafted JSON (same-URI edits,
  multi-URI refusal, `documentChanges` refusal, a bare `Command` item, title-less items
  skipped); `LspManager::RequestCodeActions` round-tripped through the same
  `SetClientForTesting`/`FakeServer` fixture slice 2 built, asserting the outgoing
  `range`/`context.diagnostics` shape; `BufferView` end-to-end (same fake-server fixture as the
  ghost-completion tests) covering zero/one/multiple actions, digit-select then confirm
  applying the right one's edit, and `n` at confirm leaving the buffer untouched.

Verification: full suite (958 test cases) and `./test-asan.sh` — one pre-existing,
already-documented ASan-timing-flaky performance test at its threshold (unrelated to this
work, reproduces in isolation on unrelated code); `[Lsp]`/`[BufferView]` alone are fully clean
under ASan/UBSan, no findings.

## LSP code actions — resolve follow-up — done

Real-world testing against `clangd` immediately surfaced a gap slice 3 missed: `clangd`
advertises `codeActionProvider.resolveProvider` and deliberately sends a `CodeAction` back with
no `"edit"` yet, expecting a `codeAction/resolve` round-trip (the exact original item sent back
verbatim, including any opaque `"data"`) once the user actually picks it — every real fix from
`clangd` was hitting `ApplyCodeAction`'s "has no edit to apply" refusal. Fixed: `initialize`'s
capabilities now advertise `textDocument.codeAction.resolveSupport`/`dataSupport`;
`CodeAction` gained `resolvable` (true for an item shaped like a real `CodeAction` — has
`"kind"`, a bare `Command` never does — but missing `"edit"`) and `raw` (the original item,
preserved for resolve); `LspContent.h`'s per-item parsing was factored out to a new
`ExtractSingleCodeAction`, shared by `ExtractCodeActions`' own loop and the new
`LspManager::ResolveCodeAction`; `HandleCodeActionConfirmKey`'s `y` branch calls it first
(fire-and-forget, same async shape as everything else here) when `resolvable`, applying the
result only once the resolved edit actually arrives.

## Status message lifecycle — done

A pending multi-chord key sequence (`C-x` waiting for its second chord) gave no feedback at
all, and status messages generally just sat there indefinitely once set, however stale —
flagged after a real false-alarm ("`C-p` fired on its own after `C-a`") turned out to be by
design (`C-a` is a direct binding, not a prefix) but exposed the missing feedback as a real gap.
- `Key.h/.cpp` gained `FormatKeyChord`/`FormatKeySequence`, the reverse of
  `ParseKeyChord`/`ParseKeySequence` (round-trips for anything a real keystroke can produce).
  `BufferView::OnKeyEvent`'s normal-dispatch path now shows the accumulated sequence
  (`"C-x-"`, matching real Emacs' own echo-area convention) while `Dispatcher::Feed` returns
  `Pending`, and `"<sequence> is undefined"` on `Unbound` — gated on `RunCommandAndHandleOutcome`'s
  own `ran` return value, not the `Outcome` captured at the call site, since a Match'd command
  that throws never lets `Dispatcher::Feed` reach its own `return Outcome::Invoked`, which
  would otherwise clobber the exception's own message with a bogus "is undefined" (a real bug,
  caught immediately by this session's own pre-existing exception-handling test).
- Every status message now auto-clears after a short idle timeout (`EnsureStatusMessageFreshness`,
  called from `Paint()`; the actual clear happens in `OnAnimation`, same self-perpetuating
  `ftxui::animation::RequestAnimationFrame` shape the ghost-completion debounce already uses) or
  immediately on the next real dispatched command that doesn't itself report anything new
  (`RunCommandAndHandleOutcome`'s own post-invoke check) — whichever comes first. Both are
  diff-based (compares `statusMessage_` against a snapshot) rather than hooking the dozens of
  call sites that write it directly; the one accepted trade-off is a command that explicitly
  re-sets the exact same text it already showed is indistinguishable from one that never
  touched it, so a rare identical re-display can appear to flash-clear.
- Deliberately excluded while any interactive session is active (`inputMode_ != Normal`) — a
  live prompt's own text (`"Project search: foo"`) is that session's actively-managed state,
  re-shown every keystroke by its own `Handle*Key` method regardless, and must never be cleared
  out from under the user just because they paused mid-typing.
- Every prompt/session cancel path (`Escape`/`C-g`) now sets a real `"X cancelled."` message
  instead of blanking immediately, so the auto-clear mechanism gives visible confirmation the
  cancel actually registered rather than silently going blank.

## project-search .gitignore support — done

`project-search` (and `project-replace`, which calls the exact same `SearchDirectory`) was
reported "locking up" — investigated and confirmed: `SearchDirectory` only ever skipped
dot-directories, so a real recursive search walked and opened every file under `build/`,
`cmake-build-*/`, and similar generated/dependency directories too — 2+ GB and ~12,000 files in
this repo's own case, scanned synchronously on the UI thread with zero progress feedback. Not
an infinite loop (no symlink-follow, dot-dirs correctly pruned) — just slow enough to feel
exactly like a hang. `ProjectTree.cpp`'s `BuildProjectTree` (the sidebar) had the identical gap.

Fixed with a new `Editor/GitIgnore.h/.cpp` (`GitIgnoreMatcher`) rather than a hardcoded
directory-name list — correct by construction for any repo. Deliberately root-level only (reads
a single `.gitignore` from the directory passed in, always `editor::ProjectRoot()` in every real
call site — matches `ProjectRoot.h`'s own single-root model; nested `.gitignore` files aren't
read, a documented v1 cut). Glob subset: literal segments, `*`/`?`, real git's own anchoring
rule (a pattern with an interior or leading `/` anchors to root; no `/` at all matches any
depth), trailing-`/` directory-only patterns, and `!`-negation with later-rule-wins ordering —
covers what real `.gitignore` files actually use; character classes and a mid-pattern `**` are
not specially handled (degrades to matching a single path segment). Each pattern compiles to an
anchored ECMAScript `std::regex` (matching this codebase's existing regex choice elsewhere).
Wired into both `SearchDirectory` (skip alongside the existing dot-directory/binary-file checks)
and `BuildProjectTree`/`WalkTree` (threaded through the recursive walk to compute each entry's
root-relative path) — `ProjectReplace` needed no changes at all, it already calls
`SearchDirectory`.

Verification: full suite (983 test cases) and `./test-asan.sh` — clean (the one pre-existing,
already-documented ASan-timing-flaky performance test aside, unrelated to this work).

## Read-only "tossable" buffers + ripgrep-backed project-search — done

Two problems reported from using the just-shipped project-search feature: closing a
`*search results*` buffer prompted to save (it has no file, and the user never intentionally
edited it — `BuildResultsBuffer`'s own `InsertAtPoint` unavoidably marks it `Modified()`), and
project-search itself was slow (a single-threaded C++ walk + `std::regex_search` per line, one
`ifstream` open per file).

- **`Text/Buffer.h/.cpp`** gained `ReadOnly()`/`SetReadOnly(bool)` — every content-mutating
  method (`InsertAtPoint`, `DeleteBackwardAtPoint`, `DeleteForwardAtPoint`, `DeleteRange`,
  `InsertAt`) throws `std::runtime_error("Buffer is read-only.")` up front when set, the single
  enforcement point rather than duplicating the check in every command that happens to mutate a
  buffer — the exception surfaces through the exact existing `RunCommandAndHandleOutcome` catch
  path, itself now covered by this session's own status-message auto-clear.
- **`ReadOnly()` doubles as "tossable"**, one concept, not two: `RequestCloseBuffer` and
  `StartInteractiveSession`'s `ConfirmQuit` case, plus (a real gap caught while testing —
  `quit`'s own separate `anyModified` check in `Commands.cpp` decides whether to even *enter*
  `ConfirmQuit`, a second call site easy to miss) `Commands.cpp`'s `quit` command itself, all
  gate their `Modified()` check on `&& !ReadOnly()`. `BuildResultsBuffer` calls
  `SetReadOnly(true)` right after building content — the one change point, since all three
  synthesized-buffer call sites (project-search results, project-replace's preview,
  project-agenda) already route through it.
- **Fold gutter and syntax highlighting are also suppressed for a read-only buffer** (a
  follow-up caught from actually using the feature: a real Mode's fold/highlight query run
  against a results buffer's own "path:line: text" content produces meaningless spans/regions,
  not an empty result, since Mode is a per-pane property, not per-buffer). Centralized in a new
  `BufferView::FoldGutterActive()` (`mode_.fold && CodeFoldingEnabled() && !ReadOnly()`),
  replacing four independent copies of the same condition across `EnsureFoldableBlocksCache`,
  `Paint()`'s own gutter-width math, `GutterWidth()`, and `OnMouseEvent`'s fold-click hit test
  that would otherwise have been able to drift out of agreement with each other; the highlight
  cache's own `!mode_.highlight` check gained the same `|| buffer.ReadOnly()`.
- **Enter and a mouse click on a read-only buffer now visit the result under
  point/click** (`project-search-visit-result`/`C-c C-v`'s own existing logic, just reached two
  more ways) — safe to key off `ReadOnly()` alone, without needing to know which specific kind
  of results buffer this is, because `VisitSearchResult()` already silently no-ops on any line
  that isn't shaped like a search result.
- **`Editor/ProjectSearch.cpp`** now tries `rg` (ripgrep) first if found on `$PATH`
  (`FindRipgrepOnPath`, a manual `$PATH` walk mirroring `Lsp/Transport.cpp`'s own
  `ResolveExecutable`) — `posix_spawn` (not `fork`, matching this codebase's established
  preference), stdin from `/dev/null`, stderr to `/dev/null` (never inherited — a child writing
  to an inherited stderr fd would corrupt this TUI app's own terminal display), stdout captured
  via a pipe and parsed as ripgrep's own `--json` output (`nlohmann::json`, already a
  dependency). `--no-config` keeps behavior predictable regardless of the invoking user's own
  `~/.config/ripgrep/config`; `--` before the path argument guards a pattern/path starting with
  `-` from being misparsed as a flag. `SearchDirectory` always constructs its `std::regex`
  first, unconditionally, so its documented "throws `std::regex_error` on invalid syntax"
  contract holds identically regardless of which backend ends up running; any `rg` failure (not
  found, spawn error, real error exit) falls back to the original single-threaded scanner,
  which keeps using the root-only `GitIgnoreMatcher` built for the previous follow-up — `rg`
  itself doesn't need it, since its own native `.gitignore` support is more complete (nested
  files, global gitignore, `.git/info/exclude`). Confirmed directly (not assumed) that `rg`
  only honors a `.gitignore` file when it can actually detect a VCS repo (a bare `.gitignore`
  with no `.git` isn't enough) — the two `.gitignore`-specific tests were adjusted to create a
  `.git` marker in their fixtures to match, since a real `.gitignore` almost always implies a
  real repo anyway. `ProjectReplace` needed no changes, it already calls `SearchDirectory`.

Verification: full suite (996 test cases) and `./test-asan.sh` — clean (the one pre-existing,
already-documented ASan-timing-flaky performance test aside); the new subprocess-spawn code
specifically re-run under ASan/UBSan in isolation, also clean.

## LSP client — repaint-on-background-update follow-up — done

Reported directly from daily use: right after opening a file, the LSP server hasn't finished its
own initial parse yet (expected — it's spawned lazily), but once it *had* finished, nothing on
screen changed until the next real keystroke or mouse event — a diagnostic, or a hover/completion/
code-action response arriving later, silently updated real state with no visible sign anything had
happened.

Root-caused by reading FTXUI's own `app.cpp`, not assumed: `ScreenInteractive::Post()`'s `Closure`
task variant runs the posted closure but never sets `frame_valid_ = false` afterward, unlike its
`Event`/`AnimationTask` variants, both of which do. `LspClient::StartReadLoop`'s background read
thread already marshals every server-pushed frame onto the main loop via exactly this `Post()` path
(`LspClient.cpp`) — the fix is one line: the posted closure now also calls
`ftxui::animation::RequestAnimationFrame()` after dispatching the frame, the same "force a real
repaint soon, no dedicated event needed" mechanism `ScrollArrowButton`'s press-and-hold repeat and
ghost-completion's debounce already rely on for an identical reason. Diagnostics, hover text,
completion candidates, and code actions that arrive from the server now become visible the moment
they're ready, not on the next unrelated input event.

## Per-buffer Mode resolution — done

Reported directly from daily use, alongside the fix above: opening `CMakeLists.txt` in a pane that
had previously shown a C++ file rendered it through C++'s own tree-sitter grammar (real, if
nonsensical, highlighting from an error-tolerant parse against the wrong language) and reserved a
fold-gutter column that never found anything foldable (C++'s fold query never matches CMake syntax).
Root cause: `Mode` had been a per-*pane* property since the window-splitting follow-up, resolved
once from whichever file a pane was created/split showing, and never re-derived when the active
buffer inside that pane later changed (`find-file`, `switch-to-buffer`, a tab click, a
project-sidebar click, visiting a search result, ...) — a long-standing, explicitly documented v1
scope cut. Per the user's own direction, Mode is now a property of the buffer being viewed, not the
pane.

- **`Editor/ModeOverrides.h/.cpp`** gained `ModeForPath(path)` and `ModeForBuffer(buffer)` —
  `ModeForPath` is `main.cpp`'s own former anonymous-namespace `ModeForPath` promoted out and
  reworked to route its bundled extension table through `ModeByName`/`BundledModeFactories()`
  (which also picked up a missing `"org-mode"` entry along the way — a pre-existing gap, `OrgMode()`
  had never been registered there) rather than calling each bundled `*Mode()` factory directly, so
  it stays a thin caller of the existing override registry instead of a second, competing table.
  `ModeForBuffer` is `buffer.Path() ? ModeForPath(*path) : FundamentalMode()`. `main.cpp` now calls
  `ModeForBuffer` directly instead of keeping its own copy — one shared table, not two that could
  drift.
- **Key finding that made the actual per-buffer wiring cheap**: `Mode` was already owned by value
  at a stable per-`Pane` member address (`Pane::mode_`), and `Dispatcher`'s `KeymapStack` already
  stored `&mode_.keymap` — the member's own address, not a snapshot taken at construction — so
  reassigning `mode_ = someOtherMode;` in place, at any time, is sufficient to swap
  highlighting/folding/expand-selection/keymap all at once, with **no `Dispatcher`/`KeymapStack`
  rebuild needed**. `ModeLine` shares that same `Mode&`, so it needed zero changes either —
  reassigning `Pane::mode_` updates both automatically.
- **`UI/BufferView.h/.cpp`** gained `SetOnActiveBufferChanged(std::function<void(text::Buffer&)>)`,
  the same "connect after construction, unset is a safe no-op" convention every other `Set*` hook
  here already follows, plus a new `modeSyncBuffer_` member mirroring `topLineValidatedBuffer_`'s
  own "seed at construction so the first `Paint()` is never mistaken for a switch" precedent
  exactly. `Paint()` now checks `modeSyncBuffer_` against the active buffer's identity first thing,
  the same "recompute, don't cache, detect via pointer identity" idiom already used pervasively
  throughout this file, firing the handler (if any) on a real change.
- **`UI/WindowManager.cpp`**'s `Pane` constructor wires the one-line integration point:
  `bufferView_->SetOnActiveBufferChanged([this](text::Buffer& buffer) { mode_ = editor::ModeForBuffer(buffer); });`.
  Every existing buffer-identity-keyed cache in `BufferView` (`highlightCacheBuffer_`,
  `foldableBlocksCacheBuffer_`, etc.) already correctly invalidates on this same buffer-identity
  change, since `mode_` in this design only ever changes in lockstep with a real buffer switch —
  no new cache keys were needed anywhere.
- **Explicit scope cut, carried forward unchanged**: a Janet call to
  `ned/set-mode-for-extension`/`ned/set-mode-for-filename` while a buffer of that type is *already*
  active doesn't immediately re-render it — it takes effect on the next real switch away and back.
  This was already the case before this fix (Mode never changed at all); it's a strict improvement,
  just not "live re-apply to the currently-focused buffer mid-session," a different, smaller feature.

Verification: full suite (1003 test cases, 7 new — `ModeOverridesTest.cpp`'s `ModeForPath`/
`ModeForBuffer` coverage, `BufferViewTest.cpp`'s hook-firing-semantics test, and
`WindowManagerTest.cpp`'s end-to-end render-based proof that switching a pane's buffer actually
changes the rendered `ModeLine` text, not just `Pane::ModeRef().name`) and `./test-asan.sh` — clean
(the one pre-existing, already-documented ASan-timing-flaky performance test aside).

## LSP client — error visibility follow-up — done

Reported directly from daily use: a misconfigured/missing LSP server command crashed the whole
running editor (`LspManager::ClientForLanguage` constructed `LspClient`/`Transport`, which throws
`std::runtime_error` on a spawn failure, uncaught the entire way up through `BufferView::Paint()`'s
per-frame `SyncBuffer` call). Separately, every other LSP failure mode — server crash/EOF, a real
JSON-RPC `"error"` response — was silently discarded with no visibility at all, and the user asked
for errors to *stream live* to a dedicated buffer as they happen, not be visible only after the
fact.

- **Crash fix**: `ClientForLanguage` now wraps `LspClient`'s construction in a `try`/`catch`,
  reporting via the new `LogError` (below) instead of letting the exception propagate. A new
  `failedCommands_` map (language -> the exact argv that last failed) stops `SyncBuffer` from
  retrying (and re-logging) the same known-bad command every single frame, while still trying
  again once the user reconfigures `ned/set-lsp-command` to something different — a process-lifetime
  latch, not a timed retry/backoff, matching this subsystem's existing static-config model.
- **`LspManager::LogError(language, message)`** (public) finds-or-creates a read-only,
  `kLspLogBufferName` (`"*lsp log*"`) buffer and appends one timestamped line to its end, then
  requests a repaint (`ftxui::animation::RequestAnimationFrame()`, the same idiom the
  repaint-on-background-update follow-up above already established) so the new line becomes
  visible without waiting for an unrelated keystroke. Lives on `LspManager` itself — it already
  uniquely owns both dependencies this needs (`BufferList&`, and the `ScreenInteractive&` every
  `LspClient` already threads through for the same reason).
- **`Text/Buffer` gained `AppendWhileReadOnly(text)`** — appends at the current end of content
  regardless of `Point`, generic (not LSP-specific, since `*agenda*`/`*search results*`/
  `*project replace*` could plausibly want the same live-update treatment later), sharing `InsertAt`'s
  existing body via a new private `InsertAtImpl`. Requires `ReadOnly()` already `true`; throws
  `std::logic_error` otherwise (distinct from every other mutator's `std::runtime_error` for "user
  tried to edit a read-only buffer" — this is a caller-bug signal, not a normal runtime condition).
- **All three failure sources now report**: spawn failure (`ClientForLanguage`'s catch, above);
  server disconnect (`LspClient` gained `SetOnDisconnected`, invoked from `StartReadLoop`'s two
  previously-silent exit points — malformed frame, EOF — via the same `screen_.Post(...)` +
  `RequestAnimationFrame()` marshaling its own frame-dispatch path already uses; `LspManager`'s
  `ClientDisconnected` erases the dead client and every affected `bufferState_` entry so the next
  `SyncBuffer` respawns and re-`didOpen`s cleanly); and a real JSON-RPC `"error"` response
  (`RequestHover`/`RequestCompletion`/`RequestCodeActions`/`ResolveCodeAction` now extract and log
  `error["message"]`, previously discarded entirely in all four).
- **Discovery**: a new `lsp-show-log` command (`M-x` only, no dedicated keybinding, matching
  `org-agenda`'s own precedent) switches to `*lsp log*`, creating it if needed. `BufferView::Paint()`
  polls a cheap `LspManager::HasUnseenLogEntry()` flag once per frame and, if set and
  `statusMessage_` is currently empty, sets a one-line hint (`"LSP error -- see *lsp log* (M-x
  lsp-show-log)"`) and acknowledges it — never forces a buffer switch (would be actively hostile
  mid-typing), matching this codebase's existing "surface via `statusMessage_`, never forcibly
  navigate the user's buffer" precedent.
- **Threading**: confirmed by reading the code, not assumed — every `LogError` call site (the
  spawn-failure catch, the four response-error branches, `ClientDisconnected`) already runs on the
  main thread by the time it's reached; the one genuinely background-thread-originated event
  (`SetOnDisconnected`'s callback) is `Post`-marshaled first, the same pattern the existing
  frame-dispatch path already established.
- **Deferred, explicitly**: raw subprocess stderr capture (still redirected to `/dev/null`) — a
  materially separate piece of work (redirecting `Transport`'s spawned stderr to a pipe instead, a
  second background drain thread, deciding how raw unstructured stderr text interleaves with the
  structured log lines this follow-up adds) that isn't needed to answer the user's actual complaint
  ("errors ... not seen ... stream to error buffer"), which this follow-up already covers for every
  *structured* (JSON-RPC/lifecycle) failure.

Verification: full suite (1012 test cases, 9 new across `LspManagerTest.cpp` — spawn-failure/gate/
reconfigure and JSON-RPC-error-message coverage — `LspClientTest.cpp`, `BufferTest.cpp`'s
`AppendWhileReadOnly` coverage, and `BufferViewTest.cpp`'s status-hint/`lsp-show-log` coverage) and
`./test-asan.sh` — clean (the one pre-existing, already-documented ASan-timing-flaky performance
test aside). The real background-thread-EOF-to-`Post`-driven-callback path itself can't be
exercised headlessly (no test in this codebase runs a real `ScreenInteractive::Loop()`, and FTXUI's
own `Post` has no synchronous fallback — confirmed by reading `app.cpp`) — `LspClientTest.cpp`
covers `SetOnDisconnected`'s registration/replacement only, while the actual reporting behavior for
every path that *doesn't* need `Post` (spawn failure, JSON-RPC errors) is covered end-to-end.

## Horizontal scroll-follow + smart line-wrap — done

Reported directly from daily use editing `init.janet`: typing past the right edge of the viewport
didn't scroll horizontally to follow point at all — the cursor simply vanished (`CursorPosition()`
had an honest `// scrolled off horizontally; no horizontal scroll in v1` comment). Org-mode/
markdown-mode prose benefits far more from real line-wrap with smart word-break than from
horizontal scrolling, so both now exist, selected per-mode by default and overridable per file
extension/filename.

- **`Mode` gained `bool wrapLines = false;`** — `MarkdownMode()`/`OrgMode()` set it `true`; every
  other bundled mode leaves it at its default.
- **New `Editor/WrapOverrides.h/.cpp`** — a small standalone extension/filename override table
  mirroring `ModeOverrides.h/.cpp`'s exact shape (deliberately *not* folded into the mode-override
  mechanism, which resolves an entire named `Mode`, not one field of it). `EffectiveWrapLines(path,
  mode)` resolves a per-file override first, falling back to the `Mode`'s own default. Exposed to
  Janet via `ned/set-wrap-for-extension`/`ned/set-wrap-for-filename`.
- **Horizontal scroll-follow** (the non-wrap path): new `leftColumn_` member mirrors `topLine_`
  exactly, with `LeftColumn()`/`SetLeftColumn()` and `ScrollToShowPointHorizontally()` (a no-op
  once `EffectiveWrapLines()` is true — a wrapped line never exceeds the viewport width by
  construction, so there's nothing left to scroll horizontally). `Paint()`'s content loop gained a
  leading fast-forward phase that consumes but never draws whatever falls before `leftColumn_`;
  `CursorPosition()`/`ByteOffsetForPoint()` both account for it symmetrically.
- **Smart word-break wrapping**: new `ComputeWrapSegments` (whitespace-boundary breaking only —
  matches this codebase's own already-established word-motion scope cut, not Unicode-aware — with
  a graceful hard-break fallback for a single token wider than the whole viewport, e.g. a long
  URL). A `RenderedLink` span is treated as one atomic unbreakable unit, same as the rest of this
  file already does via `LinkStartingAt`.
- **Row-count cache generalizes the existing fold quartet**: the pre-existing
  `IsLineHidden`/`NextVisibleLine`/`AdvanceVisibleLines`/`VisibleLineCountBetween` assumed every
  visible line is exactly one canvas row (true for fold's collapse, not for wrap's expansion). New
  `RowsForLine`/`VisibleRowCountBetween`/`VisibleRowCountAtLeast` generalize this; `topLine_` stays
  strictly line-granular (always a line's first wrap segment, never mid-segment) — a deliberate,
  documented v1 scope cut. `Paint()`'s row loop, `CursorPosition()`, and `ByteOffsetForPoint()` were
  all extended to walk wrap segments; gutter line numbers/fold glyphs render only on a line's first
  segment, matching how real editors visually distinguish a wrapped continuation.
- **Two real bugs found and fixed during implementation, not hypothetical**:
  - **A genuine perf regression, caught by its own `[Performance]` test before shipping**: the
    first version of `RowsForLine`'s cache eagerly computed every line's real word-break scan
    up front on every content/width change; since `MaxTopLine()`/`ScrollToShowPoint()` run every
    `Paint()` call, this made every frame on a large wrap-enabled document pay for the whole
    document's word-break cost. Fixed by making the cache populate lazily (one line's scan,
    memoized, only the first time that specific line is actually asked about) and adding
    `VisibleRowCountAtLeast` (an early-exit bounded check) for the "does everything already fit"
    queries, so a huge document's answer ("no") is discovered within the first viewport-height's
    worth of lines, never by touching the rest of the buffer.
  - **A real, user-reported bug**: scrolling to the end of a wrapped document could leave its own
    trailing lines permanently unreachable. Root cause: `MaxTopLine()`'s backward walk gave a
    wrapped line "partial credit" toward the viewport budget when only part of it fit — but
    `topLine_` can only ever start at a line's own first row, so `Paint()` would render that
    line's *full* row count regardless, silently pushing everything after it off the bottom no
    matter how far the user scrolled. Fixed by walking backward including only whole lines that
    fit within the budget (the first line considered is always included in full, even if it alone
    exceeds the viewport, to guarantee progress); `ScrollToShowPoint()`'s own backward walk had the
    identical bug and got the identical fix, using `RowsForLine(pointLine)` so it never partially
    shows point's own line either. A dedicated regression test (`BufferViewTest.cpp`) pins this
    exact scenario.

Verification: full suite (1033 test cases, 17 new — `ModeTest.cpp`'s `wrapLines`-default coverage,
new `WrapOverridesTest.cpp`, `BufferViewTest.cpp`'s horizontal-scroll/word-break/hard-break/cursor-
position/mouse-click/gutter/override/scroll-to-bottom-regression coverage, and a new
`[Performance]` case) and `./test-asan.sh` — clean (the one pre-existing, already-documented
ASan-timing-flaky performance test aside; the new wrap `[Performance]` case was itself tuned down
in scale, the same "leave real margin under ASan's Debug-plus-instrumentation overhead" precedent
the pre-existing JsonMode `[Performance]` case already established, after confirming directly that
its own cost tracks `Paint()` call count, not buffer size — `Paint()` only ever visits the visible
viewport, never the whole document).

## Line-truncation indicator — done

Small follow-up requested right after horizontal-scroll-follow shipped: a clipped (non-wrap), too-
long line gave no visual sign there was more content past the right edge — it just looked identical
to a line that happened to end exactly at the viewport's own edge. `Theme` gained
`truncationIndicatorForeground` (a muted "blurple," deliberately distinct from every existing
syntax/UI-chrome color but mid-brightness rather than alarming — a hint, not a warning), persisted
through `ThemeFile.h/.cpp` (`truncation_indicator_foreground` key) like every other theme color.
`Paint()`'s content loop now overwrites a clipped row's own last-drawn column with a `»` glyph in
that color whenever the loop exits because it ran out of viewport width rather than reaching the
end of what the row has to show — unreachable under wrap by construction (a wrapped segment never
exceeds the viewport width), so this only ever fires on the horizontal-scroll path.

Verification: full suite (1036 test cases, 4 new — a `ThemeFileTest.cpp` round-trip case plus three
`BufferViewTest.cpp` cases covering the clipped/fits-exactly/wrap-never-clips scenarios) and
`./test-asan.sh` — clean.

## LSP client — slice 4: go-to-definition + rename — done

The last two items on the LSP client's original "deferred, explicitly" list (see that follow-up's
own entry above — the note has now been corrected to stop naming them as deferred). Both reachable
via new commands (`lsp-goto-definition` — `M-.`/`ESC .`, matching real Emacs' own
`xref-find-definitions` binding; `lsp-rename` — `C-c C-M-r`, since `C-c C-r` was already
`project-replace`) that just set `InteractiveRequest::LspGotoDefinition`/`LspRename`, the same
"command signals intent, BufferView owns the actual request/session" shape `LspCodeAction` already
established.

- **`LspContent.h`** gained `DefinitionLocation`/`ExtractDefinitionLocations` (parses a
  `textDocument/definition`-shaped result — a bare `Location`, a `Location[]`, or a
  `LocationLink[]`, which reports its target via `targetUri`/`targetSelectionRange` instead of
  `uri`/`range` — all three normalized to one loop; a malformed entry is skipped, not treated as a
  parse error) and `RenameEdit`/`RenameResult`/`ExtractRenameEdits` (parses a bare
  `WorkspaceEdit`, not wrapped in an item the way a code action response is). `uri` stays a raw
  string in both, kept URI-agnostic like every other `ExtractX` function in this file —
  `LspManager` is what resolves a `uri` to a real `std::filesystem::path` (via its own
  already-existing, file-local `UriToPath`), the same layering split
  `ExtractCodeActions`/`ExtractSingleCodeAction` already established by taking `ownUri` as a plain
  string rather than resolving it themselves.
- **Rename's real scope, deliberately wider than code actions'**: `ExtractCodeActions`' own
  `WorkspaceEdit` parsing is scoped to one buffer's own URI, refusing wholesale the moment a
  `"changes"` map names any other URI — the right call for a quick-fix, but rename's entire value
  is usually renaming a symbol used *across* files, so `ExtractRenameEdits` keeps every URI the
  response actually named, not just the requesting buffer's own. `LspManager::ResolvedRename`
  mirrors this: a `ResolvedRenameEdit` per URI. The `"documentChanges"` form (needed for a rename
  that also creates/renames/deletes files, not just edits existing ones) is still unparsed —
  `touchesUnsupportedForm=true`, refused wholesale, edits left empty — the one scope cut kept from
  the code-actions precedent, since parsing it is a materially separate, larger piece of work
  (versioned document identifiers, file-create/rename/delete operations) than "stop refusing a
  `"changes"` map just because it names more than one URI."
- **`BufferView::ApplyRename`** resolves (find-or-open, via `BufferList::FindByPath` then
  `BufferList::OpenFile`) *every* touched file first, applying nothing until every single one
  succeeds — a rename either fully applies across every affected file or leaves every buffer
  untouched, never partially across only some of them. `ApplyWorkspaceTextEdits` (file-local,
  `BufferView.cpp`) is `ApplyCodeAction`'s own resolve-LspPositions-against-current-content +
  descending-sort-by-start-byte + `DeleteRange`/`InsertAt` sequence, factored out so both it and
  `ApplyRename` share the exact same edit-application logic. Every affected buffer is left
  modified-but-unsaved afterward, same as any other in-editor edit — no auto-save-across-files
  behavior; this codebase has no "save all" command at all yet, saves are always user-initiated
  (`C-x C-s`, one buffer at a time), and a rename doesn't get special treatment there.
- **Go-to-definition's UI shape**: mirrors `RequestCodeActionsAtPoint`'s async/staleness-guard
  shape (`definitionRequestGeneration_`, a stale response — buffer/point changed, or a newer
  request already superseded it — discarded rather than surprising the user). Zero locations
  reports "No definition found."; exactly one jumps directly with **no confirmation** (unlike a
  code action or a rename, opening a file and moving point is trivially undoable/re-navigable,
  nothing destructive to confirm — matches `VisitSearchResult`'s own precedent for jumping into a
  project file); more than one (a real, if less common, case — e.g. a virtual/overridden method
  with several implementations) enters `LspGotoDefinitionSelect`, the exact same numbered-list/
  Up-Down/digit-jump interaction `LspCodeActionSelect` already established.
- **Rename's UI shape**: a new hybrid not seen elsewhere in this codebase — a synchronous prompt
  stage (`LspRenameNewName`, routed through the existing `HandlePromptKey`, same as
  `FindFile`/`CreateDirectory`/etc.; excluded from Tab-completion, like `StringRectangle`/
  `SetHeadlineTags`, since there's nothing meaningful to complete a new symbol name against)
  followed by an async request/confirm stage (`RequestRenameAtPoint`/`LspRenameConfirm`) once
  Enter is pressed on the new name. `RefreshRenameConfirmStatus` shows a summary
  (`"N edits across M files"`) computed once when the response arrives, not recomputed per
  keystroke — there's nothing to recompute it *for*, unlike `RefreshCodeActionSelectStatus`'s own
  per-keystroke Up/Down refresh.

Verification: full suite (1064 test cases, 18 new — `LspContentTest.cpp`'s
`ExtractDefinitionLocations`/`ExtractRenameEdits` parsing coverage including the `LocationLink`/
multi-URI/`documentChanges` cases, `LspManagerTest.cpp`'s real request/response round-trips for
both `RequestDefinition` and `RequestRename` via the same injected-`FakeServer` fixture slice 2/3
built, and `BufferViewTest.cpp`'s end-to-end coverage of direct-jump/select-among-many/no-results
go-to-definition and prompt-then-confirm/decline multi-file rename) and `./test-asan.sh` — clean.

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
        - **Open architectural question, unresolved:** does the current LSP client
          support more than one concurrent server per buffer? If it's 1:1 today (needs
          checking in `Source/Editor/Lsp*` before this is scoped further), then the
          real work here is multi-server-per-buffer diagnostics merging, not adding a
          checker — that's the actual size of this item, not "just another server
          config entry."
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
  - [ ] VCS-agnostic version control, via a plugin system rather than a hardcoded git
        integration. The user's own framing: stay deliberately agnostic about which VCS
        a project uses — a small, internally-understood vocabulary of operations
        (status, diff, blame, stage/unstage a hunk, commit, log, branch, ...) that a
        plugin translates into whatever a specific VCS actually needs, so the editor-facing
        commands/keybindings/UI stay the same regardless of git vs. Mercurial vs.
        Subversion vs. jj vs. whatever else a project happens to use. Plugins are Janet
        scripts — the natural fit given this project's own "everything is
        programmable, Janet fills Elisp's role" foundation (`Source/Janet/`) rather than
        a new, separate plugin-language/runtime built just for this. Explicitly framed by
        the user as generalizable past version control once the mechanism exists: the same
        "translate a common internal vocabulary into a specific external tool's actual
        calls, via a Janet-scriptable plugin" shape could later cover cloud-provider CLIs,
        Terraform, Docker, or other external tooling entirely — version control is the
        first, most concrete case to design against, not the only one this is meant for.
        Needs a real design pass of its own (the vocabulary itself, how a plugin
        registers/is discovered, how UI surfaces like a status panel or diff gutters stay
        VCS-agnostic when the underlying data shapes genuinely differ across tools) before
        any of it is scheduled — flagged here as a real, sequenceable direction, not
        design-complete.
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
  - [ ] Built-in terminal panel.
  - [ ] Task runner (build/test tasks from within the editor).
  - [ ] Remote development (SSH remote editing).
  - [ ] Per-file session persistence: remember each file's last point/scroll position
        (keyed by absolute path) across restarts, restored on `find-file`/`ned <path>`,
        persisted under `$XDG_STATE_HOME/ned/` per this project's own XDG convention.
        Raised alongside the buffer-switch scroll-clamping fix (`BufferView::topLine_`
        is currently BufferView-level, not per-buffer) — a real prerequisite would be
        moving last-viewed position to live per-`Buffer` first, which this would then
        also persist to disk; scoped as its own slice rather than folded into that fix.
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

## Decisions made during Phase 0/1

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

- `cmake-build-debug/` (the CLion-managed Ninja tree) still has a cache generated from
  the pre-Phase-0 `CMakeLists.txt`. CLion will reconfigure it automatically on next open;
  it wasn't touched here to avoid interfering with IDE-managed state.
- Build/test verified via `build/` (Unix Makefiles): `cmake -S . -B build && cmake --build build`,
  then `ctest --test-dir build`. Sanitizer opt-in verified separately with
  `-DNED_ENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug`.
