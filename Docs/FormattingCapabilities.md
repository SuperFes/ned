# Formatting capabilities

What a configurable formatter has to be able to express, catalogued from JetBrains'
**Editor → Code Style** panes across ten languages (C/C++, Rust, PHP, JavaScript,
TypeScript, Python, SQL, HTML, Markdown, JSON), then sorted into what Ned can do today,
what it could do with the tree-sitter it already has, and what it genuinely cannot —
with, for that last group, what it would take.

Companion to `Docs/HighlightCapabilities.md`. Same method: take the union, collapse
everything that was only a different spelling of the same idea, name the result by role
rather than by language.

## The stance

Two things shape every decision below.

**One engine, two entry points.** Whatever formats on demand must be the same code that
formats while you type. Not "similar" — the same function, over a smaller range. Ned
already does this for indentation: `Editor/Indent.h`'s `ComputeIndentColumn`/
`IndentRegion`/`IndentBuffer` are deliberately the same primitives whether reached from
`newline`/`indent-for-tab-command` (live, one line) or `indent-region`/`indent-buffer`
(batch, a range). That header says so in its own comment, and it is the shape everything
here should copy. A formatter that is a pure function of
`(tree, text, range, config) → edits` runs identically at any range; the live path is
just "the smallest node containing the edit you made".

**Formatting is a choice, not a gate.** A pre-commit run is useful for one thing only:
holding a shared codebase to the standard *you* picked. It is not a verdict on your
taste. Ten-year-old code gets reformatted to today's preference; today's preference
changes next year; someone else's preference is not wrong. So: every rule below is
configurable, every rule has a defensible default, and nothing is ever forced on.
The reformat-the-whole-file case is first-class, not a recovery path.

## Nine rule kinds

The ~700 individual knobs across those panes are not 700 mechanisms. They are nine,
applied to node types a query names:

| # | Kind | What it decides | State |
|---|---|---|---|
| 1 | **Indent** | Leading whitespace on a line | **Have** |
| 2 | **Space** | A space between two adjacent tokens, or inside a delimiter pair | Need |
| 3 | **Break** | A mandatory or forbidden newline at one point | Need |
| 4 | **Wrap** | A policy over a *list* when it exceeds the margin: never / if-long / chop-down / always | Need |
| 5 | **Align** | A shared column across sibling lines | Partial |
| 6 | **Blank** | Min/max blank lines around a node | Need |
| 7 | **Case** | A token's own text case | Need |
| 8 | **Arrange** | Reorder siblings by a key | Need |
| 9 | **Rewrite** | Replace a construct with an equivalent one | Need |

Every JetBrains row in the appendix is one of these nine, pointed at a node. That is what
makes the scope tractable: the engine is nine passes, the per-language cost is one
`format.scm` naming that language's nodes, and the config is a table of
`(rule kind, capture, value)`. The same shape `indents.scm` already established for kind 1.

---

# Tier A — can, and do

Shipped and live today.

| Capability | Where |
|---|---|
| Indent width, tabs-vs-spaces, per language | `Editor/IndentStyle.h`, `ned/set-indent-style` |
| Tab display width | `Editor/TabWidth.h` |
| Structural indentation, 21 languages | `Editor/Indent.h` + the delimiter imprint (`Editor/ImprintIndent.h`, every bracket/indentation body) + `*-indents.scm` for the remainder (`@aligned`/`@indent.body`/`@align.barrier`/`@indent.suppress`, and `@indent`/`@dedent` only for keyword bodies, tag pairs and clause headers) |
| Continuation indent / align-to-opener | `@aligned` |
| Live-while-typing indent | `newline`, `indent-for-tab-command` |
| On-demand indent, any range | `indent-region`, `indent-buffer` |
| Rigid shift | `RigidShiftRegion` |
| Final newline on save | `Editor/FinalNewline.h` |
| Line-ending normalisation | `Editor/LineEndingPolicy.h` |
| Right margin / fill column | `Editor/FillColumn.h` |
| Comment-aware paragraph reflow | `fill-paragraph` (`M-q`), `Editor/Fill.h` |
| Table alignment (Org + GFM) | `Editor/Table.h` |
| Auto-pair brackets and quotes | `Editor/AutoPair` |
| Trailing-whitespace *visibility* | `Editor/WhitespaceSettings.h` (highlight only — no strip) |
| External formatter on save | `Editor/FormatOnSave.h`, `ned/set-format-command` |
| LSP formatting / range / on-type | `Editor/Lsp/Manager.h` |
| Soft-wrap override per extension | `Editor/WrapOverrides.h` |

