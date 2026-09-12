# Parsing Engine: Unifying Grammars Behind a Trait Vocabulary

Design for replacing ned's per-language tree-sitter query corpus with a single
trait-based language-definition format, and -- later, and separably -- the
tree-sitter runtime underneath it.

Status: **design sketch, unstarted.** Nothing here is implemented. Every number
below was measured against this repo's own checkout on 2026-09-11 (24 fetched
grammar repos under `build/_deps/`, 79 `.scm` files under
`Source/Editor/TreeSitter/queries/`, 113 embedded query constants in
`Source/Editor/TreeSitter/Queries.h`) rather than estimated, so that a later
reader can re-run the same counts and see what drifted.

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

### The N x M matrix, and its 51% hole

`Queries.h` declares 113 embedded query constants across 29 languages and 8
driver kinds:

```
29 languages x 8 driver kinds = 232 possible adapter files
113 written  ->  119 GAPS (51% of the feature matrix)

Highlights  29/29      Indents  21/29      Locals  15/29
Imports     12/29      Folds    12/29      Tags    11/29
Tests       10/29      Injections 3/29
```

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

**Measured 2026-09-11: 55 of 55, with no per-language rules.**
`Tools/TraitInferenceProbe.py` is the experiment, checked in so the number can
be re-derived rather than trusted. Three refinements were needed beyond the
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
deliberately cheap to reverse, and `Tests/TraitInferenceTest.cpp` pins both
directions so neither can rot.

Two things the policy deliberately does *not* decide. Whether a body spans
more than one line -- that is a property of the text rather than the grammar,
and `Editor/CodeFold.h` enforces it. And `cpp`'s `declaration_list`, which
inference reports and `cpp-folds.scm` omits: that looks like an omission the
inference just caught rather than a considered exclusion, but it is recorded
as an open question rather than quietly decided.

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
