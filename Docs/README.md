# ned Developer Docs

This is the design-record archive: deep dives into *why* a subsystem works the way it
does, capability audits that hold a claim ("every bundled language highlights strings")
against the real query/grammar data, and investigations into paths that were considered
and either taken or rejected.

**Start with `CLAUDE.md`** in the repository root, not here. It's the single authoritative
description of ned's current architecture — namespaces, dependency direction, what lives
in each `Source/` subdirectory — and it's kept current on every change that touches
architecture. Nothing in this book duplicates it; a page here either goes one level
deeper than `CLAUDE.md` has room for, or records a design decision's own reasoning so it
doesn't have to be re-derived from `git log` every time it's questioned.

If you're looking for end-user documentation — installing ned, configuring `init.janet`,
what a feature does and how to turn it on — that's the [User Guide](../../user/) instead,
built from [`UserGuide/`](https://github.com/SuperFes/ned/tree/main/UserGuide) in this
same repository.

## What's here

- **Architecture deep dives** — subsystems too large or too actively-evolving for
  `CLAUDE.md`'s own per-file summaries: the parsing engine, `BufferView`'s decomposition
  into multiple translation units, and the translucency/compositing design behind
  Theme v2.
- **Capability audits** — generated-and-held-against-reality tables of what each bundled
  language actually supports, so a claim like "C++ folds argument lists" is something a
  test enforces rather than something a comment asserts.
- **Design records** — the long-form reasoning behind one subsystem's settings surface,
  where the settings reference and the record of how each rule kind got its shape are the
  same document.
- **Investigations** — design questions that got a real, measured answer (embedding a
  second language runtime, a full keybinding audit against Emacs' own conventions) even
  where the answer was "not now."

Every page here is Markdown you can also just read directly in the repository — this
book is a navigable, searchable rendering of the same files, nothing more.

Three files in this directory — `Commands.md`, `Scripting.md`, `Invocation.md` — plus
everything under `man/` are **generated, not written**. `Tests/CommandReferenceTest.cpp` and
`Tests/InvocationReferenceTest.cpp` render them from the live `CommandRegistry`, `ned/*`
binding table, and `CLI::App`, and fail the build when the committed copy disagrees — so
edit the docstring at the registration site, then regenerate:

```sh
NED_BLESS_COMMAND_DOCS=1 ./build/Tests/ned_tests "[CommandDocs]"
```