That covers rule kind 1 completely and kind 5 partially. Everything else below is new.

---

# Tier B — could, with the tree-sitter we already have

These need no new parsing capability. They need the engine's missing passes, plus a
`format.scm` per language naming the nodes. Ordered must-have → maybe → probably not.

## B1 — Must have

The rules without which "format this file" is not a real feature.

**Spaces (kind 2).** The single largest group in every pane, and the most visible.
Language-agnostic shape: a rule is `(before|after|around|within) <capture>`. Collapses
across languages exactly the way highlight captures do — JS's "'if' parentheses", PHP's
"'if' parentheses" and C's "before parentheses in control statements" are one rule on
`@control.parens`.
- Around operators, by class: assignment, logical, equality, relational, bitwise,
  additive, multiplicative, shift, power, unary, arrow, concatenation, member access,
  null-coalescing, range.
- Before/within/after each delimiter pair: call parens, declaration parens, brackets,
  braces, index subscripts, generic/template angle brackets, grouping parens, interpolation.
- Before/after each separator: comma, semicolon, colon, ternary `?`/`:`, `->`/`=>`.
- Before a left brace, before a keyword (`else`/`while`/`catch`/`finally`).
- Empty-pair handling: `()` / `{}` / `<>` with nothing inside is its own rule everywhere.
- Before end-of-line comment, and whether to preserve existing spacing there.

**Breaks (kind 3).** Mandatory/forbidden newlines at named points. `else`/`while`/`catch`
on a new line after a closing brace; `'while'` on a new line in do-while; `catch`/`finally`
on a new line; a single embedded statement forced onto its own line; `case` bodies;
return type on a new line; `<?php` tag.

**Brace placement (kind 3, specialised).** Same-line (K&R) vs next-line (Allman) vs
next-line-indented (GNU/Whitesmiths), *per construct kind* — namespace, type, function,
lambda/closure, control statement, anonymous class, case block. Plus "keep empty braces
on one line", "keep a simple block on one line", and "force braces on a braceless
statement" (always / do-not-force / when-multiline). The per-construct granularity is the
whole point; a single global setting is not this feature.

**Wrapping (kind 4).** A policy over a list: `do-not-wrap` / `wrap-if-long` /
`chop-down-if-long` / `wrap-always`, plus the companion booleans every pane repeats —
`new line after '('`, `place ')' on new line`, `align when multiline`, `force trailing
comma if multiline`, `use continuation indent`. Applies to: declaration parameters, call
arguments, array/object/collection literals, base-class/implements lists, chained method
calls, binary expressions, assignments, ternaries, import/export lists, enum bodies,
attribute/decorator lists, union/intersection types.
- **`Keep existing line breaks`** is load-bearing and must default on. It is what makes a
  formatter safe to run over someone else's ten-year-old file: honour their breaks, fix
  only what is unambiguously wrong. JetBrains defaults it on for exactly this reason.

**Blank lines (kind 6).** Two independent halves, both needed: *maximum* preserved
(in declarations, in code, after `{`, before `}`) and *minimum* enforced (around class /
function / method / field / namespace / import block / access specifier / enum case /
before first method / after opening tag). `Remove blank lines after '{' and before '}'`
is its own toggle.

**Hard wrap / right margin.** `Hard wrap at`, `Visual guides`, and `Wrap on typing`.
Ned has `FillColumn` and `fill-paragraph` but no automatic hard wrap while typing.

**Trailing whitespace strip.** Ned highlights it, never removes it. Should be an ordinary
on-save/on-format step, toggleable, with a "not on the line I'm editing" exception (Emacs'
own behaviour) so it never fights the cursor.

**Naming conventions (kind 7 + rename).** Per entity kind — class, struct, interface,
enum, enum member, function, method, field, public field, parameter, local, global,
constant, macro, namespace, type alias, lambda, property, event, template parameter — each
carrying a case convention (`<none>`, `lowercase`, `UPPERCASE`, `camelCase`, `PascalCase`,
`snake_case`, `Leading_snake_case`, `Upper_Snake_Case`, `SCREAMING_SNAKE_CASE`,
`lisp-case`), plus prefix/suffix and an abbreviations list.
- **We are closer to this than it looks.** `Editor/LocalScopes.h` already resolves a name
  to its binding and every occurrence, and `rename-symbol` already rewrites them all as
  one undo group with no LSP. A naming *checker* is `tags.scm`/`locals.scm` + a case test;
  a naming *fixer* is that plus the rename we have.
