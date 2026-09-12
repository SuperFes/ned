# Parsing Engine: Unifying Grammars Behind a Trait Vocabulary

Design for replacing ned's per-language tree-sitter query corpus with a single
trait-based language-definition format, and -- later, and separably -- the
tree-sitter runtime underneath it.

Status: **Tier 0 is built and shipping; Tiers 1-2 and the language-definition
format are still a design sketch.** `Editor/Imprint.h` (the vocabulary),
`TreeSitter/GrammarImprint.h` (inference over `grammar.json`),
`Editor/ImprintTables.cpp` (the compiled-in result) and its two drivers --
`ImprintFold.h` and `ImprintBracket.h` -- are real code, and folding for 21
languages plus matching-bracket lookup run on them. Everything from
"Tier 1 -- declared concepts" onward is unbuilt. Every number
below was measured against this repo's own checkout rather than estimated, so
that a later reader can re-run the same counts and see what drifted. Re-counted
2026-09-11 after the fold work below: 24 fetched grammar repos under
`build/_deps/`, **77** `.scm` files under `Source/Editor/TreeSitter/queries/`
and **105** embedded query constants in `Source/Editor/TreeSitter/Queries.h`.
Both numbers were written here as 79 and 113; the corpus counts drift on their
own, which is the argument for re-running them rather than quoting them.

Ground-truthed against `CMakeLists.txt`'s grammar functions,
`Source/Editor/TreeSitter/` (the RAII wrapper), `Source/Editor/Mode.h` (the
capability surface every consumer goes through), and the fetched grammars'
own `src/grammar.json` files.

## The one architectural fact everything follows from

**A tree-sitter grammar carries structure and no meaning; a `.scm` query
carries meaning and no structure; nothing connects or validates the two.**

Four languages are involved in defining one language -- `grammar.js`
(JavaScript) becomes `grammar.json` (data) becomes `parser.c` (C), and the
meaning is bolted on afterwards in `queries/*.scm` (a Scheme-ish pattern
language). None of the four can see the others. Rename a node upstream and
every query naming it silently returns nothing: not an error, not a warning,
an empty result that looks exactly like "this file has no classes in it."

This repo has the scars. `tree-sitter-typescript`'s upstream `tags.scm` is a
*delta on JavaScript's* and carries no `class_declaration` at all, so a
TypeScript buffer had no class or function markers whatsoever until
`ned_embed_treesitter_query_concat` was invented to staple the JavaScript file
in front of it. `definition.module` had to be resolved to `SymbolKind::Namespace`
rather than `TypeLike` by hand, after the fact, because nothing declares what a
capture *means*.

Everything below is downstream of closing that gap: **one artifact per language
where structure and meaning are declared together, compiled together, and
validated together**, so a trait travels with the rule it is attached to and an
unsatisfiable trait is a build error rather than an empty runtime result.

## What the grammars actually look like

Measured across the 30 grammars in `build/_deps/*/src/grammar.json`:

- **1,391 distinct visible rule names**, of which **61% appear in exactly one
  grammar**. Only 83 names are shared by five or more grammars, and the shared
  ones are shared by convention rather than by contract: `comment` in 19,
  `escape_sequence` in 16, `if_statement` in 13, falling off a cliff from there.
- There is **no common vocabulary**. There are 114 distinct node names meaning
  "a delimited body" across 18 grammars -- `compound_statement`, `block`,
  `declaration_list`, `class_body`, `field_declaration_list`, `switch_body`,
  `accessor_list`, `match_block`, `literal_value`, `enum_variant_list`,
  `interface_type`, and a hundred more. Likewise **52** names for "parameter",
  **44** for "string", **43** for "import".

So ned's `.scm` corpus is not a set of queries. **It is the missing vocabulary,
hand-written once per language** -- an adapter from whatever that grammar's
author happened to name things to what the editor actually needs.

## What that costs today

### The same fact, restated per driver

`folds.scm` and `indents.scm` are, for every C-family language, *the same node
list*:

```
language      fold nodes  indent nodes  IDENTICAL
c                      1             3          1  100% of fold list
clojure                6             4          4   67%
cpp                    2             4          2  100%
csharp                 7             7          7  100%
go                     7             7          7  100%
java                  10            10         10  100%
javascript             3             7          3  100%
json                   2             2          2  100%
kotlin                 7             7          7  100%
python                 1            11          1  100%
rust                   6             6          6  100%
typescript             3            13          3  100%
--------------------------------------------------------
TOTAL                 55            81         53   96%
```

**96% of every fold rule is restated verbatim as an indent rule.** And 80 of
the 102 `@dedent` captures (**78%**) are mechanically
`(X close-token @dedent)` for an `X` already in that file's `@indent` list --
pure derivable boilerplate.

One fact -- *this node is a delimited body* -- yields the fold range, the
indent scope, the dedent trigger, the sticky-scroll container, the
structural-selection step and the brace match. It is currently stated two to
three times per language, across 21 languages, each restatement carrying a
hand-written comment explaining which name this particular grammar chose for
the concept.

### The N x M matrix, and its hole

`Queries.h` declares 105 embedded query constants across 29 languages and 8
driver kinds:

```
29 languages x 8 driver kinds = 232 possible adapter files
105 written  ->  127 GAPS (55% of the feature matrix)

Highlights  29/29      Indents  21/29      Locals  15/29
Imports     12/29      Folds    11/29      Tags    11/29
Tests       10/29      Injections 3/29
```

The Folds column reads 11 rather than 12 because `python-folds.scm` is gone
(see "Where a fold starts" below) -- and the *coverage* it measures went the
other way, since 21 languages fold from the imprint whether or not a query
exists for them. A gap in this matrix stopped meaning a missing feature the
moment a driver could read the grammar directly, which is the whole argument
in one cell.

Highlights is the only column at 100%, and only because upstream ships
`highlights.scm` for us. **Every column ned has to author itself is between 34%
and 72% empty**, and each empty cell is a language silently missing a feature
with nothing anywhere to say so. That is not a backlog; it is the resting state
of an N x M architecture maintained by hand.

### The corpus is already ours, which is what makes replacing it cheap

Of the 111 `.scm` references embedded by `CMakeLists.txt`, **79 (71%) are
ned-authored** files under `Source/Editor/TreeSitter/queries/`. The only
upstream query files consumed at all are `highlights.scm` and `tags.scm` -- 32
references, two kinds -- and four of the `tags.scm` consumptions only work via
`ned_embed_treesitter_query_concat`, which exists precisely *because* upstream
was incomplete.

