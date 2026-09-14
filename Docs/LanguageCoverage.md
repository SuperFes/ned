# Language Coverage: Tiers, Admission Policy, and Graveyard

What ned commits to supporting, at what depth, and how a grammar gets in.

Companion to `Docs/ParsingEngine.md`, which is the architecture this depends on.
Read that first: the tiers below are only affordable *because* Tier 0 inference
makes structural support free per language. Without it this document is a wish
list.

Status: **catalogue, not a commitment schedule.** Tier A reflects where ned
already is or clearly should be; everything below it is a ranking, not a queue
with dates.

Grammar-health data was sampled 2026-09-11 via the GitHub API. Individual
entries go stale -- the **admission policy** is the durable part, not the
snapshot.

## The one reframe this whole document depends on

**A tier is a commitment to a *depth*, not a decision about whether a language
works at all.**

Today, adding a language means authoring up to 8 hand-written `.scm` adapter
files, which is why the current matrix is 51% empty. Under the trait
architecture, structural support falls out of `grammar.json` with no rules
written, so the question stops being "do we support Nim?" and becomes "how
*deeply* do we support Nim, and what did that cost?"

That is what makes "basically every known language out of the gate" a real
target rather than a boast.

### The depth ladder

| Depth | What the user gets | What it costs us |
|---|---|---|
| **D0 — Structural** | Highlighting, folds, indent/dedent, structural selection, brace match, sticky-scroll containers | **Nothing.** Add the grammar. Tier 0 inference derives it from `grammar.json`. |
| **D1 — Navigational** | Symbol gutter and outline, go-to-file-at-point through imports, test discovery, scope-aware rename | One trait declaration file. Tier 1 vocabulary, ~30-40 composable traits. |
| **D2 — Integrated** | LSP, DAP, formatter, test-runner format, root markers | Config plus docs. **No parsing work at all** -- `Lsp/ServerConfig.h`, `Dap/Config.h`, `TestRun/Config.h` already take this as data. |
| **D3 — Bespoke** | Language-specific semantics (Org outline/agenda/clocking, Markdown tables, Janet binding completion) | Tier 2 escapes and real C++. Expensive, and rightly rare. |

D0 is free, D1 is cheap, **D2 is not a parsing problem at all**, and D3 is where
the money goes. The tiers below are drawn along those seams.

## Tier A — Flagship (D2, regression-tested)

The languages ned is *measured* on. A regression here is a release blocker.
Roughly 15, deliberately, because D2 means someone maintains LSP/DAP/formatter/
test-runner config and keeps it working.

**Already in-tree:** C, C++, Rust, Go, Python, JavaScript, TypeScript, TSX,
Java, C#, PHP, Bash, Markdown, Org, Janet, SQL.

Org and Janet are here for identity rather than popularity -- Janet is ned's
extension language and Org is a D3 feature ned already owns outright. Markdown
is here because it is what documentation is written in, including this file.

