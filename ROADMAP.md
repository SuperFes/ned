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

## Phase 9 — Zed-inspired features (aspirational, unsequenced)
A running wishlist, not yet prioritized or scheduled against the phases above — draw
from this once the Emacs-parity core (Phases 1–5) is solid, per the "editing features
before extras" guiding principle. Grouped by how big a foundational lift each is:

- **Language intelligence**
  - [x] Tree-sitter-based syntax highlighting — done (see "Tree-sitter foundation",
        "Mode/highlighting redesign for tree-sitter", and "Bundle remaining tree-sitter
        grammars" above); likely a prerequisite for most of the rest of this group.
  - [ ] LSP client: autocomplete, diagnostics, go-to-definition, hover docs, code
        actions, rename, multi-language support.
  - [ ] DAP (Debug Adapter Protocol) client for in-editor debugging.
  - [ ] Structural/AST-aware selection expansion (expand-to-next-syntax-node).
- **Navigation & search**
  - [ ] Fast fuzzy file finder / command palette (a visual layer over the Phase 2
        command-completion machinery + a project file index).
  - [ ] Multibuffers: a virtual buffer stitching together excerpts from multiple
        files/locations (e.g. all references, all diagnostics, as one scrollable view)
        — a genuinely interesting fit for our Rope/Buffer design, worth a design pass
        of its own when it comes up.
- **Version control**
  - [ ] Git integration: inline blame, diff gutters, hunk staging, a git status panel.
- **Collaboration & AI**
  - [ ] Real-time collaborative editing (CRDT-based shared sessions) — the biggest
        lift in this list; revisit only once the single-user core is solid.
  - [ ] AI-assisted editing (inline completion, chat with codebase context) — natural
        fit for Janet given "everything programmable," likely implementable as a Janet-
        scriptable integration rather than something hardcoded in C++.
- **Editor ergonomics**
  - [ ] Multiple cursors / multi-cursor editing.
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

## Companion tooling: environment setup + tree-sitter-assisted formatter (planned, unsequenced)

Two standalone utility programs shipped alongside `ned`, not part of the editor binary
itself — the user's own framing: "towards the end of our dev, maybe they even belong in
their own phase, when we're starting to setup the outside tooling." Aspirational, like
Phase 9 above — not scheduled against any phase, revisit once the editor core itself is
solid.

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
