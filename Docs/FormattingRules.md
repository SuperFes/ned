# Formatting

How ned decides indent style, what's bundled as a safe default, and how to override it for
your own project or personal taste. Design rationale and the full nine-rule-kind catalogue
(what a complete configurable formatter needs, sorted into what's shipped/planned/out of
scope): `Docs/FormattingCapabilities.md`. This page is the settings reference -- `Themes.md`'s
role, for the formatter.

## What's here today

Structural indentation, per language, via a compiled-in safe default plus your own
overrides, and a Hygiene pass (trailing-whitespace strip, blank-line-run collapse, final
newline). `format-buffer` (`C-c f f`) and the headless `ned --format <files...>` both run
the same chain: your configured external formatter if one's set
(`ned/set-format-command`), falling back to ned's own native reindent + Hygiene pass
whenever no external formatter is configured *or* it fails at runtime -- so `format-buffer`
always does something useful, never just "nothing configured."

Per-capture-name `:space`/`:break` rule *storage and resolution* also exists today
(`Editor/FormatRules.h`, `format.janet`'s `:space`/`:break` keys, `ned/set-format-*`) --
see "Space and Break rules" below. Both rule kinds have a live pilot pass (brace placement
and if/while condition-paren spacing), each with two languages (cpp, JavaScript) sharing
one rule set via a common capture name; every other capture/language is still inert.

A capture-scoped per-construct indent override (`ned/set-indent-rule`) is planned but not
built yet -- see `Docs/FormattingCapabilities.md` and `ROADMAP.md`. LSP-based formatting is
not part of this chain either (`format-buffer` has no request/response machinery of its
own) -- `save-buffer`'s own, separate LSP-format-on-save path is unaffected either way.

## The Hygiene pass

Three rules, each independently configurable and each already the default for an ordinary
`save-buffer` (that's `TrimOnSave.h`/`FinalNewline.h` -- disk-only, unrelated to this
section except for sharing the same underlying text transforms):

- **Trailing whitespace** -- strips spaces/tabs from the end of every line, and collapses
  any run of blank lines at the very end of the document to nothing.
  `ned/set-trim-trailing-whitespace-on-save` (default on).
- **Blank-line runs** -- collapses a run of more than N consecutive blank lines anywhere in
  the document down to exactly N. `ned/set-max-consecutive-blank-lines` (default 2; a
  negative value disables the rule). A deliberately coarse, global rule -- the full
  per-construct-anchored version (different limits around a class vs. inside a function
  body) needs a `format.scm` per language and is out of scope for now, see
  `Docs/FormattingCapabilities.md`.
- **Final newline** -- ensures the document ends with exactly one `\n`.
  `ned/set-ensure-final-newline` (default on).

Unlike the save-time defaults, this pass edits the live buffer directly, as one undo step --
so `format-buffer`'s Native fallback shows you the cleaned-up result immediately, without
needing a save first. It runs as part of `format-buffer`/`--format`'s own Native step
(alongside the per-language reindent), never as part of an ordinary `save-buffer` -- that
command's own disk-only trim/final-newline default (unaffected by any of this) is where
`ned/set-trim-trailing-whitespace-on-save`/`ned/set-ensure-final-newline` already applied
before this pass existed, and still does.

## Per-language indent defaults

Every bundled language gets a safe default -- sourced from that language's own canonical
formatter or style guide where one exists (PEP 8, rustfmt, PSR-12, gofmt, Prettier as the
de facto ecosystem standard, ...), not tuned to any one person's taste. A handful have no
single canonical convention at all; those are labeled as a judgment call below rather than
dressed up as settled fact, and are the most likely settings you'll want to override.

| Language | Style | Width | Source |
|---|---|---|---|
| Python | spaces | 4 | PEP 8 |
| Rust | spaces | 4 | rustfmt default (non-configurable in stable rustfmt) |
| Go | tabs | 4 (display only) | gofmt mandates tabs; it has no width knob at all |
| PHP | spaces | 4 | PSR-12 §2.2 |
| JavaScript / TypeScript / TSX | spaces | 2 | Prettier default, de facto ecosystem standard |
| JSON | spaces | 2 | matches the JS/npm ecosystem convention |
| YAML | spaces | 2 | **the YAML spec forbids literal tabs for indentation at all** -- overriding to tabs produces invalid YAML |
| HTML / XML / CSS | spaces | 2 | common web-ecosystem convention |
| Java | spaces | 4 | most common convention (Google style uses 2; Oracle/IntelliJ-default/Android use 4) -- judgment call |
| C# | spaces | 4 | Microsoft's own default `.editorconfig`/conventions |
| Kotlin | spaces | 4 | official Kotlin coding conventions |
| Ruby | spaces | 2 | community Ruby Style Guide / RuboCop default |
| Bash / Fish | spaces | 2 | Google Shell Style Guide |
| C / C++ | spaces | 4 | **no single canonical convention exists** -- 4 is the more common cross-ecosystem pick, a judgment call |
| Lua | spaces | 2 | most common community convention |
| TOML / CMake / HCL / Nix | spaces | 2 | common per-ecosystem convention (Cargo.toml style, cmake-format, terraform fmt, nixfmt) |
| Clojure / Janet / jank | spaces | 2 | Lisp community convention (Emacs `lisp-indent-function` default) |
| R | spaces | 2 | tidyverse style guide |
| Make | tabs | 4 (display only) | GNU Make *requires* a literal tab to introduce a recipe line -- a syntax rule, not a style preference |
| SQL | spaces | 4 | common convention -- judgment call |

Markdown and Org aren't in this table: both use their own hand-rolled hanging/outline
indent (a bullet's continuation lines up under its own content column, not a fixed
width), not this engine's flat-width model.

The full table, with each entry's own one-line source citation, lives in
`Source/Editor/IndentDefaults.cpp`.

## Overriding it

**From `init.janet`** (interactive editor only):

```janet
(ned/set-indent-style "python-mode" false 4)   # mode name, tabs?, width
```

This always wins over everything below -- `init.janet` loads after both `format.janet`
tiers, per the precedence order below.

**From `format.janet`** -- a plain data file, not Janet code (see "Why not JSON/YAML" below),
read by both the interactive editor and the headless `--format` CLI:

- **Personal**: `$XDG_CONFIG_HOME/ned/format.janet` (falls back to `~/.config/ned/format.janet`)
- **Project**: `<root>/.ned/format.janet`

```janet
# .ned/format.janet -- committed to the repo, applies to everyone on this project
{:indent {:python {:width 2}     # this team writes 2-space Python, not PEP 8's 4
          :go     {:width 8}}    # display width only -- gofmt's own tabs are unaffected
 :trim-trailing-whitespace true
 :ensure-final-newline true
 :max-consecutive-blank-lines 1} # stricter than the built-in default of 2
```

Run `reload-format-config` (`M-x`) to re-read both files without restarting ned -- the
point is a fast loop: edit `format.janet`, reload, `format-buffer`, repeat until it's right.

### Precedence

**`init.janet` (interactive only) > project `format.janet` > personal `format.janet` >
built-in per-language default.**

Resolution is a **per-field cascade**, not a per-file wholesale replace (git config's model,
not `clang-format`'s original nearest-file-wins-wholesale one): a project file that sets
only `:width` for one language still inherits everything else -- including that same
language's `:tabs` setting -- from your personal file, which in turn inherits from the
built-in table. A project's checked-in style only needs to state what the team actually
cares about; anything it's silent on falls through to your own taste, which is the entire
reason a personal layer exists.

### Why not JSON/YAML/TOML

`format.janet` is ned's own plain Janet-*data* syntax (structs, keywords, strings, booleans,
`#` comments -- no code, no VM) -- the same format `language.janet` already uses. Kept
deliberately consistent with the rest of ned's config surface rather than introducing a new
file format and a new dependency for one feature.

**Not trust-gated**, unlike `.ned/init.janet` and a project's own `language.janet`: its
schema is bool/int/keyword-enum leaves only, with no field naming a shell command,
executable path, or anything else interpretable as code -- the same reasoning that leaves a
user's own `$XDG_CONFIG_HOME/ned/languages/` directory ungated. If this schema ever grows
such a field (an external formatter override, say), that field has to move onto the trust
allowlist first -- see `Source/Editor/FormatConfigParse.h`'s own tripwire comment. An
untrusted project's `format.janet` still applies silently today; that's intentional, not an
oversight, and is worth knowing if you `cd` into an unfamiliar repo.

### Schema

Top-level keys, and the fields inside each `:indent`/`:space`/`:break`/`:blank` entry.
`:indent` is keyed by a language key (`python`, `cpp`, the same key the defaults table
above and `ned/set-lsp-command` both use). This list is held against the real schema on
every build:

<!-- format-keys:begin -->

`indent` `space` `break` `blank` `trim-trailing-whitespace` `ensure-final-newline`
`max-consecutive-blank-lines` `tabs` `width` `before` `after` `within` `placement`
`collapse-empty` `collapse-simple` `min-before` `max-before`

<!-- format-keys:end -->

## Space and Break rules, and overriding them per language

`:space` (kind 2 of `Docs/FormattingCapabilities.md`'s nine-rule-kind catalogue) and
`:break` (kind 3, with brace placement folded in as its specialised case -- placing
`else`/`while`/`catch` on a new line after a closing `}`, K&R vs. Allman brace style, and
so on) are both keyed by **capture name**, not language -- a dotted identity a language's
own `format.janet` query names (`control.parens`, `brace.function`, ...), the same
vocabulary `highlights.janet`/`tags.janet` already use for their own capture names. A bare
capture name is the shared rule every language with that capture gets; prefixing it with
`"<language>/"` (the same key IndentDefaults.cpp's table uses) narrows the rule to one
language's own quirk, without touching what every other language's use of that capture
resolves to -- `Editor/FormatRules.h`'s own resolution shape, mirroring
`SyntaxTheme.h`'s per-capture-name style overrides (`ned/set-capture-*`) exactly.

```janet
{:space {"control.parens" {:before true :after false}     # shared: every language's if/for/while parens
         "cpp/control.parens" {:before false}}             # cpp's own override -- no space before
 :break {"brace.function" {:placement :next-line            # Allman, not this language's usual K&R
                            :collapse-empty true}}}
```

`:space` entries: `:before`/`:after`/`:within` (true/false) -- whether a space is inserted
before, after, or just inside the captured token/delimiter pair. `:break` entries:
`:before`/`:after` (true/false, a mandatory or forbidden newline at that point),
`:placement` (`:same-line`/`:next-line`/`:next-line-indented` -- K&R/Allman/
GNU-Whitesmiths, meaningful only on a brace-carrying capture), `:collapse-empty`/
`:collapse-simple` (true/false -- keep an empty or single-statement block on one line).
`:next-line-indented` also repositions the CLOSING delimiter to match the opening one's
new (deeper) column -- the one placement whose closer doesn't align with the header's own
indent the way `:same-line`/`:next-line`'s already does, found live as a real mismatched-
brace bug during a 2026-09-15 audit and fixed before it was ever the default for anything.
Skipped (left alone) when the closer shares its line with real content, deferring to
`:collapse-empty`/`:collapse-simple` for that case instead of guessing at it.

**Five languages exist today: cpp, JavaScript, Java, Python, and Go**
(`Source/Languages/{cpp,javascript,java,python,go}/format.janet` -- the same capture NAMES
throughout, over each grammar's own different node types: cpp's
`compound_statement`/`condition_clause`, JavaScript's
`statement_block`/`parenthesized_expression`, Java's `block`/`parenthesized_expression`,
Go's `block`/`parenthesized_expression`), all wired into `format-buffer`'s and `--format`'s
Native chain (reindent, then Blank, then Break, then Space, then Hygiene) and all shipping
no built-in default -- neither does anything until you configure a rule:

- **Break-kind captures** (`Editor/FormatBracePlacement.h`'s `ComputeBracePlacementEdits`,
  reading every `:break` field): `brace.function` (a function definition's own body),
  `brace.control` (an `if`/`while`/`for`/`switch`/`catch` statement's own body -- one
  shared name, matching JetBrains' own "Other statements and blocks" grouping),
  `brace.class` (a class/struct body), and, cpp only, `brace.namespace`.
  ```janet
  (ned/set-format-brace-placement "brace.function" "next-line")
  (ned/set-format-brace-placement "brace.control" "same-line")
  (ned/set-format-brace-collapse-empty "brace.function" true)
  (ned/set-format-brace-collapse-simple "brace.control" true)
  ```
- **Space-kind captures** (`Editor/FormatSpacing.h`'s `ComputeSpaceEdits`, reading all
  three `:space` fields; deliberately never touches a whitespace run that crosses a
  newline -- a Space rule never second-guesses wherever a line break already is):
  `control.parens`, covering `if`/`while`/`switch`/`for`'s own condition and a `catch`
  clause's own parameter parens, in all three languages.
  ```janet
  (ned/set-format-space-before "control.parens" true)
  (ned/set-format-space-after "control.parens" true)
  ```

**Paired-delimiter captures.** `if`/`while`/`switch`'s own condition each have one grammar
node spanning the whole `(...)` -- a capture's own first/last byte simply ARE the delimiter
pair. A `for` loop's `(init; condition; update)` doesn't: it's three independent,
individually-optional fields around bare anonymous `(`/`)` tokens, no single node to
attach a capture to. Neither does JavaScript's own `catch` clause (a bare `parameter:`
field, no wrapping parens node the way cpp's `parameter_list` is -- and ES2019+ allows a
parameter-less `catch { }` besides). Rather than decline these outright, a query can
capture the open and close tokens directly, as a **pair**:

```janet
(for_statement "(" @control.parens.open ")" @control.parens.close)
```

`Mode.cpp`'s `formatCaptures` closure correlates a `"<name>.open"`/`"<name>.close"` pair
found in the *same pattern match* -- never across two different constructs, verified live
against two adjacent for-loops and a for-loop with a nested function call in its own
condition before this shipped -- into one synthesized capture spanning open to close,
indistinguishable from `if`/`while`'s own whole-span capture to `ComputeSpaceEdits` or any
other consumer. A pattern that simply doesn't match (JavaScript's parameter-less `catch`)
contributes nothing, not a false capture -- also verified live.
  Combined in one `format.janet`, with cpp's own brace-placement exception:
  ```janet
  {:break {"brace.function" {:placement :same-line}       # the shared rule (JavaScript gets this)
           "cpp/brace.function" {:placement :next-line}}  # cpp's own override
   :space {"control.parens" {:before true}}}              # every language's if/while parens
  ```
  Formatting a `.cpp` and a `.js` file with this one config genuinely produces two
  different brace styles: cpp's function gets Allman (its own override), JavaScript's
  gets K&R (the shared rule) -- both get the same `if (x)` spacing.

**Collapse-empty and collapse-simple.** Two more `:break` fields, both consumed by
`Editor/FormatBracePlacement.h`, both composing freely with `:placement` and with each
other (an empty body and a single-statement body are mutually exclusive by construction,
so a capture only ever takes one path):

- **`:collapse-empty`** is purely textual -- whitespace-only content between the two
  delimiter bytes is empty regardless of language, no tree needed. `true` glues an
  expanded empty body onto one line (`{\n}` -> `{}`); `false` forces an already-glued one
  apart, at whatever indent `:placement` would put its closer at (defaulting to the
  header's own indent with no `:placement` configured at all).
- **`:collapse-simple`** needs a real structural fact no text scan can safely
  answer -- "does this body have exactly one top-level statement" -- so it comes from the
  query, not from counting `;` characters (which a nested block, a string, or a for-loop's
  own semicolons would trip up). A **`"<name>.simple"` marker capture**, anchored to
  "exactly one named child" via tree-sitter's `.` (immediate-sibling) anchors:
  ```janet
  (function_definition body: (compound_statement . (_) .) @brace.function.simple)
  ```
  verified live to answer correctly regardless of what that one statement itself contains
  (a nested block, an if with its own block, ...) and to correctly report "not simple" for
  both a multi-statement body and an empty one. `Mode.cpp`'s `formatCaptures` closure
  correlates the marker against the base `brace.*` capture sharing its exact byte range
  (`FormatCapture::isSimple`) and never emits the marker as a capture in its own right.
  `true` joins an expanded single-statement body onto one line; `false` expands a one-line
  one. Both directions are declined -- left alone, not force-reflowed -- when the
  statement itself already spans more than one physical line, so this never risks joining
  or breaking something a human deliberately wrapped (a long call, a comment).

No other bundled language has a `format.janet` yet, and no capture yet reads `:within` on
an empty pair. This is the proof that the full chain (query -> `Mode::formatCaptures` ->
`FormatRules` resolution -> a computed edit -> applied to a live buffer) works end to end
for both rule kinds AND across three brace-carrying languages sharing one rule set with
per-language exceptions, ahead of rolling the remaining rule kinds/languages out (see
`Docs/FormattingCapabilities.md`'s Tier B1). Java's own for-loop allows several
comma-separated init/update expressions, unlike cpp/JavaScript's single ones -- verified
live that the paired `"("`/`")"` capture still finds the outer pair regardless.

**Python has no brace-delimited bodies at all.** Verified against tree-sitter-python's own
`node-types.json`, not assumed: `function_definition`, `if_statement`, `while_statement`,
`for_statement`, `class_definition`, `try_statement`'s `except`/`finally` clauses,
`with_statement`, and `match_statement`'s `case_clause` all use a bare `block` field with
no wrapping delimiter tokens whatsoever -- indentation alone marks a body's extent.
`ComputeBracePlacementEdits` hardcodes `text[capture.startByte]`/`text[capture.endByte-1]`
as single literal delimiter characters to reposition or splice, so `brace.function`/
`brace.control`/`brace.class`, the paired-capture mechanism, and `:collapse-empty`/
`:collapse-simple` are all structurally inapplicable to Python, not merely unconfigured --
`Source/Languages/python/format.janet` names none of them, and a `:break` rule configured
against any of those names is a verified no-op on a Python buffer (nothing to act on, not
a silent failure). The one construct that still fits the template: `if`/`while`'s own
condition is a plain `expression` field, not a required-parenthesized one, but the grammar
still allows a user to write `if (x):` anyway, which parses as a real
`parenthesized_expression` node -- verified live this only matches when parens are
actually present in the source. `control.parens`'s `:space` rules are therefore a real,
if narrow, lever for Python (keeping spacing inside a project's redundant condition parens
consistent). This is the moment the brace-based template runs out -- Python's own real
formatting need (PEP8's blank-lines-before-`def`/`class`) is the Blank rule kind's
territory instead, covered next.

The same rules are settable live from `init.janet`, per-field, mirroring
`ned/set-capture-*`'s own shape:

```janet
(ned/set-format-space-before "cpp/control.parens" false)
(ned/set-format-brace-placement "brace.function" "next-line")
```

## Blank lines, and overriding them per language

`:blank` (kind 6 of `Docs/FormattingCapabilities.md`'s catalogue -- "two independent
halves, both needed: maximum preserved and minimum enforced") reads
`Editor/FormatRules.h`'s `BlankRuleValue` the same capture-name-keyed way `:space`/`:break`
do, via `Editor/FormatBlankLines.h`'s `ComputeBlankLineEdits`, wired into the Native
chain FIRST (before Break/Space -- see that chain's own comment in `Commands.cpp` for why
running order matters here). Deliberately **"before" only**, and deliberately int-valued
rather than bool:

- **`:min-before`** -- the minimum blank lines required immediately above a capture's own
  line, enforced by inserting more if there are too few.
- **`:max-before`** -- the maximum blank lines preserved immediately above it, enforced by
  removing any excess.

```janet
(ned/set-format-blank-min-before "def.toplevel" 2)
(ned/set-format-blank-max-before "def.toplevel" 2)
(ned/set-format-blank-min-before "def.method" 1)
```

**Why "before" only.** JetBrains' own UI frames some rules as "around X" (its own wording
for "between two definitions of the same kind"). Expressing that as a *pair* of
independent `:min-after`-on-the-first-capture / `:min-before`-on-the-second-capture rules
would let two adjacent captures' edits collide at the exact same gap between them -- the
same class of coincident-edit bug `:within` on a genuinely empty delimiter pair already
surfaced once (`Editor/FormatSpacing.cpp`'s own fix). `:min-before` on the *following*
capture alone says the same thing with no possible collision, so that is the only knob
this kind exposes.

**The "first in its container" exception.** `:min-before` is skipped outright when a
capture is the first named child of its own immediate container (`Mode.cpp`'s
`"<name>.first"` marker convention, `FormatCapture::isFirst` -- the same correlation
mechanism `:collapse-simple`'s `"<name>.simple"` marker already established, applied to a
`.`-anchored tree-sitter query instead: `(block . (function_definition) @def.method.first)`).
There is nothing above such a capture to separate from but the container's own opening
line -- forcing a blank line directly under `class C:` before its first method is exactly
the behavior most style guides (and JetBrains' own, usually-off "before first method"
toggle) reject. Verified live against seven real shapes before this shipped: a module-
level def with nothing above it (first), a second module-level def (not first), a
decorated def/class (the marker fires on the *outer* `decorated_definition` node, so the
blank line lands above the decorator, matching where the base capture itself starts), a
class's first method with no docstring (first), the same class *with* a leading docstring
(NOT first -- a docstring is a real preceding sibling, not a transparent extra, so the
conservative "decline rather than guess" rule collapse-simple already set carries over
here unchanged), a decorated first method (first, decorator included), and a comment
immediately before a first method (still first -- comments are declared `extras` in
Python's own grammar, so a `.` anchor is correctly transparent to them). `:max-before` is
**not** gated on `isFirst` -- unlike the minimum, JetBrains' "keep maximum blank lines" is
an unconditional cap applied everywhere, so three blank lines hand-typed directly under a
class header are still trimmed down to whatever `:max-before` says, even for that class's
own first method.

**Python is the pilot, and currently the only language with a `:blank` capture.**
`def.toplevel` (a `function_definition`/`class_definition`/`decorated_definition` that is
a direct child of the module) and `def.method` (the same, but a direct child of a class's
own `block`) are exactly PEP8's own two rules -- "surround top-level function and class
definitions with two blank lines" and "methods inside a class are surrounded by a single
blank line." Nothing about `:blank`'s own mechanism is Python-specific, though: any
language's capture -- `brace.function`, `control.parens`, anything a `format.janet` names
-- can carry a `:blank` rule the same way, the moment a future language's own file adds
one.

## Go: a real correctness hazard, not just a style question

Go is the fifth language and the first one where a Break-kind rule can silently produce
code that no longer **compiles**, not merely code in a different style -- verified live
with a real `go build`, not assumed. Go's spec performs automatic semicolon insertion
(ASI) after a `)` token at end-of-line; a function header on its own line followed by `{`
on the next fails with `syntax error: unexpected semicolon or newline before {`, because
the inserted semicolon terminates the declaration before the brace is ever reached.
Every OTHER ASI-adjacent language already in this template is unaffected -- JavaScript's
own ASI does not fire after `)`, so `function f()\n{` parses (and compiles) fine there,
checked against the live grammar the same way.

Because `:break` rules are shared-by-default across every language naming the same
capture (the whole point of the language-scoped-override design), a project's
`(ned/set-format-brace-placement "brace.function" "next-line")` -- written with cpp/Java
in mind -- would otherwise apply to Go's own `brace.function` capture too, and silently
break every Go buffer it touches. `Editor/FormatBracePlacement.cpp`'s
`PlacementUnsafeForLanguage` neutralizes a `:next-line`/`:next-line-indented` placement
for language key `"go"` specifically, treating it as though `:placement` were never set --
`:same-line` (Go's only legal brace style) and every other field (`:collapse-empty`,
`:collapse-simple`, `:before`/`:after`) are completely unaffected, scoped or not.
Belt-and-suspenders: Go source already SHAPED like a `:next-line` rewrite (brace on its
own line) is invisible to the query engine independently of the guard too -- tree-sitter-go
performs the same ASI the real compiler does, so `function_declaration`'s own `body:`
field simply never resolves against such a file, and `mode.formatCaptures` returns zero
captures for it. Neither mechanism alone was assumed sufficient without checking; both
were verified live before shipping.

`go/format.janet` otherwise follows the same template as every prior language --
`brace.function`/`brace.control`/`brace.class` (the last reaching one level deeper into
`struct_type`'s nested `field_declaration_list`, since `struct_type`'s own span starts at
the `struct` keyword rather than at `{`), plus a Go-only `brace.interface` (its own
distinct name, the same precedent cpp's `brace.namespace` set, since `interface_type` has
no nested wrapper node at all and needs the paired-delimiter mechanism). `switch`/`select`
also need the paired mechanism for their own outer braces (`expression_switch_statement`/
`type_switch_statement`/`select_statement` have no field OR wrapper node naming their own
body), which is why they get no `.simple` marker -- there is no single node to anchor a
`.`-anchor against, the same reason a for-loop's own clause has no paired-parens capture
in any language. `control.parens` is the same narrow lever Python's own file has: Go's
idiomatic style omits condition parens entirely (`if x {`, `switch x {`), but the grammar
still allows writing them, and only then does the capture fire. Go's own for-loop clause
has no wrapping parens in the grammar AT ALL (writing them is a syntax error, not merely
non-idiomatic) -- confirming, independently of the ASI question, that "verify what a
grammar actually allows" keeps paying off language after language.

One `.simple`-marker bug caught by the test suite before this shipped, not by inspection:
Go's `block` wraps its statements in exactly one intermediate `statement_list` child
(`{ (statement_list ...) }`, at most one -- an empty block has none at all), unlike every
other language's block-shaped node, whose own children ARE the statements directly. A
first attempt anchoring `.` against `block` itself reported `isSimple` for a
TWO-statement body, because `block` always has exactly one child (the whole list)
regardless of how many statements are inside it. Fixed by anchoring one level deeper,
against `statement_list`'s own children, while still capturing the outer `block` (so the
marker's byte range matches the base capture's) -- `(function_declaration body: (block
(statement_list . (_) .)) @brace.function.simple)`.

## The `--format` CLI

```sh
ned --format file1.py file2.js
```

Headless -- no Notcurses, no event loop, no `init.janet` (format.janet only, both tiers).
Runs your configured external formatter if one's set (`ned/set-format-command` -- but see
the note below), falling back to a native per-language reindent plus the Hygiene pass when
none is configured or the external one fails. Whole-file only: a fresh headless process has
no edit history to scope a smaller pass against.

**External formatter configuration is Janet-only today**, so it has no effect in headless
mode -- `--format` never loads `init.janet`, and `ned/set-format-command` has no other way
to be set. The fallback check stays in the chain anyway, both so it reads honestly as
"external, then native" and so a future `format.janet` field for an external command (which
would need its own trust-gate entry first, being a shell command) lights it up for free.
Until then, `--format` always runs the native reindent.

Exit code is non-zero if any file couldn't be formatted (missing, a directory, unreadable,
...); everything else is still attempted.