**Promote into Tier A:** **Lua** (the configuration language of half the tooling
world; already named four times in `ROADMAP.md`) and **CMake** (named six times;
it is *ned's own build system*, and not speaking it is embarrassing).
*Both admitted 2026-09-13* — lua `tree-sitter-grammars/tree-sitter-lua` v0.5.0
(ABI 15, scanner 195 LOC, 4 corpus files; upstream highlights+tags vendored,
locals declined — see the ROADMAP entry for the global-vs-local reasoning),
cmake `uyha/tree-sitter-cmake` v0.7.5 (ABI 14, scanner 194 LOC, 13 corpus
files; highlights ned-authored, upstream's uses an out-of-scope construct).

**Closed 2026-09-13** -- java/kotlin/csharp/go/rust already carried root markers
from the Janet migration; lua's `.luarc.json`/`.luarc.jsonc` were added, and
bash/cmake deliberately have none (no fixed marker convention exists for
either -- see `Docs/LanguageSetup.md`). Since `Lsp/ServerConfig.h`/`Dap/Config.h`/
`TestRun/Config.h` never bundle or auto-detect a command by design, the actual
remaining D2 gap was documentation, not code: `Docs/LanguageSetup.md` now gives
the recommended LSP/DAP/test-runner recipe per language, including the honest
gaps (Java's DAP has no standalone adapter to spawn, Maven/Gradle/dotnet's
multi-file JUnit XML doesn't fit `ned/set-test-results-file`, Bash/Lua have no
matching built-in test-output format).

## Tier B — Core (D1 traits, D2 where a server exists)

Everything mainstream enough that a user arriving with it should find ned
already competent. Trait declaration committed; integration config accepted
gladly but not owned.

**Systems / compiled:** Kotlin, Swift, Zig, Nim, Odin, V, Crystal, D, Objective-C,
Ada, Fortran, Pascal, Vala, Assembly (x86, ARM)

**JVM / .NET:** Scala, Groovy, Clojure *(in-tree)*, F#, VB.NET

**Functional:** Haskell, OCaml, Elixir, Erlang, Elm, PureScript, ReScript, Gleam,
Common Lisp, Scheme, Racket, Fennel

**Dynamic / scripting:** Ruby, Perl, R, Julia, Dart, Tcl, AWK, Zsh, Nushell,
PowerShell, Fish *(in-tree)*, Elvish

**Web / frontend:** HTML *(in-tree)*, CSS *(in-tree)*, SCSS/Less, Vue, Svelte,
Astro

**GPU / hardware:** WGSL, GLSL, HLSL, CUDA, Verilog, SystemVerilog, VHDL

**Other:** Solidity, GDScript, MATLAB, Prolog, Hack

Health verified for a sample of these on 2026-09-11: swift `2026-09-10`,
elixir `2026-07-20`, nix `2026-09-11`, ruby `2026-03-10`, scala `2026-08-25`,
r `2026-06-22`, dart `2026-07-07`, solidity `2026-02-11`, nu `2026-08-13`,
perl `2026-09-07`, powershell `2026-07-10`, ocaml `2026-08-30`. Aging but live:
haskell `2025-08-29`, julia `2025-11-08`, zig `2025-09-10`, awk `2025-09-24`.
The rest are unverified and get checked at admission, not now.

## Tier C — Formats and infrastructure (D1, cheap, disproportionate daily value)

The category most editors under-serve, and the one where D0-is-free pays
immediately: a config file is *almost entirely* structure, so Tier 0 inference
alone makes it genuinely usable.

**Config:** INI/properties, HCL/Terraform, Nix, Dhall, Jsonnet, KDL, HOCON,
JSON5, RON, Pkl, Nickel, editorconfig, `.desktop`, systemd units, `ssh_config`,
nginx, Caddy, `.env`, `requirements.txt`, Kconfig, udev, muttrc, xresources,
`.gitconfig` / `.gitignore` / `.gitattributes`

**Build systems:** CMake *(→ Tier A)*, Make, Meson, Ninja, Starlark/Bazel,
Earthfile, GN, Bitbake, just/Justfile, Dockerfile/Containerfile, Gradle *(rides
Kotlin/Groovy)*, docker-compose and CI pipelines *(ride YAML)*, Helm *(templated
YAML -- see Tier D)*

**Data and markup:** CSV/TSV, LaTeX, BibTeX, reStructuredText, AsciiDoc, Typst,
GraphQL, Protobuf, Thrift, Textproto, Mermaid, PlantUML, HTTP, Hurl, PEM, Po,
XML *(in-tree)*, YAML *(in-tree)*, TOML *(in-tree)*, JSON *(in-tree)*

**VCS-shaped, and self-serving:** **diff/unified-diff**, gitcommit, gitrebase.
ned has a VCS side panel, hunk-level staging (`Vcs/DiffPatch.h`) and a merge-
conflict resolution mode, all of which currently read diff output as plain text.
`tree-sitter-grammars/tree-sitter-diff` is live (`2026-08-14`). This is the
cheapest high-value entry in the whole document. *Admitted 2026-09-13* — v0.2.0,
ABI 15, no scanner, 2 corpus files; highlights ned-authored with three new
first-class syntax classes (DiffAdded/DiffRemoved/DiffChanged); the one grammar
the imprint measures zero delimited bodies for.

**Injected-into-comments-and-strings:** regex, JSDoc, Doxygen, Luadoc, and
tree-sitter's own query language (`.scm` -- which ned authors 79 of and
currently edits without highlighting). All of these ride the existing injection
engine (`Editor/Injection.h`) rather than needing a mode of their own.

### SQL: D0 core admitted 2026-09-13, dialect deltas deferred

The user asked specifically about SQL dialects. **D0 core is admitted** --
`DerekStride/tree-sitter-sql` v0.3.11 (245★, ABI 15, 188-LOC scanner, 412-case
corpus across 31 files, 100% conformance clean). Highlighting is upstream's
own `queries/highlights.scm`, adapted (`Source/Languages/sql/highlights.janet`,
not vendored under `upstream/` -- one clause dropped: `parameter: [(literal)]?`
used a quantifier-on-alternation, outside QueryMatcher's census-measured scope,
same kotlin/cmake/diff precedent; the file says exactly what changed and why).
Fold/indent come from the Tier 0 delimiter imprint alone (10 delimited bodies
inferred -- `subquery`, `column_definitions`, `parenthesized_expression`, CTEs'
`list`, ...), the same as json/css/toml/php: no indent query at all, since
upstream's `queries/indents.scm` uses nvim-treesitter's `@indent.begin`/
`@indent.branch`/`@indent.end` convention, not ned's own. No `tags.janet` (D1
symbol gutter) yet -- upstream ships none, and authoring one is deferred, not
in scope for this pass.