- Ship the checker first (a diagnostic), the fixer second (a code action). Never an
  automatic reformat step — renaming on save would be hostile.

**File naming conventions.** Not code formatting at all, and cheap: a per-language
source/header extension pair and a case convention for new-file names, plus the C/C++
header-guard template (`${PROJECT_NAME}_${FILE_NAME}_${EXT}`). Pure string work over
`ProjectRoot()` and the mode table, no parsing.

## B2 — Maybe

Real, wanted, but second-order: mostly they inherit from something in B1 or are one
language's take on a rule already covered.

**Alignment in columns (kind 5).** Consecutive assignments, declaration names, enum
initialisers, designated initialisers, bit-field sizes, end-of-line comments, PHP class
property/constant groups, SQL's several. Wanted — this is the single most
"opinionated codebase" feature in the list — but it is also the one that fights
`Keep existing line breaks` hardest, and it needs a notion of "adjacent lines forming a
group" that nothing else in the engine has. Worth doing, worth doing *after* B1 so the
group-detection rule is designed against a working engine rather than guessed.

**Import organisation (kind 8).** Sort imports, sort names within an import, group plain
vs `from` separately, case-insensitive sort, merge same-module imports, split/join.
Tree-sitter handles all of it — `imports.scm` already exists for 10 languages and names
exactly these nodes. What it *cannot* do is the resolution half (below, Tier C).

**Member arrangement (kind 8).** The Arrangement tab: an ordered list of matching rules
(`const`, `field`+static, `constructor`, `property`, `method`+static, `trait`,
`interface`, `class`) plus grouping rules (keep getters/setters together, keep dependent
methods together depth-first, keep overridden methods together). Structurally easy with
`tags.scm`; semantically it needs "which methods are dependent" (a call graph — doable
within one file via `locals.scm`, not across files) and "which are overridden" (needs the
base type — Tier C without LSP). **Ship the ordering; degrade the two grouping rules to
"only what this file can see" and say so.**

**Doc-comment formatting.** PHPDoc/JSDoc/Doxygen/Rustdoc: align parameter names, align tag
comments, blank line before first tag, wrap long lines, tag order, `@param` inner spacing.
The doc comment is one token to the host grammar, so this needs the doc-comment mini-parse
Ned would want anyway for `comment.documentation.*` highlighting (see
`HighlightCapabilities.md` §1). **Same work, two payoffs — that pairing is the reason to
do it.**

**Equivalence rewrites (kind 9).** Quote style, semicolon insertion/removal, trailing
comma (keep/add/remove), `elseif`→`else if`, `array()`→`[]`, short closures, keyword case,
`True`/`False`/`Null` case. Each is a tiny local edit on a captured node. Individually
trivial; collectively the thing that makes a formatter feel opinionated. The risk is that
some are *not* semantics-preserving in every dialect, so each ships off by default.

**Markdown/prose formatting.** Wrap long text, wrap inside block quotes, insert block-quote
arrows, format tables (have), force one space after header/list/blockquote markers, blank
lines around headers/block elements/paragraphs. Cheap, and Ned already has the table half.

**HTML-shaped rules.** Wrap attributes, align attributes, new line before first / after
last attribute, spaces around `=` in attributes, inline-element and
do-not-indent-children tag lists, keep-whitespace-inside tag lists. All expressible;
all need per-tag *lists* as config values, which is a config-shape the others don't have.

**Preserve-existing controls.** "Keep when reformatting: line breaks / comment at first
column / simple blocks in one line / simple methods in one line / control statement in one
line". These belong with B1's `Keep existing line breaks` philosophically but are
individually low-value; fold them into one `preserve` group.

## B3 — Probably not, or inherit

Not because they're hard — because they'd be noise.

- **Per-dialect SQL formatting** (the `SQL:2016` / Oracle / PostgreSQL / MySQL / Db2 / H2 /
  HSQLDB / MS SQL / SQLite / Sybase / Apache Derby sub-pages). The generic SQL rules are
  worth having if we ever bundle a SQL grammar; eleven dialect variants of them are not.
  **Inherit:** one SQL rule set, dialect ignored.