This matters for sequencing. The usual objection to replacing a query layer is
"you forfeit the upstream query ecosystem," and here that is mostly already
paid: ned has been hand-authoring this vocabulary since the beginning, one
language at a time, because upstream either did not ship it (no `folds.scm`
exists for C, C++, Go, Rust, Java or C#) or shipped it broken. Phase 2 is
therefore not a migration away from someone else's corpus -- it is replacing
*our own* hand-written corpus with a generated one, and the 71% is the share we
can change freely without coordinating with anyone.

What ingestion preserves is upstream **grammars**, not upstream queries. Those
are separate dependencies and only the first one is load-bearing.

### Packaging

Secondary to the above, but real:

- **24 grammar repositories** shallow-cloned at CMake configure time.
- **4.3M lines** of generated `parser.c`, compiling to **26.2 MB** of static
  libraries (`ned`'s own text segment is 31.9 MB).
- Compile cost is *not* the crisis: the largest grammar
  (`tree-sitter-c-sharp`, 824,799 lines) builds in **2.4s** at `-O2`, so the
  whole set is roughly 30-60s.
- **Three tree-sitter ABI versions are in-tree simultaneously** -- janet-simple
  at 13, twelve grammars at 14, eleven at 15. This only gets worse as grammars
  are bumped independently.

### The coupling is already right

The one thing that is *not* a problem, and the reason any of this is tractable:

- **Four files** include `tree_sitter/api.h` (`Node.h`, `Parser.h`, `Query.h`,
  `Tree.h`).
- **Seventeen** source files touch the `TreeSitter/` wrapper at all.
- `Mode`'s `std::function` capability surface is a genuine firewall -- every
  consumer goes through `highlight`/`fold`/`symbolKind`/`testDiscovery`/
  `importTargets`/`indentColumn`/`localScopes`/`embeddedRegions`, none of which
  mention a tree-sitter type.

A runtime swap does not touch the editor. That precondition is already met and
is worth not regressing.

## Vocabulary

Three words carry the design, and they are not interchangeable. The first
describes the ordinary case; the other two name the exceptions, which is why
they are the sharper words.

- **grain** — structure legible from a construct's own shape. The ordinary
  case, and the one Tier 0 reads: 11 of the 15 bundled `locals.scm` files
  dispatch purely on shape, with zero text predicates.
- **burl** — a language or region whose grain is systematically tangled, where
  structure has to be read from *content* instead. Janet, Clojure and Fish are
  the bundled ones (16, 15 and 6 text predicates against zero for C, Go, Java,
  Python and the rest). In a Lisp, `(let [x 1] ...)`, `(defn f [a] ...)` and
  `(println x)` are the same node type and every binding form is a macro the
  grammar never heard of. Not a special case to escape — a different reading
  strategy for the whole material.
- **knot** — a single point where the declarative rules do not reach and a host
  callout is needed. Org's `*`-counted heading level, its runtime-configured
  TODO keywords. Discrete, and rare by design.

The term earns its keep by being diagnostic rather than decorative: the burls
and the open problems are the same list. The two oldest items under
"Refactoring" in `ROADMAP.md` are the Lisp binding-vector cliff and Fish's
`set -l -x count 0`.

**The 55/55 Tier 0 result is a grain result.** It does not transfer to burls,
and should not be quoted as though it does.

In code the vocabulary is `ned::editor::imprint` — what a language's structure
leaves behind, read rather than authored. `Editor/Imprint.h` holds it and knows
nothing about tree-sitter; `TreeSitter/GrammarImprint.h` is the half that reads
an imprint out of a `grammar.json`. That split is the Phase 4 seam: replacing
the engine replaces the reader and leaves the vocabulary untouched.

## The design: traits, not node names

Three tiers. The first one is where most of the value is.

### Tier 0 -- inferred structure, zero per-language work

A rule whose production is `open_token . repeat(X) . close_token` **is** a
delimited body. Nobody has to declare it: it is visible in `grammar.json`.
The same holds for token spans, nesting depth, trivia attachment and matched
delimiters.

Folds, indent, dedent, brace matching, structural selection and sticky-scroll
containers are therefore derivable **for every grammar, including grammars that
do not exist yet, with no rules written at all**.

**Measured: 60 of 60, with no per-language rules.**

Reported as 55/55 and then 56/56 for most of a day, and both were wrong. The
ground truth was extracted from the `.scm` files with a regex bounded by
`[^()]*`, which cannot cross a nested group and so could not see the
*conditional* capture form -- `(function_body "{") @fold`, which folds a Kotlin
function only when it has a brace body rather than `= expr`. Kotlin reported 7
hand-written fold nodes when it has 10. The extractor now scans back from each
capture balancing parens; the real corpus is 60, and inference reproduces all
of it. A measurement is a claim about the thing measuring, too.
`Tests/ImprintTest.cpp` is the experiment and the enforcement both -- the
number is re-derived on every build rather than trusted. Three refinements were needed beyond the
naive "a SEQ that opens and closes with a literal", and each is a real property
of how grammars are written rather than a fudge:

- **Hidden rules must be inlined.** tree-sitter inlines `_`-prefixed rules
  rather than making them nodes, so a grammar is free to put a node's real
  delimiters inside one. Clojure does exactly that -- `list_lit` is
  `SEQ[REPEAT(_metadata_lit), _bare_list_lit]`, with the parens one level down.
  Without inlining, all six Clojure fold nodes are invisible.
- **A closer may be followed by optional members.** JavaScript's
  `statement_block` is `SEQ['{', REPEAT(statement), '}', <optional>]`, so
  requiring the closer to be the literal last member misses it.
- **Indentation languages close with an external token and have no opener.**
  Python's `block` is `SEQ[REPEAT(_statement), _dedent]`; the `_indent` is
  consumed by the parent. This is a second delimiter shape, not a special case.

### The correction this measurement forces

The exit criterion was originally written as "reproduce the hand-written fold
nodes", which conflated two different things. Inference also reports **211 nodes
beyond** the hand-written 55 -- `parenthesized_expression` (9 languages),
`argument_list` (6), `parameter_list` (4), `string_literal`, `index_expression`.

Every one of those is genuinely delimited. None is something to fold.

So Tier 0 does not infer `Foldable`; it infers **`Delimited`**, which is the
honest structural fact. `Foldable` is a *policy* on top of it -- and the useful
part is what that policy costs: instead of naming 55 node types across 12
languages, it excludes a handful of shapes, most of them cross-language
("a parenthesized expression is not a fold"). That is the N x M to N + M
inversion showing up in the first tier that was actually built, which is better
evidence for the architecture than the recall number is.

The same split should be expected everywhere below: Tier 0 reports structure
with high recall and no taste, Tier 1 applies the small amount of taste.

### How much of it turned out to be taste

Measured across 11 bundled grammars once inference carried the structural
signals rather than just the delimiter kind:

```
244  delimited bodies inferred
185  foldable with argument/parameter lists ON   (the default)
144  foldable with them OFF
```

So **59 are excluded by structure** -- a body holding exactly one
subexpression (`parenthesized_expression`, `decltype(x)`, `index_expression`)
has nothing to collapse, and that is not a preference -- and **41 are governed
by taste**, all of them the argument/parameter-list family.

That 41 is the honest size of the judgement call, and it is a single flag
rather than a per-language list. `FoldPolicy::foldArgumentLists` defaults ON:
a long or overloaded signature is exactly where collapsing parameters helps,
and the hand-written queries that omit them describe themselves as
"deliberately minimal" rather than as having ruled them out. The decision is
deliberately cheap to reverse, and `Tests/ImprintTest.cpp` pins both
directions so neither can rot.

Two things the policy deliberately does *not* decide. Whether a body spans
more than one line -- that is a property of the text rather than the grammar,
and `Editor/CodeFold.h` enforces it. And `cpp`'s `declaration_list`, which
inference reports and `cpp-folds.scm` omits: that looks like an omission the
inference just caught rather than a considered exclusion, but it is recorded
as an open question rather than quietly decided.

### Where a fold starts is a different question from what folds

Structure says *which* nodes are foldable. It does not say which row a fold
begins on, and every consumer in this codebase already assumed an answer:

> A fold block's start byte sits on the row that stays visible when the block
> collapses.

A bracket body satisfies that for free, because its `{` is written on that row.
An indentation body does not: Python's `block` is `SEQ[REPEAT(_statement),
_dedent]` and starts at the *first statement*, one row below the `def f():`
that names it. Nothing was wrong with the inference -- the node range is
exactly the body -- and everything downstream was wrong anyway. The gutter
affordance sat on the body's first line, collapsing left that first statement
on screen and hid the rest, and a one-statement body was not foldable at all
because its start and end landed on one row. YAML folded from `  a: 1` instead
of `root:`; the same three rows off, in a language with a completely different
grammar.

`Editor/Imprint.h`'s `FoldAnchorStart` restores the invariant rather than
teaching each consumer a second rule, and what it needs is one grammar fact and
two text facts:

- **the node has no introducer of its own.** Python's `if_statement` also
  closes with a dedent, but `if x:` *is* its own header row; anchoring it
  upward moves its fold onto the enclosing `def` and loses it there. The
  grammar states this plainly -- `block` begins with a repeat of content,
  `if_statement` with the literal `if`, Python's `string` with the scanner's
  own opening quote -- so `openerIsFirst` now answers it for both delimiter
  kinds instead of being hardcoded `true` for indentation bodies.
- **it starts its own line**, and
- **it is indented deeper than the nearest non-blank line above.** TOML's
  `pair` carries no introducer either, but `key = [` sits at its own
  indentation and there is nothing above it to borrow.

The split is the same one the tiers are built on: *what shape is this* comes
from the grammar, *how was this written* comes from the text, and neither can
answer the other's question.

Three further text-level rules turned up with it, and all three live together
in `CodeFold.h`'s `NormalizeFoldBlocks` -- the single point every consumer of
every fold source comes through:

- a fold must span more than one line (already there);
- a block's end is trimmed back over trailing whitespace, because a node range
  can run past its own last line -- TOML's `table` ends where the next
  `[header]` begins, so "hides through the closing line" hid that header;
- one fold per start byte, the innermost, because a fold marker's key *is* a
  start byte and two blocks sharing one let the toggle and the hidden range
  name different blocks.

And one rule that was already there had to learn rows. `SupersededByChildBody`
("fold the body, not the declaration") dropped a node whose foldable child ends
where it does -- correct when both fold from the same row, which before
anchoring they always did. Afterwards a Python `for` suite and the `if` nested
inside it end at the same byte but fold from *different* rows, so by byte alone
every level superseded the one above it and a whole function collapsed to its
innermost statement. A node that introduces itself still hands its fold to that
child outright; a node whose row was borrowed yields only to a child whose fold
starts at or above its own. Both halves are load-bearing: the first keeps
Allman-braced C# folding from its `{` exactly as before, the second removes
YAML's `block_node` wrapper.

The payoff is that `python-folds.scm` is **deleted**. It said `(block) @fold`,
which is what the imprint says, with geometry a node range cannot express on
its own. That makes it the first hand-written query this work removes rather
than reproduces, and the corpus gate's hand-written total drops 60 -> 59 --
the direction that number is supposed to move.

### Fold sources compose; indent sources do not

The additive-layer principle below ("a source asserts, never denies") is what
lets a hand-written `folds.scm` and the delimiter imprint coexist with no
precedence rule. Wiring the same imprint into the *indent* driver as a second
source, and diffing it against every hand-written query over the corpus, shows
the principle has a boundary:

```
12 of 16 languages          agree line for line
 3 over-indent              cpp, toml, yaml
 1 has no query to compare  (tsx -- it was sharing TypeScript's)
```

The 12 are the encouraging half: every container the imprint adds and the
query omits -- argument lists, parameter lists, parenthesized expressions --
lands on a row the query's own rules already counted, so the answer does not
move. But the 3 are not noise, and they fail for one reason:

**A fold source contributes an assertion; an indent source contributes a
quantity.** "This range is foldable" cannot contradict another source. "+1
level here" changes the answer, and a union has no way to say *minus*.

Each of the three is a place where a language deliberately states that a real
delimited body does **not** indent:

- C++'s outermost `namespace` body. `cpp-indents.scm` indents a
  `declaration_list` only when it is nested inside another namespace -- a
  predicated rule, written that way on purpose.
- TOML's `table` and `pair`. A TOML file is flat under its `[headers]`.
- YAML's `stream` and `document`. They span the whole file, so counting them
  indents column zero.

None of these is a gap in the grammar's structure; all three are conventions
about layout, which is Tier 1's half of the split. So the Indents column
**cannot** reach 29/29 by inference the way the Folds column did, and the
Phase 2 exit criterion should say so rather than assume symmetry between two
drivers that look alike from the outside. A trait format that wants both needs
suppression to be expressible -- which is a real design requirement, and one
worth knowing before the format is written rather than after.

The wiring itself was reverted rather than shipped: with the union unsound and
every bundled language that has a table already carrying an indent query, it
had no caller.

### What that measurement found instead: JSX had no indent rules at all

`.tsx` and `.jsx` computed one flat level for a whole tree of elements. JSX
nests by matched tags, so the delimiter imprint contributes nothing (the HTML
limit, exactly), and `typescript-indents.scm` -- which TsxMode was sharing --
contained no `jsx_*` rule of any kind. Fixed by hand, modelled on
`html-indents.scm`, which had solved the identical shape.

Two things came out of it that generalise:

**`grammar.json` declares node types the compiled parser rejects.** The
`typescript` dialect lists every `jsx_*` rule; its parser knows none of them.
A query naming an unknown node type fails to *compile*, which takes the whole
mode's indentation down rather than just the offending rule -- so the two
dialects cannot share one query file however similar they look. The file that
answers to the parser is `node-types.json`, not `grammar.json`; this document
has now been wrong about that in both directions (see the `alias()` result
above, where grammar.json was the file that knew more).

**Two of the four JSX captures are covered by the imprint and two are not**,
and the split is exactly the delimiter fact: `jsx_expression` (`{...}`) and
`jsx_opening_element` (`<...>`) are bracket pairs, `jsx_element` and
`jsx_self_closing_element` are tag pairs. `Tests/ImprintTest.cpp` names those
two as the standing exception to "the imprint covers every hand-written
`@indent` node" rather than loosening the gate to a count.

### Tier 1 is not inferable, and that is now measured rather than assumed

Tier 0's result was good enough to raise the obvious question: if delimited
bodies fall out of the grammar, do *callables* and *types* too? Two hypotheses
were tested against the 14 node types the hand-written `tags.scm` files mark
`@definition.function` across 9 languages. Both are refuted, and recording that
is the point -- otherwise it gets retried.

**"A callable has a parameter list and a body."** Structural, and far too broad:
11 to 41 candidates per language against a ground truth of about two, sweeping
in `call_expression`, `for_statement`, `catch_clause` and `attribute`. Every one
of those has a paren-delimited list and a body. Shape does not separate a
*declaration* from a *call*.

**"A declaration binds a name, and `grammar.json` records `field("name", ...)`."**
Much sharper -- candidates drop to between 0 and 14, and the survivors are
recognisable: `function_declaration`, `method_declaration`, `class_declaration`,
`function_item`. But recall against the ground truth is **5 of 14, about 36%**,
and the misses are not a long tail to be tidied up:

- **C and C++** put the name behind `declarator:`, nested inside
  `function_declarator`, so `function_definition`'s own production has no `name`
  field at all.
- **Kotlin** does not use `field("name")` anywhere; zero candidates.
- **JavaScript**'s callables are *assignment* shapes -- `assignment_expression`,
  `pair`, `variable_declarator` -- because `const f = () => {}` is a binding of a
  function, not a function declaration. No amount of declaration-shaped
  inference finds that.

36% recall is worse than useless for a driver: you would ship both mechanisms
and the inferred half would be the one nobody trusts.

The reason is not incidental. Tier 0 asks *what shape is this*, which a grammar
states completely. Tier 1 asks *what does this mean* -- specifically "is this
name being introduced or used", which is the same question `locals.scm` exists
to answer and which no production records. The tiers are split along exactly the
line where grammars stop carrying the answer.

**So the N x M win is asymmetric, and the honest version is worth stating.** For
Tier 0 it is a genuine reduction in authoring: 21 languages fold, nine of which
had nothing, with no per-language rules written. For Tier 1 it is *not* fewer
things to author -- a declaration is about as much work as the `tags.scm` line
it replaces. The win there is **one vocabulary consumed by many drivers**
instead of one query per driver, which is real but smaller, and it should not be
sold as the same result.

### Neither is "this body is verbatim", and that one has a different answer

A second inference question, raised by wiring the imprint into a second driver: if
Tier 0 knows a node is a delimited body, does it know whether that body's
interior is *text* rather than *structure*? It matters immediately — an indent
rule that treats a Python docstring, a C++ raw string literal or a PHP heredoc
as an indent container rewrites the value of the string.

The hypothesis worth testing, because it is the only structural one available:
**a body is verbatim when its interior members are raw text** — a PATTERN, a
TOKEN, or a symbol resolving to one — rather than references to other rules.
Measured across 17 grammars, it is **refuted in both directions**:

```
missed  (real verbatim bodies it does not report)
        rust raw_string_literal, kotlin string_literal, php heredoc + nowdoc,
        csharp raw_string_literal + interpolation, yaml block_scalar +
        double_quote_scalar + single_quote_scalar, cpp raw_string_literal
swept in (ordinary code it reports as verbatim)
        c/cpp goto_statement + parameter_declaration, go for_statement,
        java annotation + break_statement, javascript new_expression,
        php namespace_use_clause, css supports_statement
```

The misses are the interesting half and they are all one shape: a language whose
strings need a *scanner* — heredocs matched against their own terminator, raw
strings against a counted delimiter, YAML block scalars against indentation —
spells them as external tokens, and an external is an opaque name in
`grammar.json` with no content to read at all. The very bodies whose interiors
are most emphatically text are the ones whose text-ness is invisible to the
grammar.

So this is Tier 0's boundary again, in the same place as callables and types:
the grammar states *shape*, and "these bytes are data, not code" is not shape.

But unlike Tier 1, this one has an answer already in the tree, in the one column
of the driver matrix that is full: **`highlights.scm` spans every string in every
bundled language**, and `SyntaxClass::String` is that fact already extracted.
`Editor/Indent.h`'s `VerbatimRanges` reads it there, and `RenameReview.h`'s
`ClassifyHit` had already made the same move for comments and strings a feature
earlier. Worth stating as a rule rather than a coincidence: **when a fact is not
inferable from structure, look for a query that already states it before
authoring a new one.** The trait vocabulary should expect to *import* facts from
the highlight layer, not only to replace it.

### Tier 1 must reuse the capture-name model, not invent an enum

`Docs/HighlightCapabilities.md` settled this for highlighting and the same
answer applies here, so recording it before Tier 1 is built rather than after.

Ned already resolves a **dotted capture name most-specific-first, one segment
at a time, per style field** -- `function.method.static.call` falls back through
`function.method.static`, `function.method`, `function`, with
`ResolvedCaptureOverride` keeping the first value it finds for each field
independently. That is JetBrains' "Inherit values from" checkbox, already
implemented, already per-attribute.

Three consequences from that document carry over to traits unchanged:

1. **A name costs nothing until someone uses it.** An unstyled -- or here,
   unconsumed -- leaf is byte-for-byte its parent. There is no per-name table
   to populate.
2. **The list is the contract, not the implementation.** A grammar declares a
   name; a driver may or may not have an opinion about it. The two never have
   to agree.
3. **Depth is free but not weightless.** Three segments is the working ceiling.

So Tier 1 traits are **dotted names over the existing resolver**, not a closed
`enum class Trait`. `definition.function.method` and `local.definition.parameter`
are the shape -- and note both already exist, in `tags.scm` and `locals.scm`
respectively, so this is adopting a convention the corpus already follows
rather than imposing a new one. A driver asking for `definition` gets every
kind of definition; one asking for `definition.function.method` gets exactly
that; neither needs the other to have been anticipated.

An enum would forfeit all three properties and force every language to be
enumerated against every trait -- which is the N x M shape this whole design
exists to escape. Worth being explicit about, because a flat enum is the
obvious first thing to reach for and is expensive to unwind later.

### The additive-layer principle is shared, and is already load-bearing here

That document also names the one mechanism ned lacks for highlighting: a
`HighlightSpan` is winner-takes-all per byte range ("later span wins"), so a
`region.*` marker painted over a function name *replaces* its styling instead
of tinting it. Supporting `region.disabled`/`region.generated`/`region.injected`
properly needs **a second, additive span layer** contributing background and
effects only, never a foreground.

Fold sources already work that way, and deliberately: `MergeFoldSources` takes
the union because a fold source asserts "this range is foldable" and never the
negative, so two sources cannot contradict each other, only cover different
ground. That is what lets the imprint and a hand-written query coexist with no
precedence rule, and what lets a language the imprint cannot read (HTML's
tag-pair elements, YAML's scanner-carried block structure) contribute nothing
without any special case saying so.

The same shape is the answer for `region.*`. Whoever builds that layer should
build it as additive composition rather than another winner-takes-all pass, and
can take the fold-source design as the worked precedent.

### Tier 1 -- declared concepts, ~30-40 traits, once per language

`Binding`, `Reference`, `Scope`, `Callable`, `TypeDecl`, `Parameter`,
`ImportSpecifier`, `String`, `Comment`, `DocComment`, `TestDecl`, and the rest
of the vocabulary the drivers actually consume.

Two properties carry the design:

- **Traits compose.** A node can be `Body + Scope + Callable` at once. That is
  what lets one declaration feed six drivers instead of being copied into six
  files.
- **The set is open, not a closed enum.** A language declares the traits that
  fit and omits the ones that do not. A driver asking for an absent trait gets
  a principled empty answer -- which is the same *outcome* as today's silent
  hole but a knowable one, reportable per language rather than discovered by a
  user.

### Tier 2 -- escapes, first-class

Arbitrary predicates, computed values, and host callouts into Janet.

This is where the two things that currently force a hand-built C++ mode against
a *forked grammar* belong: Org's heading level is a count of `*` characters
(arithmetic, which no query predicate can express) and its TODO-vs-DONE
resolution compares captured text against `org::TodoKeywords()`, a list
configured from Janet **at runtime**, which a statically-compiled `.scm` can
never reach.

Making the escape hatch a supported tier rather than a workaround is the honest
part of this design. A vocabulary that claims to cover everything is a
vocabulary that will be lied to.

### The inversion

Today: N x M adapter files, maintained by hand, 51% missing.

After: **N mappings + M drivers.** 29 + 8 = 37 things to write, and no gaps by
construction. A driver shipped tomorrow works on all 29 languages the day it
lands; a language added tomorrow gets all 8 drivers the day it lands.

That is also the pluggability answer. Drivers register the way
`Vcs/Provider.h` providers and `TestRun/Config.h`'s parsers already
do, including from Janet. A driver declares which traits it consumes, so the
registry can report -- statically, at load time -- which languages can serve it
and which cannot.

## The authoring format: Janet, not a Scheme dialect

**ned's own language definitions are written in Janet** -- both the Tier 1 trait
declarations and the Tier 2 escapes -- rather than in a tree-sitter-style `.scm`
query file. Decided; recorded here because the syntax would otherwise look
inherited rather than chosen.

Janet is already the extension language, and it is homoiconic, which is what
makes one file serve both tiers without the split feeling arbitrary:

- **The declarative tier is plain Janet *data*** -- tuples, keywords, symbols --
  that nothing evaluates. It reads like an S-expression query because it is
  one. A pattern file is still just data on disk.
- **The escape tier is Janet *code***, and therefore gets a real language for
  free: arithmetic over captured text (Org's heading level is a count of `*`),
  quantifiers with index parity (the Lisp binding-vector cliff), and access to
  runtime host state (Org's TODO keywords, configured from Janet and therefore
  invisible to any statically-compiled query). Each of these is a thing
  tree-sitter's predicate system structurally cannot express, not a thing it
  merely lacks.

This is the established escape-hatch shape in this codebase rather than a new
invention: `ned/register-test-parser` wraps a Janet fn that shadows a built-in
parser, `Janet/JanetVcsProvider.h` implements a C++ interface from Janet
callbacks, `ned/register-snippet` likewise. Janet over a compiled C++ core is
how the rest of ned already extends.

### What stays `.scm`, and why

An S-expression *reader* is kept, but only to **consume upstream** -- it is an
input format, not an authoring one. Measured against `CMakeLists.txt` on
2026-09-11, ned consumes **32 query files it does not write**: 20
`highlights.scm`, 9 `tags.scm`, 3 `injections.scm`. Highlighting is the one
driver column at full coverage precisely because upstream ships it, so dropping
the ability to read those files means re-authoring highlighting for 29
languages to buy nothing.

`/usr/bin/tree-sitter` is also installed on this machine, and `tree-sitter
query` validates a pattern against a real grammar without a ned rebuild --
worth keeping reachable for the upstream files, since it is the fastest
authoring loop available.

### Two disciplines that make this hold up

**Keep the declarative tier pure data.** Nothing in a Tier 1 file should need
evaluating to be *loaded*. That is what keeps the compiled artifact mmap-able
and runtime-loadable, keeps per-keystroke cost at zero for a language that
declares no escape, and keeps the whole 29-language corpus independent of which
scripting runtime ned happens to embed.

**Declare escapes, do not embed them.** A grammar file should *name* a host
predicate; the host supplies it. The escape then lives in the user's own
`init.janet`, not in the grammar corpus. Org already works this way and is the
model: `org::TodoKeywords()` is a process-wide setting with a `ned/set-*`
binding, and `Editor/Org.h`'s pure parsing functions deliberately do not read
it.

Together these mean a future scripting-runtime change touches the binding layer
and a countable handful of host predicates -- never the languages. Relevant
because `Docs/JankFeasibility.md` is a live Maybelist item: jank is also a Lisp,
so the declarative tier would carry over as-is, but escapes are *code* and would
be a rewrite (different standard library, different collections, different
embedding contract entirely -- see `Janet/Value.h`'s own CAUTION for how
specific an embedding contract gets). Shared syntax is not shared semantics. The
fewer escapes exist, the smaller that bill -- which is the real argument for
pushing Tier 1's expressiveness hard rather than reaching for Tier 2.

Janet-only is the right scope now. Nothing above depends on a second runtime
ever arriving.

## The risk worth arguing about before starting

**A universal vocabulary can collapse into lowest-common-denominator mush.**

The three tiers are the defence. Tier 0 is *inferred*, so it cannot be wrong
about a language it has never seen. Tier 2 means no concept ever has to be
forced into a shape that does not fit. But **Tier 1 is where judgement lives**,
and it is where this design either works or quietly degenerates into another
hand-maintained translation table wearing a better name.

The honest early test is **Lisp**, because it is the case that already broke.
`queries/clojure-locals.scm` documents the cliff in its own header: a Lisp
grammar carries no semantic structure at all -- `(let [x 1] ...)`,
`(defn f [a] ...)` and `(println x)` are the same node type, a `list_lit` of
children, and every binding form is a macro the grammar has never heard of.
The binding vectors are therefore unrolled by pair index, and *"the unrolling
stops at eight pairs"*: a ninth binding in one vector is not captured, and a
use of it is silently unrenameable.

If Tier 1 plus Tier 2 can express `(let [a 1 b c] ...)` correctly and without a
cliff, the vocabulary is real. If it cannot, that is worth finding out at
language 3 rather than at language 15.

## What this does and does not require

Phases 1-3 below run **on tree-sitter's existing tree**. They need no new
parser. That is not avoiding the hard part; it is sequencing it so that the
engine rewrite lands on a vocabulary that has already been proven against 29
real languages, rather than being load-bearing for an unproven one.

Phase 4 -- the engine itself -- is where the remaining tree-sitter complaints
live, and it stays a separate decision:

- **Stable node identity across a reparse.** `Node::Id()` exists solely because
  two different nodes can share a byte range, and a `TSNode` is a transient POD
  that must not outlive its `Tree`. Meanwhile `Buffer` hand-relocates six
  independent tracked-range families across every edit. A red-green tree --
  immutable deduplicated green nodes, cheap on-demand red cursors, structurally
  shared exactly as `Text/Rope.h` already is -- makes a node handle storable and
  makes tree undo free.
- **Incremental facts.** Today `symbolKind` is O(document) per keystroke
  (~5.3ms marginal) and cannot be windowed, because `Editor/StickyScroll.h`
  needs the *enclosing* definitions -- a class opened at line 1 while the
  viewport sits at line 900. If facts are derived per subtree and reused
  whenever the subtree is reused, editing inside a function re-derives that
  function's facts, not the document's, and the enclosing-definition query
  becomes a lookup in a persistent interval tree.
- **Edit-driven API.** `IncrementalParseCache` reconstructs each edit by
  common-prefix/common-suffix diffing against its own last text, because
  `Mode`'s capability signatures cannot carry a `TSInputEdit`. So every
  keystroke walks the whole document twice before tree-sitter even starts.
  Take `(edit) -> fact delta` as the primary API and `(text) -> facts` as the
  convenience wrapper.
- **Ranged parsing as a real operation.** `Editor/HugeStructuralWindow.h`'s
  windowing carries two corrections found empirically, both symptoms of feeding
  a parser a substring and calling it a document: a fold range ending exactly at
  the window boundary is untrustworthy (error recovery still emits a
  `compound_statement` reaching the fed substring's own edge), and `windowStart`
  must be snapped to a line start or the parser desyncs badly enough to misplace
  or drop a real definition well past the cut. A parser that natively supports
  "parse `[a, b)` with this entry state, and report what you are unsure about at
  the edges" removes both.
- **Injections as a real tree.** `Editor/EmbeddedDocuments.cpp` builds a virtual
  document in which every codepoint outside the embedded language is replaced by
  a same-byte-length, same-UTF-16-width Unicode whitespace filler, purely so
  `Lsp/Position.h`'s position math works unmodified. It is a good trick and
  it is a trick. First-class injected subtrees plus a coordinate mapper replace
  it.
- **Error recovery.** The standing external criticism is that tree-sitter errs
  toward skipping tokens where it should insert *missing* ones. matklad's
  resilient-LL work is the reference, but LL means hand-written grammars, which
  forfeits `grammar.json` ingestion and the whole upstream grammar ecosystem.
  Recommendation: stay GLR, and add grammar-declared anchor sets for resync plus
  missing-token insertion scored against the parse table.

### Grammar packaging, if and when Phase 4 happens

- `grammar.json` **ships in all 24 fetched repos** (3.6 MB total) -- the grammar
  as pure data, no `node` required.
- The parse tables are already data. Only **`lex_fn`/`keyword_lex_fn`** (a
  generated goto-DFA, which is what makes `parser.c` 825k lines) and the
  external scanner are code. Emitting the lexer DFA as character-range
  transition *data* is what turns a language into one inspectable, mmap-able,
  runtime-loadable artifact -- and deletes `TreeSitter/DynamicGrammar.h`'s
  `dlopen`/`dlsym("tree_sitter_<name>")` path along with it.
- **External scanners cannot be avoided**: 19 of 24 grammars carry one, ~10,600
  LOC of hand-written C (markdown ~2,000, yaml ~1,415, bash ~1,217). Three
  tiers: a declarative DSL for the common shapes (indentation stacks, heredocs,
  delimiter-matched raw strings, ASI); **a C ABI shim for everything else** --
  `TSLexer` is 7 function pointers and the scanner vtable is 5 slots, so
  upstream scanners keep working unmodified at near-zero port cost; and
  sandboxed WASM for untrusted runtime-loaded grammars, optional and later.
- Conformance is free: upstream ships **235 corpus files, ~109,000 lines**
  across the 24 grammars.

## Steps

Deliberately sequenced so each phase is independently verifiable and each one
can be the last one.

**Phase 0 -- Oracle.** Snapshot tree-sitter's tree plus all 8 fact kinds over a
corpus of real files, as a checked-in regression fixture. Everything after is
validated against it. Cheap, and without it none of the rest is falsifiable.

**Phase 1 -- Language-definition format and compiler.** Ingest `grammar.json`,
declare traits alongside, emit one artifact. Implement Tier 0 inference and
measure it against the existing fold/indent corpus. Exit criterion: 53 of 55
fold nodes reproduced with zero hand-written rules.

**Phase 2 -- Trait-driven structural drivers.** Rewrite fold, indent, dedent,
structural selection, sticky scroll and brace match against traits. Exit
criterion is *deletable files*: roughly half the `.scm` corpus, and the Folds
and Indents columns at 29/29 without anyone authoring an adapter.

**Phase 3 -- Semantic drivers.** Scopes, bindings and references as a real
resolution layer rather than query captures. Exit criterion: the Lisp
eight-pair cliff is gone, or Tier 1 is proven insufficient and this document is
revised before any engine work starts.

**Phase 4 -- The engine.** Own table generation, data-driven lexer, external
scanner ABI shim, red-green tree with stable identities, ranged parsing,
edit-driven API, missing-token recovery. A separate decision, taken only once
Phases 1-3 have demonstrated the vocabulary against 29 real languages.

## Not doing

- **A standalone reusable library.** Considered and dropped: Phase 2 and 3 have
  deep, legitimate pull into ned specifics (Janet host callouts, `Mode`'s
  capability surface, `SyntaxClass`), and designing library-first would make it
  worse at the job it exists for. Extract later if it earns it.
- **Abandoning `grammar.json` ingestion.** It is what preserves several hundred
  upstream grammars. Dropping it would unlock resilient-LL error recovery and a
  nicer grammar DSL, at the cost of every language nobody here personally writes
  a grammar for. Recorded explicitly because it forecloses the LL path
  permanently, and that should be a conscious call rather than a default.