**Packaging wrinkle worth knowing before ever bumping this grammar's version:**
`DerekStride/tree-sitter-sql` doesn't commit generated `parser.c`/`grammar.json`
to `main` or its tags -- only `grammar.js`/`scanner.c`. Generated output ships
only as a GitHub Release asset or on a `gh-pages` branch. `CMakeLists.txt`
fetches the release tarball via a URL (`ned_fetch_treesitter_release`, a new
sibling of `ned_fetch_treesitter_source`) for `src/`, and separately fetches
the ordinary git tag for `test/corpus` (which the tarball omits) -- two
FetchContent declarations for one grammar, unlike every other admission here.

**Per-dialect trait deltas remain the deferred half.** Upstream is genuinely
fragmented here -- verified 2026-09-11:

| Grammar | Last push | Note |
|---|---|---|
| `DerekStride/tree-sitter-sql` | 2026-09-10, 245★ | Active. Generic/MySQL-leaning. The default choice. |
| `takegue/tree-sitter-sql-bigquery` | — | BigQuery/GoogleSQL, separate grammar |
| `gmr/tree-sitter-postgres` | — | Postgres, separate grammar |
| `m-novikov/tree-sitter-sql` | 2024-03-06 | Postgres. Stale. → Graveyard |
| `dhcmrlchtdj/tree-sitter-sqlite` | 2023-06-24 | **Archived.** → Graveyard |

Four grammars for one language family, because tree-sitter has no way to say
"T-SQL is ANSI SQL plus these deltas." So every dialect forks the whole grammar
and then drifts.

That is exactly the problem **Tier 1 rule inheritance** exists to solve -- the
same mechanism that replaces `ned_embed_treesitter_query_concat`, and the same
shape as TypeScript-being-a-delta-on-JavaScript. One SQL core plus per-dialect
trait deltas for Postgres, MySQL, SQLite, T-SQL, PL/pgSQL, BigQuery and
Snowflake is a **better answer than anything upstream currently offers**, which
makes SQL the strongest flagship demo of the architecture rather than just
another line item.

DerekStride is now admitted as that core (see above); dialects as the first
real test of rule inheritance remain a follow-up, not done here.

#### Schema-aware completion (connect-to-the-database intelligence) is a separate question from the grammar

Worth keeping apart from the above, because it sounds like a big bespoke (D3)
feature and might not be one. Real SQL language servers already exist that
connect to a live database and complete against its actual schema --
`lighttiger2505/sqls` and `joe-re/sql-language-server` both do this, config-
driven (a connection string/credentials file, not a bespoke protocol). If one
of those proves solid, wiring it up is `ned/set-lsp-command "sql" [...]` plus a
connection-config doc entry -- ordinary **D2**, the same shape as everything in
`Docs/LanguageSetup.md`, not a new subsystem. It only becomes a real feature
(D3-scale: credential storage, a connection-picker UI, per-buffer dialect-to-
connection mapping) if evaluating those servers finds the LSP-only path
insufficient. **Recorded as a maybe, unparked by**: someone actually trying one
of those two servers against a real database and reporting back what's missing
that only bespoke C++ could fix.

## Tier D — Maybe later (needs more than a grammar)

Parked for a stated reason, each with the condition that would unpark it.