- **SQL's very long tail** — align multi-row `VALUES` percentile, minimal-increase
  threshold, break threshold, "collapse short multi-row values", postfix-option wrapping.
  Real features for a DB IDE; not for us. **Inherit:** the generic wrap policy.
- **Per-construct indent toggles that duplicate a brace rule** — "indent 'case' from
  'switch'", "indent 'break' from 'case'", "indent access specifier from class", "indent
  class member from access specifier". Each is already implied by the node's own
  `@indent`. **Inherit:** `indents.scm`, which is where they already live.
- **"Regular expression for macros starting/ending a block"** (C/C++). This exists because
  *JetBrains can't parse it either* — it is their regex escape hatch for macro-defined
  block structure. We'd be copying a workaround, not a capability. **Don't want.**
- **Framework-specific rows** — `Add for JSX attributes`, Angular/Blade/Twig/Smarty/Vue
  template panes, `JSX client component`. Out of scope until the grammar is bundled.
- **Code Generation tabs.** These are templates for IDE-generated code (getters, overrides,
  constructors), not reformatting. A separate feature, and one we may never want.
- **Scheme management** (Default-IDE vs Project scheme, "Set from…", "Revert changes").
  Ned's equivalent is init.janet plus `<root>/.ned/init.janet`, which already gives
  global-vs-project layering. **Inherit:** the config system we have.

---

# Tier C — cannot, with what we have

Split by whether we want it, each with the honest answer to "what would it take".

## C1 — Want, and know what it costs

**Semantics-dependent naming.** Distinguishing a *property* from a *field*, a *global*
from a *local* across files, an *overridden* method from a fresh one, or a *constant*
from a `const`-qualified variable, needs type/inheritance information no single-file parse
has.
> **What it takes:** LSP `documentSymbol` + `textDocument/typeHierarchy` (both already
> wired — see `project_lsp_call_type_hierarchy`). Design the rule set so each entity kind
> declares whether it is syntactic or semantic, and let the semantic ones simply not fire
> when no server is attached. That degradation is the same one `rename-symbol` already
> makes, so the precedent exists.

**Import resolution.** "Use paths relative to project/resource/sources roots", "use
directory import when index.js is available", "use file extension: auto", "use path
aliases", "do not import exactly from &lt;glob&gt;", "merge imports for members from the same
module". Sorting is syntax; *rewriting a path* is resolution.
> **What it takes:** either LSP (the server already resolves these — `documentLink`
> proved it, see `project_lsp_document_link_resolver`) or a per-ecosystem resolver reading
> `tsconfig.json`/`package.json`/`composer.json`. **Prefer the LSP route** — a second
> module resolver per ecosystem is a maintenance sink.

**Preprocessor-conditional code.** C/C++ `#if`/`#ifdef` arms. Tree-sitter parses one text;
the inactive arm is not a valid subtree, and formatting the active one silently corrupts
the other.
> **What it takes:** an explicit "never reformat inside a conditional-compilation region"
> guard, sourced from the existing `region.disabled` work in
> `HighlightCapabilities.md` §12. That is a *refusal*, not a capability — and it is the
> right answer. Recording it here so nobody tries to be clever later.

**SQL as a language at all.** Every SQL pane above is moot: no SQL grammar is bundled.
> **What it takes:** `ned_add_treesitter_grammar(sql ...)` plus the query set. Cheap
> mechanically. The question is whether SQL formatting is a thing this editor wants to own,
> not whether it can.

**Doc-comment inner structure.** As noted in B2 — a doc comment is one opaque token.
> **What it takes:** a second parse of the comment body. Either a tree-sitter grammar per
> doc dialect (upstream ones exist for some) or a small hand-rolled scanner per dialect,
> `TestOutputParser.h`'s shape. The hand-rolled route is probably right: the formats are
> small, stable, and documented.

