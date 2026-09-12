# The parsing oracle

Phase 0 of `Docs/ParsingEngine.md`. A checked-in snapshot of what the current
tree-sitter-backed `Mode` capabilities produce over a small corpus, so every
later phase has something to be validated against.

```
corpus/    small real-ish source files, one or more per language
expected/  the snapshot, one <file>.oracle per corpus file
```

Driven by `Tests/OracleTest.cpp`.

## Regenerating

```sh
NED_BLESS_ORACLE=1 ./build/ned_tests "[Oracle]"
```

**Then read the diff before committing it.** Blessing without reading is the one
way this test becomes worthless -- it cannot tell an intended improvement from a
regression, and is not trying to.

## What it is and isn't

It is a *characterization* test. It does not claim the current output is
correct; `ROADMAP.md` says parts of it are not. It claims only that a change to
the output is visible in a diff and had to be looked at on purpose.

That is exactly what Phase 1-3 need. "The trait engine reproduces 53 of 55 fold
nodes with zero hand-written rules" is otherwise an assertion nobody can check.

## Two rendering rules

- **Enum names, never indices.** Reordering `SyntaxClass` must not churn every
  golden file, and a semantic change must not hide behind a stable index.
- **Byte ranges *and* captured text.** Offsets alone are unreadable in a diff;
  text alone cannot tell two same-named captures apart.

Facts are sorted before rendering -- `FoldFunction`'s own doc comment says its
output order is unspecified.

## Corpus entries that exist to record a *limit*

Most files are ordinary samples. Some are here to pin a place where the current
engine is known to fall short, so that beating it later shows up as a diff
rather than as a claim:

- **`cliff.clj`** -- a `let` with nine binding pairs. `clojure-locals.scm`
  unrolls by pair index and stops at eight, so the snapshot records exactly
  eight `Definition.var` captures and no `i`. This is the acceptance test for
  Tier 1 quantifiers: when the ninth appears, the vocabulary is real.

Add to this list rather than fixing the corpus around a limitation. A limit
nobody wrote down gets rediscovered instead of closed.

## Scope

Five languages today -- json, cpp, python, clojure, html -- chosen to cover
distinct shapes (trivial, C-family, indentation-based, Lisp, injection host)
rather than breadth. The harness is the work; widening the corpus is a loop.
`Tests/OracleTest.cpp`'s `ModeByExtension()` is the one place to extend, and a
corpus file with no mode registered there fails rather than silently skipping.