- **GDScript / Godot.** Grammar is live (`PrestonKnopp/tree-sitter-gdscript`,
  `2026-07-13`, 115★) even though nvim-treesitter marks *its own queries*
  unmaintained -- worth keeping those two facts apart. The real blocker is
  recorded in `ROADMAP.md` already: Godot 4's language server reportedly speaks
  LSP over a **TCP socket to a running editor instance**, not a spawned stdio
  subprocess, which `Lsp/Transport.h` cannot express. **Unparks when** a
  raw-socket `Transport` exists.
- **Templating languages** -- Jinja2, Twig, ERB, Handlebars, Liquid, Tera,
  Django, Helm. Host language and template language interleave in one file, so
  these are the hardest possible injection case. **Unparks when**
  `Editor/EmbeddedDocuments.h`'s width-preserving-padding workaround is replaced
  by real injected subtrees (Phase 4).
- **Single-file components** -- Vue, Svelte, Astro. Same reason, milder: three
  languages per file with the boundaries declared in markup. Listed in Tier B
  because the grammars handle the split; listed *here* because D1 traits across
  a language boundary are not solved yet.
- **Jupyter notebooks.** Already has its own `ROADMAP.md` section; the format
  problem dominates the language problem.
- **Anything wanting D3.** A bespoke semantic driver is Tier 2 escapes plus real
  C++. Org earned it. The bar should stay that high.

## Ahead of the catalogue — languages that do not exist yet

Distinct from Tier D (parked, needs more than a grammar) and from the Graveyard
(evaluated, rejected). These are languages that are not *ready to be evaluated*,
where the correct action today is to watch rather than to wire.

This section is also where the architecture's central claim gets a concrete
test. "Semantic drivers should work with languages not yet invented" is easy to
assert and hard to check; naming a specific one, with a specific admission
trigger, makes it falsifiable.

### Carbon

**Status, verified 2026-09-11:** `carbon-language/carbon-lang` is very much
alive -- pushed the same day, 33,888★, weekly nightly builds -- but the language
is explicitly experimental and pre-0.1. Carbon's own roadmap states that *"the
end of 2026 is now the soonest that 0.1 could realistically be ready to ship"*,
with 0.1 being the point at which it becomes realistically evaluable as a C++
successor at all. Current focus is C++ interop and memory-safety design.

**Grammar situation: nothing admissible, and correctly so.** A search returns
six `tree-sitter-carbon` repositories. **All six have zero stars.** One is
archived (`samestep`, last push 2024-10-31), the oldest dates to 2022-07-20, and
the three recent ones (`Aaron-212` 2026-08-23, `peterpaul` 2026-07-31,
`m13silva` 2026-04-11) are independent hobby attempts with no shared lineage.
None passes the admission policy, and none *should* -- a grammar cannot
stabilise ahead of the language it describes, so any of these is tracking a
moving target by definition.

**Why it is in this document anyway.** ned is C++23 throughout. Carbon's entire
pitch is C++ interoperability as a migration path for large existing C++
codebases, which is a precise description of this one. If Carbon ships, ned is
in its target audience rather than adjacent to it, and the editor plausibly ends
up written in the language it is also editing.

**Why it is a good Tier 0 candidate specifically.** Carbon names *"Principle:
Low context-sensitivity"* as an explicit design principle -- a direct reaction
to C++'s parsing pathologies. Low context-sensitivity is precisely the property
that makes structural inference reliable: the less a construct's meaning depends
on distant context, the more trustworthy "this production is
`open . repeat(X) . close`, therefore this is a delimited body" becomes. A
language designed for tooling is the easy case for Tier 0, not the hard one.

**What admitting it will cost, whenever that day comes.** One grammar and one
trait file for D0+D1 -- and D0 the instant *any* grammar exists, before anybody
writes a rule. Under the current architecture the same language would cost up to
eight hand-authored `.scm` files, which is the difference between "wait for
Carbon to matter" and "support Carbon the week it stabilises".

**Admission trigger:** Carbon 0.1 ships with a stable surface syntax **and** a
grammar exists that tracks it -- whoever maintains it. Until both hold, watch;
do not wire. Re-check the six candidates then rather than betting on one now.

### The general rule this illustrates

For any pre-1.0 language: **the grammar cannot be more stable than the language,
so admitting one early buys a maintenance obligation rather than a feature.**
Record the watch trigger, re-check on a cadence, and let D0 do the work on the
day it lands. The architecture is what makes waiting cheap -- there is no
back-catalogue of hand-written adapters to write when the moment arrives.