**Idempotence and safety proof.** A formatter that isn't idempotent, or that changes
semantics, is worse than none.
> **What it takes:** a property test per language — `format(format(x)) == format(x)`, and
> `parse(format(x))` has the same node-kind sequence as `parse(x)`. Both are cheap to
> write against the existing tree-sitter layer and should exist from the first rule, not
> be retrofitted. This is the single highest-value item in Tier C.
>
> **Built** — `Tests/FormatterPropertiesTest.cpp`, over the oracle corpus, and each of
> the three properties it carries caught something on its first run. Idempotence caught
> `indent-buffer` writing four spaces onto a trailing empty line, so every save churned
> the file.
>
> The third property was not in the list above and had to be added: **the text of every
> multi-line string is byte-identical**. A docstring, a C++ raw string literal and a PHP
> heredoc had their interiors reindented, which edits what the program *says* — and the
> node-kind sequence is identical either way, because a reindented docstring is still one
> `string` node. Structural safety cannot see the one edit that is unambiguously illegal.
> Worth generalising before the next rule lands: a property that compares *structure*
> will not notice a rule that rewrites *content* inside a leaf.

## C2 — Don't want

**Type-aware type rewriting.** PHPDoc's "use fully qualified class names", "place `null`
in types", "reorder `null` in types". Needs name resolution to be correct, and gets it
wrong silently when it isn't.

**Cross-file reformatting as one operation.** Every pane here is per-file. The
project-wide equivalent already exists in a better shape — `ProjectReplace`'s editable
review multibuffer — and a "reformat the project" button that bypasses review would be
exactly the punishment this whole document is arguing against.

