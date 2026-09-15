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

**Sixteen languages exist today: cpp, JavaScript, Java, Python, Go, PHP, Rust, C#,
TypeScript, TSX, Kotlin, C, Bash, Lua, and Fish**
(`Source/Languages/{cpp,javascript,java,python,go,php,rust,csharp,kotlin,c,bash,lua,fish}/format.janet`
-- the same capture NAMES throughout, over each grammar's own different node types: cpp's
`compound_statement`/`condition_clause`, JavaScript's
`statement_block`/`parenthesized_expression`, Java's `block`/`parenthesized_expression`,
Go's `block`/`parenthesized_expression`, PHP's
`compound_statement`/`parenthesized_expression`, Rust's
`block`/`parenthesized_expression`, C#'s `block`/paired anonymous parens tokens, Kotlin's
`function_body`+a text predicate/paired anonymous parens tokens, C's
`compound_statement`/`parenthesized_expression` (a DIFFERENT node type from cpp's own
`condition_clause`, despite the grammars' close relationship), Bash's `function_definition`/
paired KEYWORD tokens (`"do"`/`"done"`, `"then"`/`"fi"`, `"in"`/`"esac"`) plus a subshell/
group-command's own `"("`/`")"`/`"{"`/`"}"`, Lua's paired KEYWORD tokens (`")"`/`"end"`,
`"do"`/`"end"`, `"then"`/`"end"` -- no brace/paren delimiter anywhere in the grammar at all,
see below), Fish's `begin_statement` alone, in two forms (`"begin"`/`"end"` or `"{"`/`"}"`
-- no delimiter of any kind for if/while/for/switch/function, see below); TypeScript
and TSX have no `format.janet` files of their own at all, see below), all wired into
`format-buffer`'s and
`--format`'s Native chain (reindent, then Blank, then Break, then Space, then Hygiene) and
all shipping no built-in default -- neither does anything until you configure a rule:

- **Break-kind captures** (`Editor/FormatBracePlacement.h`'s `ComputeBracePlacementEdits`,
  reading every `:break` field): `brace.function` (a function/method/constructor's own
  body, AND -- since the 2026-09-15 coverage audit's policy reversal, see below -- any
  anonymous function/lambda/closure body too), `brace.control` (an `if`/`while`/`for`/
  `switch`/`do`-`while`/`try`/`catch`/`finally` statement's own body, plus each language's
  own extra shapes -- static/instance initializer blocks, `using`/`lock`/`unsafe` blocks,
  `switch`/`match` expressions -- one shared name, matching JetBrains' own "Other
  statements and blocks" grouping), `brace.class` (a class/struct/enum/impl/anonymous-class
  body), `brace.interface` (Go/PHP/Rust's own interface/trait body), and `brace.namespace`
  (cpp's own namespace/`extern "C"` block, Rust's own module).
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

**Python is the pilot; all seven languages now name `def.toplevel`/`def.method`.**
`def.toplevel` (a `function_definition`/`class_definition`/`decorated_definition` that is
a direct child of the module) and `def.method` (the same, but a direct child of a class's
own `block`) are exactly PEP8's own two rules -- "surround top-level function and class
definitions with two blank lines" and "methods inside a class are surrounded by a single
blank line." Nothing about `:blank`'s own mechanism is Python-specific: every other
language's `format.janet` now names the same two capture NAMES, over whatever its own
grammar's equivalent shapes are:

- **cpp**: `def.toplevel` covers a free function, `class`/`struct`, AND a
  `template_declaration` (captured itself, not its own inner function/class -- the same
  "capture the outer wrapper" precedent Python's own `decorated_definition` set); `def.method`
  is an inline-bodied method directly inside a `field_declaration_list`.
- **JavaScript**: `def.toplevel` covers `function`/`class` declarations plus
  `export_statement` (captured itself, same "outer wrapper" precedent) and
  `generator_function_declaration`; `def.method` reads `class_body`'s own `method_definition`
  children (covering ordinary/static/generator/async alike) -- deliberately unqualified by
  field name (TypeScript rollout follow-up, below) rather than JS's own `member:` tag, since
  the pattern is shared verbatim with a grammar that has no such field at all.
- **Java**: `def.toplevel` is TYPE declarations only (`class`/`interface`/`enum`/`record`/
  `annotation_type_declaration`) -- Java has no top-level *functions* the way cpp/
  JavaScript/Python do; `def.method` covers `method_declaration`/`constructor_declaration`
  across all three of Java's distinct body node types (`class_body`, `interface_body`,
  `enum_body_declarations` -- no single shared node the way PHP's `declaration_list` is).
- **Go**: `def.toplevel` covers free functions, receiver methods, AND type declarations
  (struct/interface/alias) alike -- but Go gets **no `def.method` at all**, a real language
  difference rather than a scope cut: a Go method is its own top-level declaration carrying
  a separate receiver, never a node nested inside a struct's own field list.
- **PHP**: `def.toplevel` covers `function`/`class`/`trait`/`interface`/`enum` (not
  `namespace_definition` -- a container, not a definition, the same distinction cpp's own
  `namespace_definition` exclusion draws); `def.method` reads `declaration_list`, the same
  single node type already shared by class/trait/interface bodies for `brace.class`/
  `brace.interface` above.
- **Rust**: `def.toplevel` covers `fn`/`struct`/`enum`/`trait`/`mod`/`impl`; `def.method`
  reads `declaration_list`, shared by `impl`/`trait`/`mod` bodies alike -- deliberately NOT
  distinguishing a real `impl`/`trait` method from a free function merely nested inside a
  `mod` block, since both share the identical node type with no fact at that level to tell
  them apart (the same "close enough to fold together" call this file's own `brace.class`
  already makes for struct+enum+impl).

**The "first in its container" exception has one sharp edge, found in every language that
has one:** a file-level construct that always precedes the first real definition --
Python's own leading `import`, cpp's `#include`, Go's mandatory `package` clause, PHP's
opening `<?php` tag -- is a REAL preceding sibling, so `isFirst` is correctly `false` for
even the very first definition in an ordinary file of any of these languages. Not a bug:
a `:min-before` rule legitimately wants to say something about the gap right after
`package main` or `<?php` too, and verified live in every case rather than assumed to
carry over from Python's own finding.

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

## PHP: a per-construct three-way ambiguity, and one node shared by two syntaxes

PHP is the sixth language, and the first where a SINGLE construct's own body field
accepts three different shapes rather than the two-way ones (brace vs. none) every prior
language had: `{ ... }` (`compound_statement`, what `brace.control`/`brace.function`
capture), PHP's own colon-alternate syntax (`colon_block` -- `if (x): ... elseif (y): ...
else: ... endif;`, with NO braces anywhere in the whole chain), or a single bare unbraced
statement (`if (x) return;`, C's own shape). This applies to `if`/`elseif`/`else`/`while`/
`for`/`foreach` alike -- `else_if_clause`/`else_clause` carry the exact same three-way
`body` field `if_statement` itself has, so a full colon-syntax `if:...elseif:...else:...
endif:` chain produces **zero** `brace.control` matches across every clause, not just the
leading `if`, verified live before this shipped. Requiring the field's own node TYPE to be
`compound_statement` in the query (`(if_statement body: (compound_statement)
@brace.control)`) is what discriminates all three shapes -- a colon-syntax or
bare-statement body simply produces no match, not a false or corrupted one, the same
"decline rather than approximate" precedent every prior language's own unreachable shapes
already set.

**`switch` needed a second, different trick.** PHP's grammar uses a SINGLE node type
(`switch_block`) to represent BOTH its brace form (`{ case ... }`) and its own colon
form (`: case ... endswitch;`) -- there is no second node type here the way
`compound_statement`/`colon_block` split for if/while, so a bare `(switch_block)
@brace.control` capture would sometimes hand `ComputeBracePlacementEdits` a node whose
first byte is `{` and sometimes one whose first byte is `:`, which that pass hardcodes as
a literal brace character to reposition or splice. A real hazard, confirmed live, not a
hypothetical. Fixed with the same paired-delimiter mechanism a for-loop's own clause
already uses: `(switch_block "{" @brace.control.open "}" @brace.control.close)` only ever
matches when the literal `{`/`}` tokens actually exist, so the colon form contributes
nothing -- verified live it produces zero matches, not a false one.

Otherwise the template applies unchanged: `brace.class` covers both `class_declaration`
and `trait_declaration` (a PHP trait is structurally and stylistically a reusable class
body, the same "close enough to fold together" call cpp's own `brace.class` already makes
for structs and classes), `brace.interface` is its own name (the same precedent Go's own
`brace.interface` set), `control.parens` fires on ordinary code rather than a rare
redundant-parens edge case (if/while/switch/elseif's own condition is a REQUIRED
`parenthesized_expression` here, matching cpp/JavaScript/Java's own mandatory-parens
shape, not Python/Go's optional one), and a for-loop's own clause and a catch clause's own
parameter both need the paired mechanism, the same reasons cpp/JavaScript/Java's own files
already document.

## Rust: no new structural hazard, but a real judgment call on scope

Rust is the seventh language, and the first with neither Go's ASI trap nor PHP's per-
construct body ambiguity -- every brace-carrying construct's own `body`/`consequence`
field is a plain, required `block` (or, for `match`, `match_block`), with no second
syntax to discriminate and no compiler behavior a Break rule could silently break.
`rust/format.janet` follows the established template: `brace.function` (`function_item`),
`brace.control` (`if`/`while`/`loop`/`for`/`match`'s own body), `brace.class` (folding
`struct`/`enum`/`impl` together -- a struct's data, an enum's variants, and an impl
block's methods all read as one "type body" grouping, the same "close enough to fold
together" call cpp's own `brace.class` makes for structs and classes, and PHP's for
classes and traits), `brace.interface` (`trait`'s own body, the same precedent Go/PHP's
own `brace.interface` set), and `brace.namespace` (`mod`'s own body -- the same name and
precedent cpp's `namespace_definition` set; a bodyless `mod foo;` file-per-module
declaration has no `body` field at all, verified live, so it never matches). A unit
struct (`struct Unit;`) and a tuple struct (`struct Tup(i32, i32);`) likewise have no
`body` field carrying `field_declaration_list` -- confirmed against node-types.json rather
than assumed -- so neither is ever captured; there is no brace to place.

`control.parens` is the same narrow lever Go/Python's own files are: idiomatic Rust omits
condition parens entirely (`if x {`, `match x {`), but the grammar still allows writing
them (`parenthesized_expression` is one of `_expression`'s own subtypes), and the capture
only fires when they're actually present. The one genuine judgment call: `match`'s own
`value:` field gets `control.parens` (matching JavaScript/Go's own treatment of `switch`'s
condition-like value), but a `for`-loop's own iterable does NOT, even though wrapping it in
parens (`for x in (0..3) {`) parses fine -- verified live, so this is a scope choice, not a
grammar limitation the way Go's whole-clause for-loop omission is. The distinction:
`match`/`switch`/`if`/`while` all test a value as a condition/scrutinee; a `for`-loop's
value merely names a sequence to walk, a different semantic role no prior language's own
grammar happened to offer a redundant-parens lever on at all. A `match_arm`'s own `value:`
(sometimes itself a `block`, `_ => { ... }`) is left uncaptured for the same "no
distinguishing shape to anchor a pattern against" reason -- match's OWN outer braces are
still captured, only the per-arm body is the scope cut.

Rust's `else` branch (`if_expression`'s own `alternative` field) needed no special-casing
at all, unlike PHP's colon-alternate hazard -- an else-if chain resolves itself
clause-by-clause through the same `if_expression`/`consequence` pattern recursing into each
nested `if_expression`, and a trailing bare `else { ... }` is simply never captured, the
same scope cut cpp/JavaScript/Java's own `else_clause` already carries. `block` wraps its
statements directly (no Go-style intermediate `statement_list` wrapper -- confirmed against
node-types.json before writing the `.simple` markers), so every `.simple` marker anchors
the same way cpp/JavaScript/Java's own do, not Go's one-level-deeper anchor.

## C#: mandatory parens that still need the paired mechanism

C# is the eighth language, and the first with a genuine surprise about what "mandatory
parens" implies. Every prior language with required condition parens (cpp/Java/JavaScript)
had them because the condition FIELD itself was typed as a node spanning the whole
`"(...)"` (`condition_clause`/`parenthesized_expression`) -- capturing that field directly
was enough. C#'s `if_statement`/`while_statement`/`switch_statement` condition/value field
is instead a bare `expression`, with the parens as unwrapped anonymous tokens beside it
(confirmed against `grammar.json`'s own rule, not assumed from cpp's shape) -- so even
though writing `if x` with no parens at all is a compile error, `control.parens` still
needs the SAME paired `"("`/`")"` mechanism a for-loop's own clause uses in every language,
not a bare field capture. A genuinely redundant double-paren lever
(`condition: (parenthesized_expression)`, the python/go-style narrow trick) was considered
and declined: it would overlap the outer paired capture's own span rather than sit beside
it the way python/go's version safely does (their languages have no outer paired capture to
begin with), risking the exact "two captures editing the same gap" class of bug `:within`
on an empty delimiter pair already taught a lesson about once.

**One real mistake caught before shipping, not after:** a catch clause's own `"(Type
name)"` is a real named node (`catch_declaration`), and the first guess -- since its
`fields` only name `type`/`name`, nothing about parens -- was that it needed the paired
mechanism too, the same as the condition case above. Wrong: a node's `fields` list names
only its FIELDS, not its whole byte span, and `grammar.json`'s own rule for
`catch_declaration` shows it literally starts with the `"("` token and ends with `")"` --
so the node's own span already covers the whole clause, the same as cpp's named
`parameter_list`, needing no pairing at all. Caught by checking `grammar.json` directly
(not just `node-types.json`'s field summary) before writing the query, and reconfirmed with
a live byte-range check once written.

`class`/`struct`/`record` bodies all share the same `declaration_list` node type already
load-bearing for `brace.class`, `brace.interface` (`interface_declaration`'s own body), and
`brace.namespace` (`namespace_declaration`'s own body) -- one `def.method` pattern covers a
method nested in any of them, the same "one node type, several owners" shape PHP's own
class/trait/interface bodies already established. A positional record
(`record R(int X, int Y);`) has no `body` field at all when it ends in `;` -- confirmed
live -- so it's captured as `def.toplevel` alone, same as every other bodyless top-level
shape in this whole rollout. Verified live via a real `dotnet build`: the formatted output
compiled clean (one pre-existing unused-variable warning, no errors).

## TypeScript and TSX: a shared file, not a new one, and one real cross-grammar surprise

TypeScript is the ninth language, and the first added as a pure DELTA over an existing
file rather than its own from-scratch one. Every shared-with-JavaScript construct
(function/class/if/while/for/switch/catch bodies, `control.parens`, the method side of
`def.method`) uses byte-identical node type names in tree-sitter-typescript and
tree-sitter-javascript, confirmed live before writing anything -- so
`typescript/language.janet`'s own `:queries` entry concatenates
`["javascript/format.janet" "typescript/format.janet"]` directly (the exact mechanism
`:tags` already uses to reuse javascript's own tags query, described above) rather than
duplicating those captures. `typescript/format.janet` itself is small: only what
JavaScript's grammar has no equivalent for at all --
`interface_declaration`/`enum_declaration`/`type_alias_declaration`/
`abstract_class_declaration` widen `def.toplevel` (a BARE, unexported one; `export
interface I {}` is already caught by javascript's own `export_statement` capture, which
wraps the outer node regardless of what it exports), `interface_declaration`'s own body
gets `brace.interface`, and `abstract_class_declaration`'s own body gets `brace.class` --
it's a genuinely distinct node type from `class_declaration`, not a modifier on it, so
JavaScript's own `brace.class` pattern (anchored on `class_declaration` specifically) does
not match it at all. A class member with no body comes in THREE distinct node-type
flavors here, each needing its own `def.method` pattern: `method_signature` (an
interface's own abstract member), `abstract_method_signature` (an abstract class's own),
and the ordinary `method_definition` JavaScript's own file already captures -- confirmed
live all three coexist as siblings inside one `abstract class`'s own `class_body`
(a concrete method right alongside an abstract one). TypeScript's legacy
`namespace N { ... }`/`module N { ... }` syntax (`internal_module`) is deliberately
declined: verified live it parses wrapped in a field-less `expression_statement`, an
unusual enough shape for a feature ES modules have mostly superseded that it wasn't worth
chasing.

**One real cross-grammar mistake, caught by the query failing to COMPILE, not by a wrong
answer:** the first attempt at reusing `javascript/format.janet` unmodified failed outright
-- `unknown field name 'member'` -- because tree-sitter-typescript's own `class_body` has
NO `member:` field at all, despite sharing the `class_body`/`method_definition` node type
NAMES with JavaScript verbatim. Fixed by dropping the field-name qualifier from
`javascript/format.janet`'s own `def.method` pattern (`(class_body (method_definition)
@def.method)` instead of `(class_body member: (method_definition) @def.method)`) --
verified live this changes nothing for JavaScript itself (every `method_definition` inside
a `class_body` IS the `member` field there, so the unqualified match is already exactly as
narrow), while making the SAME pattern text compile and match correctly against both
grammars. The lesson: two grammars sharing a node type's NAME is not a guarantee they share
its FIELD names too.

**TSX inherits from TypeScript, but needs the SAME explicit `:format` entry restated in
its own `language.janet`, not just `:queries-from "typescript"`** -- confirmed against
`LanguageParse.cpp`'s `DiscoverQueryFiles`: each language definition's own explicit
`:queries` map is consulted independently; `:queries-from` only redirects the DIRECTORY
convention-based discovery searches for kinds an explicit entry doesn't already cover.
Since TypeScript's own `:format` entry is itself explicit (not a bare file discovered by
convention), TSX's `:queries-from "typescript"` would never find it on its own -- the same
reason `tsx/language.janet` already duplicates `:tags` verbatim rather than relying on
inheritance. Live-verified via `ned --format` combining a real `.ts` and `.js` file in one
project `.ned/format.janet`, output re-checked with `tsc --strict --noEmit` (0 errors) for
the TypeScript file.

## Kotlin: no fields at all, and the first format capture needing a text predicate

Kotlin is the eleventh language and a real outlier, not just another grammar with its own
node types. `tree-sitter-kotlin` (fwcd's community grammar) declares **zero fields
anywhere in the whole grammar** -- confirmed against `node-types.json`, not assumed -- so
`kotlin/format.janet` has no `body:`/`condition:`-style capture at all; every pattern is a
bare node-type match.

That absence of fields creates a real hazard no prior language had: `function_body` is the
SAME node type for both a real `{ ... }` block AND Kotlin's own brace-less
single-expression function body (`fun f(x: Int) = x + 1` parses to `(function_body
(additive_expression ...))`, no distinguishing wrapper at all) -- verified live. A bare
`(function_body) @brace.function` capture would sometimes hand `ComputeBracePlacementEdits`
a span with no literal brace in it whatsoever, the same severity class as PHP's
`switch_block` hazard. `control_structure_body` (if/while/for/when's own body) has the
IDENTICAL ambiguity: `if (x) 1 else 2` wraps each branch in a bare `control_structure_body
(integer_literal)`, no braces.

**Fixed with this codebase's `:match?` query predicate**
(`Editor/Grammar/QueryPredicates.cpp`, ECMAScript `std::regex`, already used elsewhere --
e.g. `rust/tests.janet`'s own `#[test]` detection) checking the captured span itself starts
with a literal `{`:

```
(function_declaration (function_body) @brace.function (:match? @brace.function "^\\{"))
```

Verified live: zero captures for an expression body, correct exact-brace spans for both an
empty and a real block. This is the first TEXT predicate this whole rollout has needed for
a format capture -- every prior discrimination problem (Go's ASI, PHP's 3-way body, C#'s
catch span) was solved with pure structure (field types, paired tokens, a node's own
grammar-rule span); Kotlin's grammar offers no structural handle to use instead.

**`if_expression` has no field to distinguish its "then" branch from its "else" branch**
either -- unlike every prior language (cpp/Java/JavaScript/Rust/C#/TypeScript all
deliberately exclude the else branch from `brace.control` via field-based selection),
Kotlin's own grammar has no way to single one out. `brace.control` here captures BOTH
branches uniformly, a real, grammar-forced deviation from that precedent rather than an
oversight.

**There is no `interface_declaration` node type in this grammar at all** -- confirmed
against `node-types.json` -- a Kotlin `interface` parses to the exact same
`class_declaration` node a `class` does, discriminated only by an anonymous `interface`
keyword token with no field naming it. So there is no `brace.interface` capture in this
file: an interface's own body is captured as `brace.class`, a real grammar limitation
rather than an oversight. `object_declaration` and `companion_object` both wrap their own
plain `class_body` the identical way `class_declaration` does, so `brace.class`/`def.method`
cover them for free with the same patterns -- a companion object's own methods get
`def.method` with no extra work, confirmed live.

**One real mistake caught by a test, not by inspection:** the first attempt captured
`when`'s own `(subject)` with the same paired `"("`/`")"` mechanism if/while/for/catch all
correctly need -- and it matched zero times. Checking `grammar.json`'s actual rule (the
same discipline C#'s `catch_declaration` taught) showed `when_subject` is a real named node
whose own rule literally opens with `"("` and closes with `")"`, the same shape as C#'s
`catch_declaration` -- so it needed a direct node capture (`(when_expression (when_subject)
@control.parens)`), not pairing. Fixed and reconfirmed live.

Verified live throughout with a comprehensive capture-shape probe before writing any
permanent test (every prior discrimination hazard checked explicitly: expression-bodied
functions, brace-less if branches, empty catch bodies, companion object nesting) -- caught
nothing wrong on that pass except the `when_subject` mistake above, caught immediately by
the first permanent test run. Live-verified via `ned --format` on a real project
`.ned/format.janet` combining all three rule kinds; `kotlinc` wasn't available at the time,
so the formatted output was instead re-parsed with `ned`'s own engine and confirmed to
contain no `ERROR`/`MISSING` nodes (structural, not semantic, validity). Full suite: 4636
cases.

**09-15 follow-up: real `kotlinc` verification, once installed.** The structural-only check
above was upgraded to a genuine one: a representative `.kt` file exercising every capture
this file names (a class/companion object/interface, an expression-bodied AND a
brace-bodied function, if/else/while/for/try-catch/when) was formatted through
`ned --format` with a real project config (`brace.function`/`brace.control` same-line,
`brace.class` next-line, `control.parens` within-false), then compiled with
`kotlinc -include-runtime` and RUN -- 0 compiler errors, correct program output. This is
strictly stronger than the structural-only check: it confirms the formatter's output isn't
merely parseable but semantically compiles and executes as intended.

## C: a separate grammar from cpp, not a subset, confirmed rather than assumed

C is the twelfth language, and deliberately NOT treated as "cpp minus the parts C doesn't
have" the way TypeScript was treated as a delta over JavaScript -- `tree-sitter-c` is a
genuinely separate grammar from `tree-sitter-cpp`, not a shared base the way
tree-sitter-typescript extends tree-sitter-javascript's own rules, so every shape was
verified live rather than assumed to carry over. One real difference found doing exactly
that: C's own `if`/`while`/`switch` condition field is typed `parenthesized_expression`
directly, the same node type name JavaScript/Java use -- NOT cpp's own `condition_clause`,
despite the two grammars' close relationship and shared ancestry.

Otherwise the template applies directly: `brace.function` (`function_definition`),
`brace.control` (if/while/for/switch bodies -- C has no try/catch at all, so no analogous
capture), `brace.class` folding `struct_specifier` and `union_specifier` together (both are
plain data-field aggregates in C, the same "close enough" call cpp's own `brace.class`
already makes for struct+class) while `enum_specifier`'s own body
(`enumerator_list`/`enumerator`, not `field_declaration`) gets no `brace.class` capture --
there is nothing brace-shaped worth placing inside it, only `def.toplevel` names it. A
bodyless function prototype (`int f(int x);`) parses to a plain `declaration` node, not
`function_definition`, so it is captured by nothing at all here -- the same "no defining
shape, no capture" precedent every prior language's own bodyless forms already set.

**No `def.method` at all** -- a real language absence, not a scope cut, the same one
`go/format.janet`'s own file documents for Go: C structs/unions hold only data fields,
never functions, so there is no "method nested in a type body" concept for this language to
capture in the first place.

Live-verified via `ned --format` on a real project `.ned/format.janet` combining all three
rule kinds, output re-checked with `gcc -Wall -Wextra` -- 0 errors, 0 warnings. Full suite:
4645 cases.

## Bash: the first genuinely PARTIAL language, not a full brace-carrying one

Bash is the thirteenth language, and the first one this rollout has added where the
brace-carrying template only partly applies -- verified live that `function_definition`
(covering all three real syntaxes: `f() { }`, `function g { }`, `function h() { }`, which
all produce the identical node) is the **only** brace-delimited construct in the whole
grammar. `if`/`while`/`for`/`case` all use keyword delimiters instead (`then`/`fi`,
`do`/`done`, `in`/`esac`), never braces, and a subshell uses `(...)` parens, not braces
either -- so `bash/format.janet` names only `brace.function` and `def.toplevel`. There is
no `brace.control`, `brace.class`, `control.parens`, or `def.method` in this file at all --
a real language absence for each, not a scope cut, the same kind `go/format.janet`'s own
missing `def.method` and `c/format.janet`'s own missing `def.method` already document, just
covering more of the vocabulary at once this time.

A nested function definition (`f() { g() { ... } }`, which Bash genuinely allows) still
gets its own `brace.function` capture -- brace PLACEMENT applies to any function's braces
regardless of nesting -- but is deliberately NOT a separate `def.toplevel`: only a direct
child of `program` counts, the same "direct child of the container" rule every prior
language's `def.toplevel` already follows, and Bash has no type/class concept for a nested
function to be a "method" of anyway.

A leading shebang line (`#!/usr/bin/env bash`) is a real preceding sibling in the parse
tree, so `isFirst` is correctly `false` even for the very first function in an ordinary
script -- the same lesson Python's own leading `import os`, Go's `package` clause, and
PHP's `<?php` tag already taught, reconfirmed live rather than assumed to carry over.

Live-verified via `ned --format` on a real project `.ned/format.janet` combining
`:break`/`:blank`, output re-checked with `bash -n` (syntax check) -- exit 0. Full suite:
4653 cases.

## Lua: the first KEYWORD-delimited language, and a real mechanism gap it exposed

Lua is the fourteenth language, and the first with **no brace or paren delimiter
anywhere in its block-forming grammar at all** -- verified live against a real parse:
`if`/`while`/`for`/`do` bodies are a bare `block` field with the delimiting keyword tokens
(`"then"`/`"do"`/`"end"`) sitting OUTSIDE it as direct siblings, the same shape
Python's own `block` field has, minus Python's total absence of any delimiter. The paired
`"<name>.open"`/`"<name>.close"` capture mechanism (already proven for C#/Kotlin/for-loops
on single-BYTE tokens like `"("`/`")"`) is what this file leans on -- but every consumer of
a synthesized paired capture (`Editor/FormatBracePlacement.h`, `Editor/FormatSpacing.h`)
had hardcoded `text[capture.startByte]`/`text[capture.endByte - 1]` as ONE-BYTE delimiter
characters throughout, since every capture before this one satisfied "my own first/last
byte IS the delimiter" for free. `"do"` (2 bytes), `"then"` (4 bytes) and `"end"`
(3 bytes) don't. This is the real "we need the additional formatters" moment the language
rollout was always going to hit eventually (see the session's own standing directive) --
not a new rule KIND, but a genuine generalization of the Break-kind mechanism itself:

- `FormatCapture` (`Source/Editor/Mode.h`) gained `openLength`/`closeLength`, defaulting
  to 1 so every existing capture (a brace, a paren) needs no query change at all.
  `Mode.cpp`'s own paired-capture correlation sets them from the raw `.open`/`.close`
  captures' own spans -- the delimiter TOKEN's length, whatever it is.
- `ComputeBracePlacementEdits` (`FormatBracePlacement.cpp`) now reads the open/close
  TOKEN TEXT (`text.substr(capture.startByte, capture.openLength)` and its close-side
  twin) everywhere it used to read a single byte -- the interior-is-empty check,
  collapse-empty's glue, collapse-simple's expand/collapse, and the `NextLineIndented`
  closer-repositioning step (which needed the closer TOKEN's own start, not just
  "one byte before the capture ends").
- `ComputeSpaceEdits`'s (`FormatSpacing.cpp`) `:within` handling generalized the same way
  (`capture.startByte + capture.openLength` / `capture.endByte - capture.closeLength`
  instead of `+1`/`-1`) -- unexercised by any bundled default, but a real correctness fix
  once a keyword-paired capture could reach it at all.
- **Gluing two keyword tokens together is a real hazard single-character delimiters never
  faced**: `collapse-empty`'s `{}`-style glue (no inserted space) would turn Lua's
  `"do"`+`"end"` into the single identifier `doend` if applied unchanged. A general,
  content-based rule -- insert a space only when both sides are ASCII word bytes
  (letter/digit/underscore) -- fixes this without any per-language special case: `"{"`+`"}"`
  still glues with nothing (neither is a word byte), `")"`+`"end"` still glues with
  nothing (`")"` isn't a word byte either), and `"do"`+`"end"` glues as `"do end"`.

Verified against the full suite (4670 cases, unchanged) before any Lua-specific test was
added, then against three hand-built regression cases specific to the generalization: a
`NextLineIndented` edit on `while x do\nprint(1)\nend\n` repositioning BOTH the 2-byte
`"do"` and the 3-byte `"end"` correctly; the same repositioning applied through a real
`if`/`elseif`/`else`/`end` chain, confirming the elseif/else content in between the
captured `"then"` and the final `"end"` is never touched (`brace.control` on `if_statement`
spans the WHOLE chain -- there is only ever one literal `"end"`, closing every branch, not
one per branch, so `collapse-empty`/`collapse-simple` correctly never fire once an
elseif/else is present, needing no special-case query); and the word-byte glue boundary
itself, both directions.

`lua/format.janet` itself:

- **`brace.function`**: paired on the parameter list's own closing `")"` (the one token
  always present, even with zero parameters) through to `"end"` -- Lua has no opening
  keyword for a function body at all (`_function_body` is `parameters, body, end`, verified
  against the real grammar rule, not assumed from the field list). Covers every
  DECLARATION shape uniformly: free functions, `local function`, and dot/colon "method"
  syntax (`function M.f()`/`function M:f()`) all parse as the identical
  `function_declaration` node, differing only in their own `name:` field's node type --
  Lua draws no structural method/function split the way a class body would. Deliberately
  does NOT capture `function_definition` (an anonymous function EXPRESSION, e.g.
  `local f = function() end` or an inline callback) even though it has the identical
  `parameters`/`end` shape and would match if named -- matching every prior language's own
  "declarations, not expressions" scope for `brace.function`. An earlier draft captured it
  anyway and was caught by this file's own test before it shipped.
- **`brace.control`**: `"do"`/`"end"` for `while`/`for`/a standalone `do_statement`;
  `"then"`/`"end"` for `if_statement` (see the elseif/else discussion above). elseif/else
  branches themselves get NO capture of their own -- a real absence, not a scope cut:
  neither has a delimiter pair naming just its own body (an elseif's `"then"` opens it, but
  its closer is whatever comes next -- another elseif's `"then"`, `"else"`, or the outer
  `"end"` -- never a token belonging to the elseif itself).
- **`control.parens`**: the same narrow "already parenthesized" lever python's and go's own
  files use -- Lua's idiomatic style omits parens (`"if x then"`), but the grammar still
  allows writing them (`"if (x) then"`), parsing as a real `parenthesized_expression`
  (verified live, zero matches vs. exactly one). `for`'s own clause takes no general
  boolean expression at all, so it has no analogous capture, matching every other
  language's own "for-loops don't get `control.parens` unless the grammar genuinely wraps
  them" precedent.
- **No `brace.class`/`brace.interface`/`def.method` at all**: Lua has no dedicated
  class/interface syntax the grammar could ever name (a "class" is conventionally just a
  table plus dot/colon-qualified functions), and a "method" is structurally identical to
  and interleaved with an ordinary function -- never nested inside a distinct class-body
  container the way every `def.method`-carrying language's own methods are. Matches Go's
  own precedent exactly (a receiver method is a top-level declaration too, for the same
  structural reason).
- **No `.simple` marker anywhere**: the marker mechanism (`Mode.cpp`'s own correlation)
  requires a SECOND pattern capturing the exact same byte range as the base capture from a
  SINGLE node; every capture in this file is instead synthesized from a paired open/close
  match with no single node spanning `")"..."end"` or `"then"..."end"` (the block sits
  strictly BETWEEN the two keyword tokens, never wrapped by anything that also includes
  them), so there is no single node to anchor a matching `.simple` pattern against either.
  A real, documented scope cut, matching `go/format.janet`'s own switch/select and
  `csharp/format.janet`'s own catch precedent -- extending the marker convention to a
  paired form of its own is real future work, not attempted here.
- **`def.toplevel`/`.first`**: covers every declaration shape (see `brace.function` above)
  but never a nested one (only a direct child of `chunk`, the module-top-level node, the
  same "direct child of the container" rule every prior language's `def.toplevel` already
  follows) and never the anonymous `function_definition` expression form. `.first` is
  wired at exactly the SAME level `def.toplevel`'s own primary capture is (`chunk`),
  deliberately NOT at every `block` a function could be nested inside -- an earlier draft
  did this and was wrong (it made `isFirst` fire for a function that's merely the first
  statement of some arbitrary nested `do`/`if`/`while` body, a scope no other language's
  own `.first` convention ever covers either: python/go/js all wire `.first` at the
  identical module/class-body level their `def.toplevel`/`def.method` itself uses, never at
  a generic nested block). `local function f() end` is NOT a distinct node type -- it
  parses to the same `function_declaration`, merely wrapped by its parent's own
  `local_declaration:` field; an earlier draft named a nonexistent `local_function` type
  and failed to compile, the same class of mistake `typescript/format.janet`'s own
  field-name lesson already recorded.

Live-verified with `luac -p` (syntax check) on three representative outputs -- a
`NextLineIndented` edit, a keyword-pair glue, and a brace-pair glue -- all exit 0. Full
suite: 4670 cases.

## Bash, revisited: `brace.control`/`control.parens` unlocked by Lua's own
generalization, and two real hazards found and guarded

Once Lua's `openLength`/`closeLength` generalization above proved out the paired
keyword-delimiter mechanism, bash's own file -- originally shipped PARTIAL (only
`brace.function`/`def.toplevel`, see the section above) specifically because the mechanism
didn't exist yet -- was revisited to add the rest. Every construct verified live against a
real sexp dump before writing any query, same discipline as every language before it:

- **`brace.control`**: `do_group`'s own `"do"`/`"done"` pair (shared by while/until/for/
  select's own body field AND a C-style for-loop's `do`/`done` alternative -- captured
  ONCE, not once per parent construct); `if_statement`'s `"then"`/`"fi"` pair, spanning an
  elif/else chain to the one real closing `"fi"` token (the exact shape Lua's own
  if/then/end pairing established); `case_statement`'s `"in"`/`"esac"` pair; a C-style
  for-loop's own brace-body ALTERNATIVE (`for ((;;)) { ... }`, confirmed live this is real,
  accepted bash syntax, not merely something the grammar tolerates); a bare `subshell`
  `"(...)"` and a standalone group command `"{ ...; }"` (both real, distinct constructs --
  the group command needed an explicit `:not-has-parent?` exclusion of the two other
  `compound_statement` contexts already captured elsewhere, or the same node would be
  captured twice under different names at the identical byte range).
- **A real, verified fact, not assumed to carry over from Lua's own precedent**: a truly
  empty `do`/`done`, `then`/`fi`, subshell, or group-command body is a hard SYNTAX ERROR in
  real bash (confirmed live with `bash -n`) -- unlike Lua's `do end`. So `:collapse-empty`'s
  own trigger condition can only ever arise from ALREADY-invalid source for those four
  shapes, making the feature real but practically inert there, not a hazard. `case`/`esac`
  is the one genuine exception -- `case $x in esac` (zero items) IS valid bash, confirmed
  live, so `:collapse-empty` is a real, reachable lever there specifically.
- **A real, Go-ASI-class `:placement` hazard, found live and GUARDED in C++**: unlike
  every brace-carrying language so far, `:same-line` is the DANGEROUS placement value for
  bash's `brace.control` (every other value is safe) -- confirmed live with a real
  `bash -n`: `"while true do"`/`"if true then"` (same-line's own plain-space gap) are hard
  syntax errors, since `do`/`then` are bash reserved words requiring a real statement
  TERMINATOR (a semicolon or newline) before them, never merely whitespace.
  `Editor/FormatBracePlacement.h`'s own `PlacementUnsafeForLanguage` gained a capture-NAME
  parameter for this (Go's own guard never needed one, since its hazard was language-wide)
  and now declines `:same-line` for `brace.control` specifically -- `brace.function` is
  unaffected (bash's function bodies are real braces, confirmed live `:same-line` works
  fine there). The guard is a blanket decline for the whole capture name rather than
  per-construct: `case`'s own `"in"` and a C-style for-loop's own `"do"` both have an
  OPTIONAL terminator in the grammar and are actually safe for `:same-line` (confirmed
  live), but `brace.control` has no per-instance signal available at this layer to tell
  them apart from while/until/for/if's genuinely unsafe shapes -- declining the safe cases
  too is the same "decline rather than risk corruption" tradeoff Go's own guard already
  accepts.
- **`control.parens`**: `test_command`'s own `"["`/`"]"` or `"[["`/`"]]"` (both parse to
  the same node type, differing only in bracket width -- 1 byte vs. 2, both handled
  correctly by the openLength/closeLength generalization) -- the MANDATORY condition
  syntax itself, not a redundant/optional lever the way every prior language's
  `control.parens` was (the same "mandatory yet still needs pairing" shape C#'s own
  condition parens turned out to have, for an unrelated grammar reason). A C-style
  for-loop's own `"(("`/`"))"` clause, captured the same way every C-family language's own
  for-loop clause is.
- **A second real `:within=false` hazard, ALSO guarded (a follow-up correction)**: an
  earlier pass here claimed only `[[`/`]]` needed guarding and single `[`/`]` was safe --
  WRONG, caught by re-verifying with a real bash RUN rather than trusting `bash -n` alone.
  Both bracket forms are ordinary bash WORDS (a command name, a reserved word) needing
  whitespace separation from their own content: `[-n x` fails as a bad command name (a
  shell-parser-level failure `bash -n` catches), and `[ -n x]`/`[[ -n x]]` (space missing
  only on the CLOSE side) fail at RUNTIME instead (`[`'s own argument scanning happens
  inside the builtin, not the shell's parser, so `bash -n` alone misses it). Only `((`/`))`
  (arithmetic context) is genuinely safe. `Editor/FormatSpacing.h`'s new
  `WithinRemovalUnsafe(languageKey, openText)` declines just the `:within=false` direction,
  and only for this one gap -- `:within=true`/`:before`/`:after` on the same capture stay
  unaffected.
- **A real, pre-existing test-isolation bug found and fixed along the way**: `Tests/
  FormatBracePlacementTest.cpp`'s own `FormatRulesGuard` only ever reset bare
  `"brace.function"`'s rules, never `"brace.control"`'s, even though at least six
  pre-existing tests (php/java/... `End to end` tests) set a bare `"brace.control"` rule
  relying on this guard for cleanup. Surfaced as an intermittent, `--order rand`-dependent
  failure once a new bash-mode test needed `brace.control`'s `:placement` to be a genuine
  no-op and instead got a leaked `collapse-simple=true` from an earlier php-mode test.
  Fixed by extending the guard to cover `brace.control` too -- confirmed with several
  `--order rand` runs clean afterward, not just the one failure disappearing.

Live-verified with a real `bash` run (not just `-n`) executing every construct above and
producing correct output, and via `ned --format` on a real project config combining
`:break`/`:space` rules, re-checked with `bash -n` (exit 0). Full suite: 4683 cases.

## Fish: an even more minimal partial case than bash's own, and a general word-fusion
guard reused from collapse-empty

Fish is the fifteenth language, and the paired keyword-delimiter mechanism (proven on Lua,
extended to bash) unlocks it directly -- but fish's own grammar turns out to offer even
LESS structure than bash's: `if`/`while`/`for`/`switch`/`function` bodies have **no
capturable `brace.function`/`brace.control` at all**, a real structural absence for a
DIFFERENT reason than bash's original partial shape. Each of those constructs' own header
ends directly in a `_terminator` (the same token -- `;`/newline -- that already separates
any two ordinary statements), with no SEPARATE opening keyword sitting between the
condition/header and the body at all (confirmed against `grammar.json`'s own rules, not
node-types.json's field summary alone) -- unlike bash's "do"/"then", there is nothing here
for `:placement` to move: the terminator IS the boundary, not a repositionable delimiter,
and turning `;` into a newline (or back) is a different KIND of edit than this mechanism
was ever built for.

`begin_statement` is the ONLY construct in the whole grammar with a real, capturable
delimiter pair -- and it has TWO distinct forms (`begin ... end`, and a `{ ... }`
alternate, confirmed live as real, executable fish syntax) sharing one node type,
discriminated here by which literal token is actually present rather than a `:match?`
text predicate (unlike Kotlin's own ambiguity, which had no anonymous token to distinguish
its two shapes by at all). `begin_statement`'s own node span starts and ends exactly at
its paired tokens for EITHER form (verified against `grammar.json`'s own rule -- unlike
bash's `if_statement`/`case_statement`, whose own span starts at `"if"`/`"case"`, not
`"then"`/`"in"`), so BOTH forms get a real `.simple` marker, unlike anything bash's own
`then`/`fi` or `in`/`esac` could support.

Two real hazards found, both already-existing mechanisms rather than new ones:

- **The same `:same-line` "glues onto a preceding statement's terminator" hazard bash's
  own `do`/`then` has, for the identical underlying reason**: `begin_statement` is a bare,
  standalone statement, so confirmed live with a real fish RUN: `"echo hi begin ... end"`
  reads `"begin"` as an ARGUMENT to `"echo"` rather than starting a new block (`"'end'
  outside of a block"`). `PlacementUnsafeForLanguage` extended for `"fish"` the same way it
  already covers `"bash"`.
- **A real `:within=false` hazard too, but a GENERAL one this time, not fish-specific**:
  unlike bash's `do`/`then` (which need an immediate terminator, an unrelated lexical
  fact), fish's `"begin"` does NOT need one -- `"begin echo hi;end"` runs fine. But
  removing the space right after `"begin"` would still glue it onto a following WORD
  (`"beginecho"`), fusing two identifiers -- confirmed live. This is the EXACT hazard
  collapse-empty's own glue logic already guards (Lua's `"do"`+`"end"` -> `"doend"`), just
  reached from `:within` instead -- so rather than writing a fish-specific check the way
  bash's `[`/`[[` needed, `IsWordByte` was lifted into the shared `Editor/FormatEdit.h`
  (used by both `FormatBracePlacement.h`'s collapse-empty and `FormatSpacing.h`'s new,
  general `EmitWithinIfSafe`), so ANY future keyword-delimited language's own open-side
  word-fusion risk is already covered, not just fish's. The `"{"` form has no such risk
  (`"{"` is not a word byte -- `"{echo hi;}"` runs fine, confirmed live).

`def.toplevel` covers every function, never a nested one (fish genuinely allows nesting,
confirmed live) -- the same "direct child of the container" rule every prior language's
`def.toplevel` already follows. No `def.method` at all, matching bash/Go's own precedent
(no type/class concept for a "method" to belong to).

Live-verified via `ned --format` on a real project config combining `:break`/`:blank`,
output re-checked with `fish -n` (syntax check) and a real `fish` run. Full suite: 4693
cases.

## Coverage audit: closing the gaps this rollout's own day-by-day focus left behind

A dedicated audit pass (2026-09-15) went back over every language above looking for
constructs skipped not because a grammar genuinely lacks them, but because they weren't
the specific thing being worked on that day -- five parallel investigations (one per
language group), each cross-checking the shipped captures against the real grammar's full
node-type list. The pattern held everywhere: every language's *declaration-position*
bodies (a named function, an `if`/`while`/`for`) were captured; the *parallel* shapes --
`do`-`while`, a `try` block's own body (as opposed to `catch`/`finally`), static/instance
initializer blocks, `switch`/`match` expressions, anonymous classes -- were not, simply
because no prior pass had reason to look for them.

**A deliberate policy reversal, not a bug fix.** Anonymous function/lambda/closure bodies
(JavaScript's arrow functions, Go's `func` literals, PHP's closures, Rust's closures, C#/
Java's lambdas, Kotlin's lambda literals) were excluded from `brace.function` by an
explicit early call (the Lua rollout, "declarations, not expressions") -- correctly
consistent everywhere it was applied, but the audit found it was a real, systemic scope
line, not a one-off. Asked directly, the call was reversed: anonymous bodies now get
`brace.function` too, matching whatever placement rule the language's declared functions
already use. This mattered practically as well as philosophically -- Go's `func` literals
(`go func() { ... }()`), PHP's closures, and Rust's closures had literally never been
captured under EITHER policy, since no prior pass had reason to add them regardless of the
scope question.

**The two highest-impact finds were pure gaps, not policy questions.** JavaScript's own
`method_definition` -- covering every class method, object-literal method, and
constructor -- had **zero** placement capture of any kind before this pass; only
`def.method` (blank-lines) existed for it. C#'s `constructor_declaration`/
`destructor_declaration` were similarly invisible to `brace.function`; only
`method_declaration` was ever captured. Both are now `brace.function` like everything
else.

**No new C++ mechanism was needed for any of it** -- every addition reuses machinery
already proven earlier in this rollout: the paired `.open`/`.close` tokens (C#'s
`switch_expression`, Kotlin's `init { }`/secondary constructors, Lua's `repeat`/`until`),
the `:match?` text predicate (Kotlin's `do`-`while`, Rust's closures -- though Rust's
turned out not to need it after all, see below), and type-qualified field captures for
anything with two genuinely distinct node types in one field slot (C#/JavaScript's own
lambda/arrow-function bodies, needing no predicate at all since `block` and `expression`
are different node TYPES there, unlike Kotlin's `function_body` which is the SAME type
either way and can only be told apart by its own leading byte).

**Two real corruption hazards were found and guarded, both live, both by the same
apply-and-check-the-whole-result discipline this rollout has used throughout:**

- Fish's own earlier writeup had wrongly lumped `function_definition` in with `if`/
  `while`/`for`/`switch`'s genuine structural absence -- it doesn't share their reason at
  all (it has a real, literal `"function"`/`"end"` pair, simply never captured). Fixed,
  and it needed the identical SameLine placement guard `begin_statement` already has
  (`PlacementUnsafeForLanguage` extended to cover `"fish"`/`"brace.function"`).
- **A new hazard, unique to fish's own function, found by force-expanding a real
  single-line function**: `:collapse-simple`'s own interior computation assumes the open
  TOKEN sits directly beside the real body -- true for every other paired capture in this
  codebase, false for fish's `function_definition`, whose mandatory `name:` field (and any
  `option:` fields) sit between `"function"` and the body. Force-expanding
  `function greet; echo hello; end` produced `function\n    greet; echo hello;\nend` --
  confirmed with a real `fish -n` this is a hard syntax error (`"function"` alone names no
  function at all). Guarded with a new, narrowly-scoped
  `CollapseSimpleUnsafeForLanguage(languageKey, captureName)`
  (`Editor/FormatBracePlacement.cpp`), declining both collapse-simple directions for
  `"fish"`/`"brace.function"` specifically -- `:placement` and `:within` are both
  unaffected, since neither reads "interior" the same name-inclusive way.

**One correction to an over-eager first guess, caught by checking rather than assuming**:
Rust's `closure_expression` body was first assumed to need the same `:match?` predicate
Kotlin's `function_body` needs, since its own field type list includes a generic `_`
wildcard alongside `_expression`. Checking `node-types.json` directly showed `block` is
itself one of `_expression`'s own concrete variants (a block IS a valid Rust expression),
so a plain type-qualified `body: (block)` capture already discriminates correctly with no
predicate at all -- the same shape C#/JavaScript's own lambda bodies turned out to have.

**One pre-existing, unrelated bug found and logged, not fixed in-session**: testing Lua's
new `repeat`/`until` capture live surfaced that its own body is never reindented at all
(`Editor/ImprintTables.cpp`'s Lua table has entries for `do`/`if`/`while` but none for
`repeat_statement`) -- a real gap in the separate structural-indent engine, unrelated to
this formatter work, logged to `ROADMAP.md`'s watch list.

Every language's `format.janet` file carries the specific per-construct grammar facts
inline, in the same style as every entry above; this section is the cross-cutting summary,
not a replacement for reading them. Full suite: 4709 cases, confirmed clean across multiple
`--order rand` reruns, output for every touched language re-verified with that language's
own real toolchain (`g++`, `javac`, `dotnet run`, `node`, `go run`, `rustc`, `php`,
`kotlinc`, `fish`, `lua`) wherever the audit changed generated output, not merely
re-parsed.

## Ruby: every opening keyword is optional, and two real hazards the paired mechanism
had never hit before

Ruby is the seventeenth language, and the `def...end` keyword-delimited path it was flagged
as a proven candidate for back when Lua/bash/fish's own paired `.open`/`.close` mechanism
was built. The one genuinely new wrinkle: **every construct's opening keyword is optional
in Ruby's own grammar**, unlike Lua/bash/fish where a real opening keyword (`function`/`do`/
`then`/`begin`) is always present. `if`/`unless`'s own body wrapper (`then`) only carries a
literal `"then"` token when the user actually writes one (`if x then`); the far more common
`if x\n ... end` has no separate opening token at all between the condition and the body,
confirmed via `grammar.json`'s own `CHOICE` rule, not assumed. `while`/`until`'s own `do`
wrapper has the identical optionality. Confirmed live with `tree-sitter query` (0 matches
without the keyword, 1 with it): `brace.control` for these four is real, but only ever
fires for the keyword-having form -- a genuine structural absence for the dominant style,
not a scope cut.

`method`/`singleton_method` are anchored on their own NAME field rather than the parameter
list's own closing paren (Lua's own precedent), because Ruby's parameter list has THREE
mutually-ambiguous shapes (parenthesized, bare, or absent entirely) sharing one aliased
node type with no structural way to tell a bare list from an absent one apart -- anchoring
on the closing paren would need a second pattern for the other two shapes, and that second
pattern would ALSO match the parenthesized case, producing two overlapping candidate opens
for the same method. One uniform NAME anchor covers all three parameter styles with no
double-match risk, at a minor, documented cost: a parenthesized method's own interior
computation sees the parameter list as part of the "interior," so `def f()\nend` doesn't
collapse-empty even though its body is genuinely empty -- the common bare/no-arg case is
fully precise. Ruby's own "endless method" (`def f = 1`, a real expression-bodied form, not
a rare edge) needs no `:match?` discriminator at all, unlike Kotlin's identically-shaped
ambiguity: requiring a literal `"end"` token as the node's own child is already a real
structural fact an endless method's own node simply doesn't have.

Two real corruption hazards, both caught live by this rollout's own apply-and-check
discipline, not by inspection -- and both required a genuinely NEW guard shape, since every
prior guard in this file keyed on `(language, captureName)` alone, which turns out to be
too coarse here:

- **`do_block`'s own multi-byte delimiter, caught by a scratch capture dump**: a bare
  `(do_block) @brace.function` direct capture (the same shape `block`'s own `"{"`/`"}"` uses)
  would silently default `openLength`/`closeLength` to 1, matching only the FIRST byte of
  `"do"`/`"end"` -- confirmed via a live dump showing `open=1 close=1` where `2`/`3` were
  expected. Fixed by pairing `"do"`/`"end"` explicitly the same way `begin`/`while`'s own
  keywords already are; since both are `do_block`'s own first/last children, the synthesized
  span is byte-identical to the direct capture it replaces, so the `.simple` marker (still
  capturing the whole node) correlates against it unchanged.
- **Collapse-empty gluing an identifier onto `"end"`, caught by an applied Buffer test**:
  `method`/`class`/`module`/`singleton_class`'s own NAME/VALUE-anchored open is the FIRST
  capture in this whole rollout where `.open` is arbitrary identifier text rather than a
  fixed keyword/punctuation delimiter. Collapsing `def foo\nend` to `def foo end` is NOT the
  word-fusion problem `IsWordByte` already guards (a separating space IS inserted) --
  confirmed live via `tree-sitter parse` that shape still MISPARSES (`"end"` swallowed as a
  bare parameter, a genuine `MISSING "end"` node), purely from the grammar's own
  params-continuation ambiguity at that lexical position, independent of whitespace. The
  same misparse hits `class Foo end`/`module M end`/`def self.foo end`. Fixed with a new
  `CollapseEmptyUnsafeForLanguage(languageKey, openText)` guard keyed on the open TOKEN'S
  OWN TEXT (mirroring `FormatSpacing.h`'s own `WithinRemovalUnsafe` precedent for bash's
  `[`-vs-`((` distinction) rather than the capture name, since Ruby's own `brace.function`/
  `brace.class` each mix a safe shape (`block`/`do_block`'s real delimiter) with an unsafe
  one (`method`/`class`'s own name-anchored open) under ONE name -- declined for any
  word-shaped open text that isn't one of Ruby's own fixed keyword opens (`do`/`then`/
  `begin`, confirmed live that gluing any of those three onto `"end"` parses clean).
- A related, smaller finding of the SAME shape: `"begin"` is a bare, standalone statement
  with the same "glues onto whatever precedes it" hazard bash/fish's own `do`/`then`/
  `begin_statement` have (confirmed live: `"foo begin...end"` gets swallowed as an argument
  to `"foo"` rather than starting a fresh block) -- while if/unless/while/until's own
  `"then"`/`"do"` are always nested inside their OWN statement's header and confirmed safe
  by construction. `PlacementUnsafeForLanguage` gained an `openText` parameter for the same
  reason, discriminating `"begin"` from `"then"`/`"do"` under the shared `brace.control`
  name rather than blanket-declining the whole capture name the way bash/fish's own guards
  do (their own hazard is real for every instance sharing the name, so a capture-name-wide
  decline was the correct answer for them, not merely the convenient one -- Ruby's own split
  is genuine, and the signal was already available from the collapse-empty fix above).

`case`/`case_match` are declined from `brace.control` entirely -- confirmed via
`grammar.json` there's no opening keyword analogous to bash's own `"case ... in"` (`"when"`/
`"in"` belong to each clause, not the case statement as a whole), and (like `case`/`when`)
`rescue`/`ensure` clauses inside `begin` share the ONE enclosing `"end"` rather than owning
a closing token of their own. `control.parens` is the same narrow "already parenthesized"
lever Python/Go/Lua's own files use, reaching if/unless/while/until's own condition and
case/case_match's own value alike.

**One pre-existing, unrelated bug found and logged, not fixed in-session**: a baseline `ned
--format` pass with no format.janet rules configured revealed `method`/`singleton_method`/
`while`/`until` bodies are never reindented at all -- `Editor/ImprintTables.cpp`'s Ruby
table has entries for `if`/`class`/`module`/`begin`/`do`/`case` but none for these four,
for the identical underlying reason format captures needed extra care for them (no single,
always-present open token directly beside the body). Logged to `ROADMAP.md`'s watch list; a
real fix needs the same design work this rollout did for the format side, not a mechanical
table entry.

No real Ruby toolchain (`ruby`/`irb`) is installed in this environment -- the one language
in this whole rollout without one. Verified instead via `tree-sitter parse`/`tree-sitter
query` against the real vendored grammar directly (`-p <grammar-dir>`, sidestepping the
0.27 CLI's own need for a `tree-sitter.json` config) for every structural claim, and by
re-parsing `ned --format`'s own output with ned's own parse engine for zero `ERROR`/
`MISSING` nodes -- the same honest, disclosed structural-only substitute Kotlin used before
`kotlinc` was installed. Full suite: 4709+ cases (17 new test cases across all three
format-kind test files), confirmed clean.

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