## Graveyard

Evaluated and rejected, with the reason and the revisit trigger recorded, so
nobody re-litigates them from scratch.

| Entry | Reason | Revisit when |
|---|---|---|
| `dhcmrlchtdj/tree-sitter-sqlite` | **Archived upstream** (last push 2023-06-24) | A maintained fork appears |
| `m-novikov/tree-sitter-sql` | Stale since 2024-03; Postgres-only | Superseded by DerekStride + dialect deltas |
| `alemuller/tree-sitter-make` | Stale since 2024-01 | Superseded by `tree-sitter-grammars/tree-sitter-make` |
| `bkegley/tree-sitter-graphql` | Stale since 2024-06 | Check `tree-sitter-grammars` for a fork first |
| `mitchellh/tree-sitter-proto` | Stale since 2024-06 | Same |
| `MunifTanjim/tree-sitter-lua` | Personal fork, 1★, superseded | Use `tree-sitter-grammars/tree-sitter-lua` |
| nvim-treesitter's unmaintained set | caddy, djot, robot, roc, slint, vento, ziggy, ziggy_schema | Per-grammar, on demand |
| Long-tail DSLs | ABL, Magik, Hoon, Uxntal, Papyrus, Quakec, Runescript, Sflog, T32, and ~200 similar | **One credible user request.** Not rejected on quality -- parked on audience. Under D0 the cost of honouring such a request is adding one grammar, so the bar is deliberately low. |

The long-tail row is the important one. These are not bad grammars; they are
grammars with no ned audience *yet*. D0 makes "yes, next release" a reasonable
answer to a request for any of them, which is a very different posture from
every other editor's.

## Admission policy

The durable half of this document. A grammar is admitted when it passes all of
these, and the answers get recorded beside its declaration so a later reader can
see what was true at the time.

1. **Prefer `tree-sitter-grammars/*`** -- the community maintenance org -- over
   the original personal repository. Measured 2026-09-11:

   ```
   alemuller/tree-sitter-make (original)      2024-01-31   51*
   tree-sitter-grammars/tree-sitter-make      2026-02-26   17*

   MunifTanjim/tree-sitter-lua (fork)         2025-05-17    1*
   tree-sitter-grammars/tree-sitter-lua       2026-06-19  104*
   ```

2. **Star count is not a health signal.** See above: 51★ stale beats 17★ live on
   popularity and loses on every measure that matters. Use `pushed_at` and
   `archived`.
3. **Reject `archived: true`** outright.
4. **`pushed_at` older than ~18 months → Graveyard**, unless nothing else exists
   for that language, in which case admit it and record why.
5. **Pin by tag or commit SHA, never a branch.** Already the convention in
   `CMakeLists.txt`; worth restating because it is what makes the health snapshot
   meaningful.
6. **Record the ABI version.** Three are already in-tree simultaneously
   (janet-simple at 13, twelve grammars at 14, eleven at 15), and this only gets
   worse as grammars are bumped independently.
7. **Record the external-scanner LOC.** 19 of 24 current grammars carry one
   (~10,600 LOC of hand-written C). It is the port cost if Phase 4 ever happens,
   and it is free to write down now and expensive to reconstruct later.
8. **Require a `test/corpus`.** Upstream ships 235 corpus files / ~109,000 lines
   across the current 24 -- that is the conformance suite for the engine phase,
   and a grammar without one cannot be validated against a rewrite.

## What to do first

Not a schedule, an ordering, and every item is independent of the engine work:

1. **Close Tier A's D2 gap.** java/kotlin/csharp/go/rust/bash get the LSP root
   markers, formatter and test-runner config that c/cpp/python/js/ts already
   have. Pure config, no parsing, deliverable today.
2. **Add Lua and CMake.** Both are Tier A by usage, both are absent, and CMake is
   ned's own build system.
3. **Add diff/unified-diff.** Cheapest high-value entry in this document; ned
   already renders diff output as untyped text in a VCS panel it owns.
4. **Add `.scm` (tree-sitter query).** ned authors 79 of them and edits them with
   no highlighting.
5. **Adopt SQL via DerekStride**, and make the dialect deltas the acceptance test
   for Tier 1 rule inheritance in Phase 2.

Items 1-4 are worth doing whether or not the parsing engine is ever built.
Item 5 is the one that is genuinely gated on it.