**Indents Detection.** JetBrains' "Settings may be overridden by Indents Detection" —
inferring style from the file and silently overriding your config. It is the single most
complained-about behaviour in that product. If we ever want it: make it a *command* that
writes a project config you can read and edit, never an invisible override. (Same stance
`ned-setup`'s generator already takes in `ROADMAP.md`.)

**EditorConfig as a silent override.** The PHP panes show "Settings may be overridden by
EditorConfig". Reading `.editorconfig` is fine and probably wanted; *silently outranking
the user's own config* is not. If supported, it should be an explicit precedence step the
user can see and reorder.

---

# What tree-sitter needs to grow

Nothing in the *core* — this is all query work, in the shape `indents.scm` already
established.

| New query | Purpose | Languages |
|---|---|---|
| `*-format.scm` | Names the nodes rules 2/3/4/6 attach to: delimiter pairs, operator classes, separators, list-shaped nodes, blank-line anchors, brace-carrying constructs | All 21 |
| `*-arrange.scm` | Names member kinds and their modifiers, for rule 8 | OO languages |
| Extend `*-tags.scm` | Entity kind per declaration, for naming rules | Has 3, needs 21 |
| Extend `*-imports.scm` | Import group/name sub-nodes, not just the target | Has 10 |

Plus one engine addition beyond the nine passes: a **format-region resolver** — given an
edit, the smallest node whose reformatting cannot affect anything outside it. That is what
makes the live path cheap and is the direct analogue of
`HugeStructuralWindow`'s windowing.

And one refusal, as above: a **do-not-format region** list, so conditional-compilation
arms, `// @formatter:off` regions, and heredoc/raw-string bodies are never touched.

---

# Appendix: pane coverage

Every distinct row from the 58 captured panes, mapped to a rule kind and a tier. Rows
that collapse to one name across languages are listed once.

### Tabs and Indents — *kind 1, Tier A*
Use tab character · Smart tabs · Tab size · Indent · Continuation indent · Keep indents on
empty lines · How to align when tabs are used · Align even if resulting indentation is too
large · Indent chained methods · Indent all chained calls in a group · Indent code in PHP
tags
*(the last three: kind 5, B2)*

### Naming — *kind 7, B1*
Enable 'Inconsistent Naming' inspection · Header guard style · Classes and structs ·
Concepts · Enums · Unions · Template parameters · Parameters · Local variables · Global
variables · Lambdas · Global functions · Class and struct methods · Class and struct
fields · Class and struct public fields · Union members · Enumerators · Other constants ·
Global constants · Namespaces · Typedefs · Macros · Properties · Events · Abbreviations
*(Properties/Events and any public-vs-private split: C1, needs semantics)*

### New File Extensions — *not code formatting, B1*
Source Extension · Header Extension · File Naming Convention *(`<none>` · lowercase ·
camelCase · PascalCase · snake_case · Leading_snake_case · Upper_Snake_Case ·
SCREAMING_SNAKE_CASE · lisp-case · UPPERCASE)*

### Braces Layout — *kind 3, B1*
Namespace declaration · Linkage specifications · Export declarations · Type declaration ·
Function definition · Lambda expression · Block under 'case' label · Requires expressions ·
Other statements and blocks · Other braces · In namespace · In class declaration · In
anonymous class declaration · In function declaration · In closure declaration · Place
namespace definitions on the same line · Empty braces formatting · Place braces for empty
classes/functions on one line · Keep abstract property hooks on one line · Keep simple
compound statements in one line · Allow comment after '{'
*(Regular expression for macros starting/ending a block: B3, don't want)*

### Blank Lines — *kind 6, B1*
Keep max in declarations / in code / after '{' / before '}' · Remove blank lines after '{'
and before '}' in declarations / in code · Around class/struct/enum definition · Around
function declarations · Around function definitions · Around single line function
definitions · Around namespaces · Around other definitions and declarations · Before /
After access specifier · After imports · After top-level imports · After local imports ·
Around class · Around field · Around property · Around method · Around function · Around
top-level classes and functions · Before the first method · Before method body · Before
'return' statement · Around class constants · Around enum cases · After opening tag ·
Before / After namespace · Before / Between group / After 'use' statements · Before /
After class body · Around header · Around block elements · Between paragraphs

### Line Breaks and Wrapping · Wrapping and Braces — *kinds 3 & 4, B1*
Line feed at end of file *(A)* · Hard wrap at · Wrap on typing · Visual guides · Keep
existing line breaks · Wrap long lines · Ensure right margin is not exceeded · Place
'else' / 'while' / 'catch' on a new line after a compound statement · 'while' on new line ·
'catch' / 'finally' on new line · Break line in single embedded statement · in simple
'case' statement · after goto labels · after member/top-level function definition/
declaration return type · after template<…> · after init-statement · before requires-clause ·
before/after colon in member initializer lists · before/after '->' in trailing return types ·
before function try block · before/after comma in member initializer lists · Return type on
new line · Prefer to wrap before ',' · before ',' in base clause · before '?' and ':' ·
before ':' · Wrap ternary expression · Wrap base classes list · Wrap constructor
initializer · Wrap formal parameters · Wrap invocation arguments · Prefer to wrap
before/after '(' / ')' in declaration / invocation · Prefer to wrap after '{' / before '}'
in initializer lists · Extends/implements/permits list and keyword · Function declaration
parameters · Function/constructor call arguments · Chained method calls · '.' / ';' on new
line · Place first call on new line when multiline · Take priority over call chain wrap ·
Binary expressions · Operation sign on next line · Assignment statement · Assignment sign
on next line · Ternary operation · Arrays · Objects · Variable declarations · ES6
import/export · List / Set / Dictionary literals · Parenthesized tuple expressions ·
Collections and Comprehensions · "From" Import Statements · Force parentheses if multiline ·
Force trailing comma if multiline · Use continuation indent · New line after '(' / '[' /
'{' · Place ')' / ']' / '}' on new line · Align when multiline · Hang closing brackets ·
Force new line after colon *(single/multi-clause)* · Keep ')' and '{' on one line · Place
'()' for constructor · Align named arguments · Treat multiline arrays/anonymous functions
as multiline · Force braces · Special 'else if' treatment · Indent 'case' branches · Indent
'break' from 'case' *(B3, inherit)* · Modifier list · Wrap after modifier list · Group
'use' · Compound type (union/intersection) · Attributes · Attributes for parameters ·
Function parameter / class / class field / class method decorators · Arrangement of
enumerations *(Keep existing · Max members on a single line · Place on single line · Wrap
enum definition)* · Keep when reformatting *(Line breaks · Comment at first column · Simple
blocks in one line · Simple methods in one line · Control statement in one line · Simple
classes in one line)* *(B2)* · Comments: Wrap at right margin · Align multiline · Align
inline comments *(B2, kind 5)* · PHP opening tag: New line after '<?php' · 'match'
expression: Align arm bodies · Short closure: Place '=>' on new line · Heredoc and nowdoc
placement

### Spaces — *kind 2, B1*
**Before parentheses:** function declaration · function call · anonymous function · arrow
function · method declaration · method call · left bracket · 'if' · 'for' · 'while' ·
'switch' · 'catch' · in function expression · in async arrow function · array initializer ·
lambda parameters
**Around operators:** assignment · logical · equality · relational · bitwise · additive ·
multiplicative · shift · power · unary additive · unary NOT · postfix · binary · arrow
function · concatenation · object access · null coalescing · assignment in declare · in
named parameter · in keyword argument · dot / '->' / '.*' / '->.' · '=' in alias
declaration and namespace alias · '->' in trailing return types
**Before left brace:** function · class · 'if' · 'else' · 'for' · 'while' · 'do' ·
'switch' · 'try' · 'catch' · 'finally'
**Before keywords:** 'else' · 'while' · 'catch' · 'finally'
**Within:** parentheses · empty parentheses · function declaration/call parentheses ·
control-statement parentheses · brackets · index access brackets · array subscript
brackets · grouping parentheses · object literal braces · uniform initialization braces ·
empty uniform initialization braces · array initializer parentheses · ES6 import/export
braces · interpolation expressions · angle brackets in template parameters / arguments ·
empty angle brackets · cast expressions · brackets around variable/expression · `<?=` and
`?>` · compound type parentheses
**Separators:** before/after comma *(function parameters · enum · base clause · template
parameters/arguments · call and initialization · uniform initialization braces ·
declaration of multiple variables)* · before/after 'for' semicolon · before semicolon ·
before/after ':' · before/after property name-value separator · before/after type
reference colon · base types list colon · bit-field declaration colon · constructor
initializer colon · return type colon · named argument colon · backed enum type
declaration colon · ternary '?' / ':' / between '?' and ':' · after '…' in rest/spread ·
before/after '…' in parameter pack · before/after '*' in generator · before '\' ·
before/after '#'
**Declarations (C/C++ pointer/reference):** before/after ptr in declaration of variable ·
of multiple variables · in return type · in nested declarator · in abstract declaration ·
and the same five for ref
**Other:** after type cast · remove extra spaces inside type cast parentheses · keep type
cast on same line as operand · around '|' in union type · around '&' in intersection type ·
before end of line comment · preserve spaces before end of line comment · within splicers ·
object literal type braces · union and intersection types

### Indentation and Alignment — *kinds 1 & 5*
Continuous line indent · Use continuous line indent in function declaration and invocation
parentheses / in initializer lists · Indent namespace members · linkage specification block
members · export declaration block members · access specifier from class · class member
from access specifier · member initializer list · if a function definition or declaration
is wrapped after the type · 'case' from 'switch' · goto labels · function declarations' /
method calls' / statement parentheses · Preprocessor directives indenting · Place comments
at the first column when commenting out code
**Align multiline construct:** Declarators in declaration · Function parameters · Call
arguments · First call argument by '(' · Initializer list arguments · Template parameters
in template declaration · Template arguments · Base classes in class base clause · Member
initializers in member initializer lists · Outdent commas · '?:' operator · Indent aligned
'?:' operator · Chained method calls · Outdent '.' and '->' in chained method calls ·
Chained binary expressions
**Align similar code in columns** *(B2)***:** Fix column alignment in adjacent lines ·
Assignments · Declaration names · Enum initializers · Designated initializers · Bit-field
sizes · End comments · Align consecutive assignments · Align properties in columns · Align
constants · Align enum cases · Align key-value pairs

### Punctuation — *kind 9, B2*
Use / Don't use semicolon *(everywhere · in code generated by IDE)* · Use single / double
quotes *(same scoping)* · Trailing comma *(Keep · Add · Remove)*

### Imports — *kind 8, B2 + C1*
Sort import statements · Sort imported members · Sort imported names in "from" imports ·
Sort plain and "from" imports separately within a group · Sort case-insensitively · Sort
imports by modules · Structure of "from" imports *(Leave as is · Join imports with the same
source · Always split imports)* · Sort 'use' statements *(Alphabetically · By length)*
*(all B2)*
Merge imports for members from the same module · Use paths relative to the project,
resource or sources roots · Use directory import when index.js is available · Use file
extension · Use path aliases · Do not import exactly from *(all C1 — resolution)*

### Arrangement — *kind 8, B2*
**Grouping rules:** Group property field with corresponding getter/setter · Group fields
initialized with arrow functions with methods · Keep getters and setters together · Keep
dependent methods together *(depth-first order)* · Keep overridden methods together
*(last one C1)*
**Matching rules:** ordered list over `const` · `field` *(+static, by modifiers)* ·
`constructor` · `property` *(+static)* · `method` *(+static, by modifiers)* · `trait` ·
`interface` · `class`

### Code Conversion — *kind 9, B2*
Convert True/False constants to lower/upper case · Convert Null constant to lower/upper
case · Convert Keywords to lower case · Convert else if / elseif · Add a comma after last
parameter in parameter list · after last variable in closure use list · after last argument
in function call · after last match arm · after last element in multiline array *(each
Always / When multiline)* · Force short array declaration style

### PHPDoc — *B2, gated on a doc-comment parse*
Keep blank lines · Blank line before the first tag · Blank lines around parameters · Wrap
long lines · Align parameter/property names · Align tag comments · '@param' spaces
*(between tag and type · type and name · name and description)* · Sort PHPDoc tags + order
list
Use fully qualified class names · Place 'null' in types · Reorder 'null' in types on
reformatting *(C2, don't want — needs name resolution)*

### HTML — *B2*
Hard wrap at · Wrap on typing · Visual guides · Keep line breaks · Keep line breaks in
text · Keep blank lines · Wrap attributes · Wrap text · Align attributes · Align text ·
Keep white spaces · Spaces around "=" in attribute · after tag name · in empty tag · Insert
new line before *(tag list)* · Remove new line before *(tag list)* · Do not indent children
of *(tag list, or if tag size more than N)* · Inline elements *(tag list)* · Keep white
spaces inside *(tag list)* · Don't break if inline content *(tag list)* · New line before
first attribute · New line after last attribute · Generated quote marks · Enforce on format
Add for JSX attributes *(B3)*

### Markdown — *B2, table half already A*
Hard wrap at · Wrap on typing · Wrap long text · Wrap text inside block quotes · Visual
guides · Keep line breaks inside text blocks · Insert block quote arrows · Format tables
*(A)* · Force one space between words / after header symbol / after list marker / after
blockquote marker · blank-line rows *(listed under Blank Lines)*

### Python-specific — *B1/B2*
Dict alignment · Format injected fragments · Add indent *(for injected fragments)* · Add
line feed at the end of file *(A)*

### SQL — *C1 (no grammar) then B3 for the dialect tail*
**Case:** Word case for Keywords · Identifiers · Built-in types · Custom types · Aliases ·
Built-in functions · Quoted identifiers · Use original case · Identifier quotation ·
Quotation character
**Queries:** Align the first word of clause · Place clause elements on · Place comma ·
Collapse short statement · Keep section elements under section header · Align section
elements · Align line comments at right of elements · and the per-clause repetitions for
INSERT/VALUES · UPDATE · WITH · SELECT · FROM · WHERE/HAVING · GROUP BY/ORDER BY ·
Subquery · Wrap the first / next JOIN · Indent JOIN · Place JOIN in join-only queries
under · Align joined tables · Align table aliases · Wrap ON/USING · Place ON/USING under ·
New line after ALL, DISTINCT · Keep elements on one line if ≤ N · Use AS · Align AS · Treat
asterisk as a regular element · Place top-level AND/OR · Align ASC/DESC · Align multi-row
VALUES *(+ with INSERT · percentile · minimal increase threshold · break threshold — B3)*
**DDL:** CREATE TABLE *(opening paren · elements · closing paren · collapse when short)* ·
ALTER TABLE *(wrap / indent / align altering instructions · altered items)* · CREATE or
ALTER TABLE *(align types · defaults · nullabilities)* · CONSTRAINT *(wrap CONSTRAINT ·
KEY/CHECK · REFERENCES · cascade and deferrability)* · CREATE SCHEMA *(indent content ·
min/max blank lines)* · Views *(wrap AS · wrap beginning of query · indent query)* ·
Postfix options
**Code:** Script *(wrap command-ending semicolon · add blank lines after commands)* ·
Imperative Commands · Declared variables *(wrap section/variables · align types/
assignments/expressions)* · Routine arguments · Routine statement *($$ wrapping)* ·
IF…THEN…ELSE · Loops
**Expressions:** Cortege · Binary expression *(spaces around operators · align operands ·
space within parenthesized sub-expressions)* · Function or procedure call · CASE clause
*(wrap/align WHEN · THEN · ELSE · END · collapse short clause)*

### Cross-cutting — *C2 / B3*
Scheme *(Default IDE · Project)* · Set from… · Revert changes · Disable formatting ·
Preview dialect · Settings may be overridden by Indents Detection · Settings may be
overridden by EditorConfig
