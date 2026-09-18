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

**Systems / compiled:** Kotlin, Swift *(admitted 2026-09-18 — see below)*,
Zig *(→ Tier D, no admissible grammar yet)*, Nim *(admitted 2026-09-18)*, Odin
*(admitted 2026-09-18)*, V *(admitted 2026-09-18)*, Crystal *(admitted
2026-09-18)*, D *(admitted 2026-09-18)*, Objective-C *(admitted 2026-09-18)*,
Ada *(admitted 2026-09-18)*, Fortran *(admitted 2026-09-18)*, Pascal *(admitted
2026-09-18)*, Vala *(admitted 2026-09-18)*, Assembly (x86 *admitted
2026-09-18*; ARM: no grammar found under the obvious names)

**JVM / .NET:** Scala *(admitted 2026-09-18 — see below)*, Groovy *(admitted
2026-09-18)*, Clojure *(in-tree)*, F# *(admitted 2026-09-18)*, VB.NET *(no
grammar with a corpus -- parked)*

**Functional:** Haskell *(admitted 2026-09-18)*, OCaml *(admitted 2026-09-18,
with its interface grammar)*, Elixir *(admitted 2026-09-18)*, Erlang *(admitted
2026-09-18)*, Elm *(admitted 2026-09-18)*, PureScript *(admitted 2026-09-18)*,
ReScript *(admitted 2026-09-18)*, Gleam *(admitted 2026-09-18)*, Common Lisp
*(admitted 2026-09-18)*, Scheme *(admitted 2026-09-18)*, Racket *(admitted
2026-09-18)*, Fennel *(admitted 2026-09-18)*

**Dynamic / scripting:** Ruby *(admitted 2026-09-13 — see below)*, Perl
*(admitted 2026-09-18 — see below)*, R *(admitted 2026-09-14 — see below)*,
Julia *(admitted 2026-09-18)*, Dart *(admitted 2026-09-18)*, Tcl *(admitted
2026-09-18)*, AWK *(grammar rejected by the reference generator -- parked, see
below)*, Zsh *(rides bash)*, Nushell *(admitted 2026-09-18)*, PowerShell
*(admitted 2026-09-18)*, Fish *(in-tree)*, Elvish *(→ Graveyard)*

**Web / frontend:** HTML *(in-tree)*, CSS *(in-tree)*, SCSS *(admitted
2026-09-18)*/Less, Vue *(admitted 2026-09-18)*, Svelte *(admitted 2026-09-18)*,
Astro *(admitted 2026-09-18)*

**GPU / hardware:** WGSL *(no corpus upstream -- parked)*, GLSL *(admitted
2026-09-18)*, HLSL *(admitted 2026-09-18)*, CUDA *(admitted 2026-09-18)*,
Verilog *(admitted 2026-09-18)*, SystemVerilog *(no corpus upstream -- parked;
the Verilog grammar covers the SystemVerilog syntax its corpus exercises)*,
VHDL *(admitted 2026-09-18)*

**Other:** Solidity *(admitted 2026-09-18)*, GDScript *(admitted 2026-09-18 at
D0/D1; the Godot integration Tier D describes is untouched)*, MATLAB
*(admitted 2026-09-18)*, Prolog *(no `grammar.json` upstream -- parked)*, Hack
*(→ Graveyard, archived)*

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

**Config:** INI/properties *(both admitted 2026-09-18 — see below)*, HCL/Terraform *(admitted 2026-09-13 — see below)*,
Nix *(admitted 2026-09-13 — see below)*, Dhall, Jsonnet, KDL *(admitted
2026-09-18)*, HOCON *(→ Graveyard)*, JSON5 *(admitted 2026-09-18)*, RON
*(admitted 2026-09-18)*, Pkl, Nickel, editorconfig *(admitted 2026-09-18)*,
`.desktop`, systemd units, `ssh_config` *(admitted 2026-09-18)*, nginx, Caddy,
`.env` *(admitted 2026-09-18)*, `requirements.txt` *(admitted 2026-09-18)*, Kconfig *(no corpus
upstream -- parked)*, udev *(admitted 2026-09-18)*, muttrc *(ships no
`grammar.json` -- parked)*, xresources, `.gitconfig` *(admitted 2026-09-18)* /
`.gitignore` *(→ Graveyard)* / `.gitattributes` *(admitted 2026-09-18)*

**Build systems:** CMake *(→ Tier A)*, Make *(admitted 2026-09-13 — see
below)*, Meson *(admitted 2026-09-18)*, Ninja, Starlark/Bazel *(admitted
2026-09-18)*, Earthfile *(admitted 2026-09-18)*, GN *(no corpus upstream --
parked)*, Bitbake *(no corpus upstream -- parked)*, just/Justfile *(admitted
2026-09-18)*,
Dockerfile/Containerfile *(admitted 2026-09-13 — see below)*, Gradle *(rides
Kotlin/Groovy)*, docker-compose and CI pipelines *(ride YAML)*, Helm *(templated
YAML -- see Tier D)*

**Data and markup:** CSV/TSV/PSV *(admitted 2026-09-18 — see below)*, LaTeX
*(admitted 2026-09-18)*, BibTeX *(no corpus upstream -- parked)*, reStructuredText
*(admitted 2026-09-18)*, AsciiDoc *(admitted 2026-09-18)*, Typst *(admitted
2026-09-18)*, GraphQL *(→ Graveyard)*, Protobuf *(admitted 2026-09-18)*, Thrift
*(admitted 2026-09-18)*, Textproto *(no corpus upstream -- parked)*, Mermaid *(→
Graveyard)*, PlantUML *(→ Graveyard)*, HTTP *(admitted 2026-09-18)*, Hurl *(no
corpus upstream -- parked)*, PEM *(admitted 2026-09-18)*, Po *(no corpus
upstream -- parked)*, XML *(in-tree)*, YAML *(in-tree)*, TOML *(in-tree)*, JSON
*(in-tree)*

**VCS-shaped, and self-serving:** **diff/unified-diff**, gitcommit *(admitted
2026-09-13 — see below)*, gitrebase *(admitted 2026-09-13 — see below)*. ned
has a VCS side panel, hunk-level staging (`Vcs/DiffPatch.h`) and a merge-
conflict resolution mode, all of which currently read diff output as plain text.
`tree-sitter-grammars/tree-sitter-diff` is live (`2026-08-14`). This is the
cheapest high-value entry in the whole document. *Admitted 2026-09-13* — v0.2.0,
ABI 15, no scanner, 2 corpus files; highlights ned-authored with three new
first-class syntax classes (DiffAdded/DiffRemoved/DiffChanged); the one grammar
the imprint measures zero delimited bodies for.

### 2026-09-13 batch: Dockerfile, Make, HCL, Nix, Ruby, gitcommit, gitrebase

Seven more D0-core admissions in the same pass as SQL, all through the
post-Phase-4b pipeline; every corpus passes 100% against the production engine
(conformance, incremental-vs-scratch, MatchCache reconciliation, and the red-
layer differential against the ts runtime), and every bundled highlight query
passes the QueryMatcher construct census. Admission facts:

- **dockerfile** `camdencheek/tree-sitter-dockerfile` v0.2.0 (ABI 14, 318-LOC
  scanner, 113-case corpus; upstream highlights vendored unmodified). No
  `tree-sitter-grammars` fork exists yet -- this is the de-facto canonical
  grammar (Helix, nvim-treesitter both use it).
- **make** `tree-sitter-grammars/tree-sitter-make` v1.1.1 (ABI 14, no scanner,
  99-case corpus; upstream highlights vendored, one regex clause's
  unnecessary `\*`/`\?` escapes decoded/re-encoded away by the ordinary
  `ConvertScmToJanet` string round-trip, not hand-edited). Confirms the
  ROADMAP's own prediction: supersedes the stale `alemuller/tree-sitter-make`
  (51★, dead since 2024-01). Packaging wrinkle: its corpus files are `*.mk`,
  not the `*.txt` every other bundled grammar uses -- `ParseConformanceTest`'s
  five corpus-discovery call sites now accept both.
- **hcl** `tree-sitter-grammars/tree-sitter-hcl` v1.2.0 (ABI 15, 421-LOC
  scanner, 102-case corpus; **no `queries/` directory at all** -- highlights
  ned-authored directly from `grammar.json`/`node-types.json`, the cmake
  precedent). Core HCL only, fetched with no `grammar_subdir` -- the repo's
  separate `dialects/terraform` grammar is not fetched, matching SQL's "one
  core, dialect deltas deferred" stance. Confirmed HCL wraps nearly every
  structural token (braces, brackets, `null`, `...`) in its own named grammar
  rule rather than leaving it as a bare anonymous literal, which is why the
  highlights match named nodes (`(block_start)`, `(null_lit)`, `(ellipsis)`,
  ...) rather than literal strings almost throughout.
- **nix** `nix-community/tree-sitter-nix` v0.3.0 (ABI 13, 238-LOC scanner,
  54-case corpus; richest upstream query set of the batch -- highlights,
  injections, locals *and* tags all present). Highlights ned-adapted: one
  clause (`"?"? @punctuation.delimiter` on a formal parameter's optional
  default marker) quantifies a bare string token, which QueryMatcher's
  census-measured scope excludes on purpose (not merely unmeasured -- see the
  ROADMAP watch-list entry), so that clause alone is dropped. Packaging
  wrinkle: corpus lives at repo-root `corpus/`, not `test/corpus/` like every
  other bundled grammar.
- **ruby** `tree-sitter/tree-sitter-ruby` v0.23.1 (official tree-sitter org,
  same publisher as c/cpp/python/go already in-tree; ABI 14, largest scanner
  of the batch at 1107 LOC, 290-case corpus; upstream highlights *and* tags
  vendored unmodified -- tags gives the symbol gutter and go-to-definition
  despite the nested alternation-of-multi-field-patterns shape in its
  `(comment)* @doc . [ (method ...) (singleton_method ...) ]`-style captures,
  which compiled clean).
- **gitcommit** `gbprod/tree-sitter-gitcommit` v0.5.0 (ABI 15, 65-LOC scanner,
  64-case corpus; upstream highlights vendored unmodified). Claims
  `COMMIT_EDITMSG`/`MERGE_MSG`/`TAG_EDITMSG` by filename.
  `the-mikedavis/tree-sitter-git-commit` (13★, archived) was graveyarded in
  favor of this community grammar.
- **gitrebase** `the-mikedavis/tree-sitter-git-rebase` v1.0.0 (ABI 15, no
  scanner, 15-case corpus; highlights ned-adapted -- upstream wraps each
  multi-sibling command/label/message group in an extra layer of parens
  before attaching its `:match?` predicate, one level deeper than
  QueryMatcher's "a multi-pattern group is only supported at the top level"
  scope allows; flattened so the predicate sits inside the same top-level
  group it constrains, same resolution shape as cmake's own adaptation).
  Separate maintainer from gitcommit above, despite both being "the git
  ecosystem" -- they share no source.

**Not admitted: Zig.** Researched alongside the batch above and deliberately
declined. `tree-sitter-grammars/tree-sitter-zig` (the only actively
maintained candidate, pushed 2026-09-13) ships **no `test/` directory at
all**, failing admission policy item 8 outright; the one alternative with
real history, `maxxnino/tree-sitter-zig` (124★), is **archived**. Revisit
when either a corpus lands upstream or a maintained fork adds one.

### 2026-09-18 batch: ini, json5, meson, gitattributes, ssh_config, requirements, udev, gitconfig

The first batch through `ned --import-language` (Docs/LanguageAuthoring.md):
no tree-sitter tooling anywhere in the loop, every package compiled from its
`grammar.janet` by ned's own generator at build time, every corpus at 100%
against the production engine. Chosen as the scanner-free half of Tier C's
config/build column, so the batch measured the import path itself rather than
the port work. Admission facts:

- **ini** `justinmk/tree-sitter-ini` v1.4.0 (ABI 15, no scanner, 11-case
  corpus; upstream highlights vendored unmodified, tags ned-authored: a
  section is a namespace over its settings). The de-facto grammar (Helix,
  nvim-treesitter); no `tree-sitter-grammars` fork exists.
- **json5** `Joakker/tree-sitter-json5` v0.1.0 (ABI 15, no scanner, 39-case
  corpus; upstream highlights vendored unmodified). One upstream corpus case
  (`Array with only trailing comma`) carried its `:error` marker *below* the
  header fence, where tree-sitter's own reader treats it as input; moved into
  the header, which is what the case meant.
- **meson** `tree-sitter-grammars/tree-sitter-meson` v1.3.0 (ABI 14, no
  scanner, 22-case corpus; upstream highlights vendored unmodified, the
  `folds.scm` it also ships is not a kind ned reads -- folds come from the
  imprint). Claimed by filename: `meson.build`, `meson.options`,
  `meson_options.txt`.
- **gitattributes** `tree-sitter-grammars/tree-sitter-gitattributes` v0.1.6
  (ABI 14, no scanner, 10-case corpus; upstream highlights vendored
  unmodified). Claimed by filename.
- **ssh_config** `tree-sitter-grammars/tree-sitter-ssh-config` v0.5.0 (ABI 15,
  no scanner, 9-case corpus; upstream highlights and injections vendored
  unmodified -- `ProxyCommand`/`LocalCommand`/`Match exec` arguments inject
  bash; tags ned-authored over `Host`/`Match` blocks). `~/.ssh/config` has
  the basename `config`, too generic to claim; `ssh_config` and
  `*.ssh_config` are claimed, the per-user file waits on path-pattern claims.
- **requirements** `tree-sitter-grammars/tree-sitter-requirements` v0.6.1
  (ABI 15, no scanner, 9-case corpus; upstream highlights vendored
  unmodified). Claimed by the conventional basenames (`requirements.txt`,
  `requirements-dev.txt`, `constraints.txt`, ...) and `*.pip`.
- **udev** `tree-sitter-grammars/tree-sitter-udev` v0.2.1 (ABI 14, no
  scanner, 5-case corpus; upstream highlights, injections *and* tags vendored
  unmodified -- `LABEL`/`GOTO` and `ENV` definitions and references).
- **gitconfig** `the-mikedavis/tree-sitter-git-config` pinned at commit
  `3a61756a81a86291a0f48e3eeeaa0692b9981aa9` (2026-07-20; the repository
  has no tags, so this is the batch's one SHA pin -- `--ref` takes a full
  hash) (ABI 14, no scanner, 14-case corpus; upstream highlights vendored
  unmodified, tags ned-authored over section headers). Claims `.gitconfig`,
  `.gitmodules`; `.git/config` has the generic basename `config`, same
  caveat as ssh_config.

Screened and not admitted in the same pass: `tree-sitter-grammars/tree-sitter-kconfig`,
`-gn` and `-bitbake` are live but ship **no corpus** (policy item 8);
`neomutt/tree-sitter-muttrc` ships no `src/grammar.json` (the import reads
the generated grammar, not `grammar.js`); `shunsambongi/tree-sitter-gitignore`
and `antosha417/tree-sitter-hocon` are stale since 2022 (→ Graveyard). Ninja,
xresources and Caddy have no grammar under the names tried; revisit with a
specific repository.

### 2026-09-18 batch 2: properties, kdl, starlark, just, editorconfig, ron, earthfile, dotenv

The scanner-bearing half of the same column, and the first eight scanners
to arrive through `ned --import-language`'s port helper rather than a hand
port: every one compiled and passed its corpus with no edit beyond the ones
the header explains (just's, below). Admission facts:

- **properties** `tree-sitter-grammars/tree-sitter-properties` v0.3.0 (ABI 15,
  28-LOC scanner, 3-case corpus; upstream highlights and tags vendored
  unmodified). Java `.properties`.
- **kdl** `tree-sitter-grammars/tree-sitter-kdl` v2.0.0 (ABI 15, 184-LOC
  scanner, 211-case corpus in `v1/` and `v2/` subdirectories -- the corpus
  reader recurses, so both dialect trees run; upstream highlights,
  injections and locals vendored unmodified).
- **starlark** `tree-sitter-grammars/tree-sitter-starlark` v1.3.0 (ABI 15,
  433-LOC scanner -- python's indent scanner, 90-case corpus; upstream
  highlights, injections and locals vendored unmodified, tags ned-authored
  as python's upstream tags minus the class rule). Claims `.bzl`, `.bazel`,
  `.star`, `.sky` and the `BUILD`/`WORKSPACE`/`MODULE.bazel` basenames,
  plus `Tiltfile` and `Snakefile`, which are Starlark dialects. Root
  markers `MODULE.bazel`/`WORKSPACE` for a future Bazel LSP entry.
- **just** `IndianBoy42/tree-sitter-just` v0.2.0 (ABI 15, 317-LOC scanner,
  64-case corpus; upstream highlights, injections and locals vendored
  unmodified from the repository's `queries/just/` layout -- the import
  reads `queries/<name>/` as well as `queries/`; tags ned-authored over
  recipes, aliases, assignments and modules). **One scanner edit:**
  upstream's `assertf` calls `exit(1)` on an invariant failure and refuses
  to compile under `NDEBUG`; ned keeps the no-op the scanner already
  defines for wasm builds -- an editor never exits on a scanner invariant.
- **editorconfig** `ValdezFOmar/tree-sitter-editorconfig` v2.0.0 (ABI 15,
  73-LOC scanner, 29-case corpus; upstream highlights vendored from
  `queries/editorconfig/`; tags ned-authored over section globs).
- **ron** `tree-sitter-grammars/tree-sitter-ron` v0.2.0 (ABI 15, 191-LOC
  scanner, 13-case corpus; upstream highlights, injections and locals
  vendored unmodified).
- **earthfile** `glehmann/tree-sitter-earthfile` 0.6.0 (ABI 15, 155-LOC
  scanner, 96-case corpus across 31 files -- the largest of the batch;
  upstream highlights and injections vendored unmodified, tags ned-authored
  over targets). Claims `Earthfile` by name and `.earth`.
- **dotenv** `pnx/tree-sitter-dotenv` v1.1.2 (ABI 15, 44-LOC scanner,
  11-case corpus; upstream highlights vendored unmodified). Two corpus
  cases are written in tree-sitter's newer `:cst` form (a positions-and-text
  listing rather than an S-expression); the corpus reader now renders that
  form byte for byte (`Grammar/Corpus.h::RenderCst`), so they run rather
  than skip -- and pin node positions, which the S-expression never did.
  Claims `.env` and its `.env.local`/`.env.production`-style variants by
  name.

Two query constructs joined the QueryMatcher census with this batch, both
inert: `(:set! "priority" N)` with the key quoted (starlark) and the
one-operand `(:set! injection.include-children)` (earthfile, just).

### 2026-09-18 batch 3: latex, rst, typst, asciidoc (+inline), proto, thrift, http, pem, csv/tsv/psv

Tier C's data-and-markup column. Three of these grammars ship no queries at
all, so their highlights and tags are ned-authored (the hcl precedent); the
rest vendor upstream's. Admission facts:

- **latex** `latex-lsp/tree-sitter-latex` v0.6.0 (ABI 15, 141-LOC scanner,
  104-case corpus across 10 files; **no queries upstream** -- texlab is its
  consumer -- highlights and tags ned-authored from the grammar: sectioning
  commands, environments, includes, labels, citations, math). The largest
  table in the tree (3.4MB, 4,531 parse states, ~11s to generate); the
  build-time compile absorbs it.
- **rst** `stsewd/tree-sitter-rst` v0.2.0 (ABI 15, scanner split across
  `src/tree_sitter_rst/` -- 44-line entry file, ~3,300 lines inlined by the
  port helper, which now stages `src/` subdirectories and inlines each header
  once; 40-case corpus; **no queries upstream**, highlights and tags
  ned-authored). `:imprint false`: section adornments are lines, not
  brackets.
- **typst** `uben0/tree-sitter-typst` v0.11.0 (ABI 14, 1,077-LOC scanner --
  one `int → enum` cast at two sites was the whole C++ friction; 430-case
  corpus in `.scm`-named files, so the corpus reader now takes any regular
  file; upstream highlights and injections vendored from `queries/typst/`,
  tags ned-authored over headings and `let` bindings). **Two cases fail on
  purpose:** `negative/000` and `negative/001` expect a `(MISSING _)`
  recovery where ned (and current tree-sitter) produce an `ERROR` -- the
  expectations were generated by an older CLI whose error-cost heuristics
  differed; the file is upstream's own "negative" bucket. Pinned in the
  conformance baseline, not blessed away.
- **asciidoc** + **asciidoc-inline** `cathaysia/tree-sitter-asciidoc` v0.9.0,
  subdirectories `tree-sitter-asciidoc` and `tree-sitter-asciidoc_inline`
  (ABI 15; 935-LOC and 145-LOC scanners sharing `include/` headers -- the
  `Result &=` enum-narrowing at four sites was the C++ friction; 40- and
  49-case corpora; upstream highlights and injections vendored unmodified,
  tags ned-authored over the document title and the five section-title node
  types). Wired the markdown/markdown-inline way: `:injection-aliases
  ["asciidoc_inline"]` resolves upstream's underscore spelling.
- **proto** `coder3101/tree-sitter-proto` 0.6.0 (ABI 14, no scanner,
  40-case corpus; upstream highlights and injections vendored unmodified;
  tags ned-authored: messages, enums, services, rpcs, fields, package).
  `mitchellh/tree-sitter-proto` was already graveyarded.
- **thrift** `tree-sitter-grammars/tree-sitter-thrift` v0.5.0 (ABI 14, no
  scanner, 29-case corpus; upstream highlights, injections and locals
  vendored unmodified; tags ned-authored).
- **http** `rest-nvim/tree-sitter-http` v3.0 (ABI 14, no scanner, 26-case
  corpus; upstream highlights and injections vendored unmodified -- the
  `:offset!` directive on script bodies is inert in ned, so a `{% %}` script
  injection spans its delimiters). Claims `.http` and `.rest`.
- **pem** `tree-sitter-grammars/tree-sitter-pem` v0.1.1 (ABI 14, no
  scanner, 3-case corpus; **no queries at the pinned tag** -- the
  `queries/` directory arrived later on the default branch -- highlights
  ned-authored, six lines). Claims `.pem`, `.crt`, `.cer`, `.csr`; `.key` is
  left unclaimed (too many other file kinds use it).
- **csv**, **tsv**, **psv** `tree-sitter-grammars/tree-sitter-csv` pinned at
  commit `f6bf6e35eb0b95fbadea4bb39cb9709507fcb181` (2025-11-13; the v1.2.0
  tag predates the corpus, which lives at the repository root and routes
  the `tsv`/`psv` cases with `:language(...)` -- the conformance suite
  routes them through the dialect map the way typescript/tsx is handled,
  and `ned --test-language` now skips a case whose marker names another
  grammar). Three packages from one repository via `--subdir`; ABI 14, no
  scanners, 8 shared cases; upstream highlights vendored unmodified.

Three more query constructs joined the census with this batch:
`:not-lua-match?` (the matcher's generic `not-` negation already covers it),
and the inert nvim directives `:gsub!` (asciidoc) and `:offset!` (http).

Screened and not admitted: `latex-lsp/tree-sitter-bibtex`,
`pfeiferj/tree-sitter-hurl`, `PorterAtGoogle/tree-sitter-textproto` and
`tree-sitter-grammars/tree-sitter-po` ship no corpus (policy item 8);
`monaqa/tree-sitter-mermaid` (2024-04) and `lyndsysimon/tree-sitter-plantuml`
(2021-12) are stale (→ Graveyard); `bkegley/tree-sitter-graphql` was already
graveyarded and no fork has appeared.

### 2026-09-18 batch 4: swift, nim, odin, v, crystal, d, objc, ada, fortran, pascal, vala, asm

Tier B's systems column, twelve grammars, six with scanners (7,144 lines
ported in all; every C++ friction point was an enum conversion C tolerates).
Two generator gaps surfaced and were closed for good: swift's identifier
regex names `\p{Emoji}` and `\p{EMod}`, and swift's and nim's use the
reference regex crate's class set operations (`[a-z&&[^aeiou]]`, `--`,
`~~`) -- `Compile/Regex.cpp` now parses all three at the reference's
precedence, and `Tools/gen-unicode-tables.py` reads the six emoji
properties from a vendored `Tools/unicode/emoji-data.txt`. Admission facts:

- **swift** `alex-pinkus/tree-sitter-swift` 0.7.3 (the de-facto grammar --
  Helix, nvim-treesitter; no `tree-sitter-grammars` fork). Upstream commits
  `grammar.json` and `scanner.c` but **no `parser.c`**, so there is no
  upstream ABI to record: ned generates the tables itself, which is the
  whole point. 949-LOC scanner, 238-case corpus across 10 files; upstream
  highlights, tags, injections and locals vendored unmodified.
- **nim** `alaviss/tree-sitter-nim` 0.6.2 (ABI 14, 1,156-LOC scanner --
  serialize/deserialize take byte buffers, wrapped; 72-case corpus; upstream
  highlights adapted at one clause: a nested field alternation `[ type: [...]
  return_type: [...] ]` split into the two patterns it means; tags
  ned-authored over the routine kinds, types and constants). The largest
  table set in the tree: 20,305 parse states, 9.1MB, ~70s to generate.
- **odin** `tree-sitter-grammars/tree-sitter-odin` v1.3.0 (ABI 14, 305-LOC
  scanner, 2-case corpus -- thin, but present; upstream highlights,
  injections and locals adapted: four `"="?`/`":"?` quantified-token
  clauses spelled out or dropped, the nix precedent; tags ned-authored over
  the `name :: ...` declaration forms).
- **v** `vlang/v-analyzer` pinned at commit
  `925d4570d1668746762a2cdf0ecb9a25be704a67` (2026-06-20), subdirectory
  `tree_sitter_v` -- the language server's own grammar, which superseded
  `nedpals/tree-sitter-v` (stale since 2023-07, → Graveyard). ABI 15, no
  scanner, 336-case corpus across 48 files; upstream highlights vendored
  unmodified, tags ned-authored (Go's layout).
- **crystal** `crystal-lang-tools/tree-sitter-crystal` pinned at commit
  `50ca9e6fcfb16a2cbcad59203cfd8ad650e25c49` (2025-10-12; no tags upstream).
  ABI 15, 3,383-LOC scanner (Ruby-class, the largest of the batch), 245-case
  corpus, 132 of them marked `:language(crystal)` -- routed through the
  dialect map like typescript; upstream highlights (from `queries/nvim/`,
  which the import now reads) and injections vendored unmodified; tags
  ned-authored, Ruby's shape.
- **d** `gdamore/tree-sitter-d` v0.9.1 (ABI 14, 587-LOC scanner, 218-case
  corpus across 36 files; upstream highlights, tags and injections vendored
  unmodified -- the Helix/Nova variants it also ships are not read).
- **objc** `tree-sitter-grammars/tree-sitter-objc` v3.0.2 (ABI 14, no
  scanner; its 75-case corpus is tree-sitter-c's, inherited -- there is no
  Objective-C-specific case upstream, so the ObjC constructs are covered by
  the generator's C conformance only; upstream highlights adapted at one
  `"="?` clause, injections and locals unmodified; tags ned-authored). Claims
  `.m` only: `.h` stays with C. `jiyee/tree-sitter-objc` (2022) is
  superseded.
- **ada** `briot/tree-sitter-ada` pinned at commit
  `dd5fa4cdb3aba91abc687aa68fb1431396fce6a6` (2026-07-31; the repository's
  only tag is named `master`). ABI 14, no scanner, 129-case corpus across 21
  files; upstream highlights and locals vendored unmodified, tags
  ned-authored. **Generation outlier:** 149s CPU for 2,207 parse states --
  the case-insensitive keyword tokens (`[pP][aA][cC][kK][aA][gG][eE]` and
  friends) multiply the lex-state work; noted in ROADMAP's generator
  follow-ups, absorbed by the parallel build for now.
- **fortran** `stadelmanma/tree-sitter-fortran` v0.6.0 (ABI 15, 764-LOC
  scanner, 137-case corpus; upstream highlights, tags and locals vendored
  unmodified). Free-form extensions plus `.f`/`.for`.
- **pascal** `Isopod/tree-sitter-pascal` v0.10.2 (ABI 14, no scanner,
  88-case corpus across 13 files; upstream highlights and locals vendored
  unmodified, tags ned-authored over units, routines, types, fields,
  properties).
- **vala** `vala-lang/tree-sitter-vala` pinned at commit
  `97e6db3c8c73b15a9541a458d8e797a07f588ef4` (2024-10-29 -- **23 months
  stale, past policy item 4's line; admitted under its own exception:
  nothing else exists**, and the grammar is the language project's own).
  ABI 13, no scanner, 6-case corpus; upstream highlights and locals vendored
  unmodified, tags ned-authored.
- **asm** `RubixDev/tree-sitter-asm` v0.24.0 (ABI 14, no scanner, 32-case
  corpus; upstream highlights and injections vendored from `queries/asm/`;
  tags ned-authored: labels and constants). GNU as / NASM-style x86;
  claims `.s`, `.S`, `.asm`, `.nasm`.

Screened and not admitted: `tree-sitter-grammars/tree-sitter-zig` still
ships no corpus (unchanged since the 2026-09-13 note); no ARM-specific
grammar under `tree-sitter-grammars/tree-sitter-arm` or the other obvious
names.

### 2026-09-18 batch 5: scala, groovy, fsharp, haskell, ocaml (+interface), elixir, erlang, elm, purescript, rescript, gleam, commonlisp, scheme, racket, fennel

Tier B's JVM and functional columns, sixteen packages from fifteen
repositories, twelve with scanners (19,600 ported lines, the two Haskell-family
scanners alone 13,400). What this batch taught the tooling: a multi-grammar
repository's shared `common/` scanner code is now staged and its includes
retargeted (fsharp, ocaml); C99 `restrict` and allocator `void*` locals are
handled by the port helper; a `queries/` tree at the repository root serves a
`--subdir` grammar; and `ned --test-language` treats `:language(ocaml_interface)`
as naming the `ocaml-interface` package. One matcher feature landed:
**supertype-scoped node names** (`(expression/variable)`), which haskell's
upstream highlights use 42 times -- the subtype must be in the supertype's
declared subtype set (checked at compile time), and matching is by the subtype's
symbol alone; the node's position under the hidden supertype is not consulted.
Admission facts:

- **scala** `tree-sitter/tree-sitter-scala` v0.26.2 (official org; ABI 15,
  1,927-LOC scanner, 273-case corpus, one `:skip`; upstream highlights, tags
  and locals vendored unmodified). 18,261 parse states, 15s to generate.
- **groovy** `murtaza64/tree-sitter-groovy` pinned at commit
  `deb0dcf8c4544f07564060f6e9b9f6e4b0bfc27d` (2026-04-11; its only tag is
  named `initial`). ABI 15, no scanner, 87-case corpus; upstream highlights,
  injections and locals vendored unmodified, tags ned-authored. Claims
  `Jenkinsfile` and `.gradle` by name.
- **fsharp** `ionide/tree-sitter-fsharp` v0.2.0, subdirectory `fsharp` (the
  separate `fsharp_signature` grammar is not bundled; `.fsi` files parse with
  this one). ABI 15, 811-LOC scanner through the repository's `common/`,
  340-case corpus (the `fsharp_signature/` cases pruned); upstream highlights,
  injections and locals vendored, adapted at three clauses -- two quantified
  bare tokens (`"*"*`, `"?"?`) dropped, one `(identifier)+` run respelled;
  upstream's `tags.scm` is empty and is not vendored.
- **haskell** `tree-sitter/tree-sitter-haskell` v0.23.1 (official org; ABI
  14, 5,996-LOC scanner after inlining -- five of its enums get an integer
  carrier type because the scanner increments and combines them the C way;
  725-case corpus, **one pinned failure**: `varsym.txt: varsym: error: carrow`
  expects an older CLI's recovery shape). Upstream highlights, injections and
  locals vendored; highlights adapted at one `(_)+` run, and otherwise run
  unmodified on the new supertype-scoped names. Tags ned-authored.
- **ocaml** + **ocaml-interface** `tree-sitter/tree-sitter-ocaml` v0.26.0,
  subdirectories `grammars/ocaml` and `grammars/interface` (the `type`
  grammar is not bundled). ABI 15, 634- and 633-LOC scanners sharing
  `common/`; 94- and 2-case corpora (the repository's one corpus tree, split
  by its `:language(...)` markers and its `ocaml_interface/` subdirectory);
  upstream highlights, tags and locals vendored unmodified.
- **elixir** `elixir-lang/tree-sitter-elixir` v0.3.5 (the language's own; ABI
  14, 660-LOC scanner, 292-case corpus; upstream highlights, tags and
  injections vendored unmodified).
- **erlang** `WhatsApp/tree-sitter-erlang` 0.20 (ABI 14, 215-LOC scanner,
  228-case corpus; upstream highlights vendored unmodified, tags
  ned-authored: functions, the module attribute, records, types).
- **elm** `elm-tooling/tree-sitter-elm` v5.9.4 (ABI 15, 682-LOC scanner --
  its `VEC_RESIZE` macro needed the allocator `void*` wrapped; 119-case
  corpus; upstream highlights, tags, injections and locals vendored
  unmodified).
- **purescript** `postsolar/tree-sitter-purescript` v0.3.0 (ABI 15, 7,393-LOC
  scanner after inlining, Haskell-derived; 147-case corpus; upstream
  highlights, injections and locals vendored unmodified; tags ned-authored
  over the aliased declaration nodes `data`/`newtype`/`type_alias`).
- **rescript** `rescript-lang/tree-sitter-rescript` v6.0.0 (the language's
  own; ABI 15, 419-LOC scanner, 142-case corpus; upstream highlights,
  injections and locals vendored unmodified, tags ned-authored).
- **gleam** `gleam-lang/tree-sitter-gleam` v1.1.0 (the language's own; ABI
  15, 69-LOC scanner, 70-case corpus; upstream highlights, tags, injections
  and locals vendored unmodified).
- **commonlisp** `theHamsta/tree-sitter-commonlisp` v0.4.1 (ABI 14, no
  scanner, 42-case corpus; upstream tags vendored unmodified, **no upstream
  highlights** (they live in nvim-treesitter) -- highlights ned-authored).
- **scheme** `6cdh/tree-sitter-scheme` v0.24.7 (ABI 14, no scanner, 31-case
  corpus in `.scm`-named files; upstream highlights adapted -- five
  quantified bare wildcards (`_*`) and one `(symbol)+` run respelled; tags
  ned-authored over the define forms).
- **racket** `6cdh/tree-sitter-racket` v0.25.0 (ABI 15, 176-LOC scanner,
  24-case corpus; upstream highlights, tags and locals vendored, adapted at
  the same `_*` and `(symbol)+` shapes as scheme's).
- **fennel** `TravonteD/tree-sitter-fennel` 1.1.0 (ABI 14, no scanner,
  35-case corpus; **no queries upstream** -- highlights and tags
  ned-authored).

Screened and not admitted: VB.NET has no grammar with a corpus
(`CodeAnt-AI/tree-sitter-vb-dotnet` ships example files only).

### 2026-09-18 batch 6: perl, julia, dart, tcl, nu, powershell, scss, vue, svelte, astro, glsl, hlsl, cuda, verilog, vhdl, solidity, gdscript, matlab

The rest of Tier B, eighteen packages, fifteen with scanners (38,000 ported
lines; VHDL's alone is 27,000 after its per-keyword tables inline). Two
generator findings: `[--]` at the head of a character class is a literal dash
(powershell spells its operators `[--][gG][tT]`; ned's set-operation parser
had read it as an empty difference), and `\p{White_Space}` joined the
property table (perl). Admission facts:

- **perl** `tree-sitter-perl/tree-sitter-perl` v2.0.0 (the organisation's own
  rewrite; upstream commits `grammar.json` and `scanner.c` but no `parser.c`,
  so no upstream ABI to record). 4,545-LOC scanner after inlining its keyword
  tables -- three C++ fixes: two `goto`s that jumped an initialised local,
  `register`, a `void*` state cast; 283-case corpus; upstream highlights and
  injections vendored unmodified, tags ned-authored.
- **julia** `tree-sitter/tree-sitter-julia` v0.25.0 (official org; ABI 15,
  206-LOC scanner, 63-case corpus; upstream highlights, injections and locals
  vendored unmodified -- one `:has-ancestor?` clause joins the
  ancestor-crossing census; tags ned-authored).
- **dart** `UserNobody14/tree-sitter-dart` pinned at commit
  `be07cf7118d3dba06236a3f19541685a68209934` (2026-07-07; no tags upstream).
  ABI 15, 149-LOC scanner, 187-case corpus; upstream highlights vendored
  unmodified, tags vendored minus two `@reference.*` clauses that quantify
  bare tokens.
- **tcl** `tree-sitter-grammars/tree-sitter-tcl` pinned at commit
  `850a72ab6436e06645b33b11cfa60cbdb04b1f01` (2026-08-05; no tags upstream).
  ABI 15, 63-LOC scanner, 19-case corpus; upstream highlights vendored from
  `queries/tcl/`, tags ned-authored.
- **nu** `nushell/tree-sitter-nu` pinned at commit
  `4f577aaa735154f934594b598a69ed7b1b707cf6` (2026-09-14; the language's
  own, untagged). ABI 15, 155-LOC scanner, 309-case corpus in `.nu`-named
  files; upstream highlights and injections vendored from `queries/nu/`,
  adapted at three clauses (a quantified delimiter alternation, two optional
  sigil tokens, one `(comment)+` run); tags ned-authored.
- **powershell** `airbus-cert/tree-sitter-powershell` v0.26.5 (ABI 15,
  93-LOC scanner, 139-case corpus -- the batch's generator find; upstream
  highlights vendored unmodified, tags ned-authored).
- **scss** `tree-sitter-grammars/tree-sitter-scss` v1.0.0 (ABI 14, 118-LOC
  scanner, 56-case corpus; upstream highlights vendored unmodified).
  `serenadeai/tree-sitter-scss` (2022) is superseded.
- **vue** `tree-sitter-grammars/tree-sitter-vue` pinned at commit
  `ce8011a414fdf8091f4e4071752efc376f4afb08` (2026-01-24). ABI 15, 865-LOC
  scanner, 10-case corpus; upstream highlights and injections vendored from
  `queries/vue/` unmodified (its nvim `bo.commentstring` directive is inert).
  `ikatyang/tree-sitter-vue` (2024-02) is superseded.
- **svelte** `tree-sitter-grammars/tree-sitter-svelte` v1.0.2 (ABI 14,
  1,105-LOC scanner, 45-case corpus; upstream highlights, injections and
  locals vendored unmodified).
- **astro** `virchau13/tree-sitter-astro` pinned at commit
  `213f6e6973d9b456c6e50e86f19f66877e7ef0ee` (2025-04-23, 17 months -- inside
  the line). ABI 14, 1,124-LOC scanner, 22-case corpus at the repository root
  (`corpus/`, the nix layout, which the import now finds); upstream highlights
  and injections vendored unmodified.
- **glsl** `tree-sitter-grammars/tree-sitter-glsl` v0.2.0 (ABI 14, no
  scanner, 13-case corpus; upstream highlights vendored unmodified; tags
  ned-authored, C's declarator chain). Claims the shader-stage extensions;
  `.vs`/`.fs`/`.vsh` stay with F# and V.
- **hlsl** `tree-sitter-grammars/tree-sitter-hlsl` v0.2.0 (ABI 14, 167-LOC
  scanner, 14-case corpus; **no queries upstream** -- the grammar is
  tree-sitter-cpp plus HLSL, so `:queries-from "cpp"` reads cpp's whole set;
  tags ned-authored).
- **cuda** `tree-sitter-grammars/tree-sitter-cuda` v0.21.2 (ABI 15, 167-LOC
  scanner, 184-case corpus; upstream highlights vendored unmodified, tags
  ned-authored). 13,077 parse states, 7MB, 35s to generate.
- **verilog** `tree-sitter/tree-sitter-verilog` v1.0.3 (official org; ABI 14,
  no scanner, 70-case corpus; **no queries upstream** -- highlights and tags
  ned-authored; its comments are node-less extras and cannot be coloured).
  The largest table set in the tree: 20,534 parse states, 18MB. **Takes
  `.v`** from the V language (far more files in the wild); V keeps `.vsh`,
  `.vv` and `v.mod`.
- **vhdl** `jpt13653903/tree-sitter-vhdl` v2.0.3 (ABI 15, the 27,000-line
  scanner -- 841 `{ KEYWORD, 0 }` enum arrays needed a cast, and its typed
  entry points a `void*` wrapper; 34-case corpus; upstream highlights and
  injections vendored from `queries/Neovim/`, tags ned-authored).
  `alemuller/tree-sitter-vhdl` (2023-12) is superseded.
- **solidity** `JoranHonig/tree-sitter-solidity` v1.2.13 (ABI 15, no scanner,
  125-case corpus; upstream highlights, tags and locals vendored, one
  grouped-anchor clause respelled).
- **gdscript** `PrestonKnopp/tree-sitter-gdscript` v6.1.0 (ABI 14, 646-LOC
  scanner, 167-case corpus; **no queries upstream** -- highlights and tags
  ned-authored).
- **matlab** `acristoffers/tree-sitter-matlab` v1.3.1 (ABI 15, 1,191-LOC
  scanner, 157-case corpus; upstream highlights, injections, locals and tags
  vendored from `queries/neovim/`, locals adapted at one quantified group).
  Claims `.mlx` only: `.m` is Objective-C's.

**Parked: AWK.** `Beaglefoot/tree-sitter-awk` v0.7.2 declares
`binary_relation > piped_io_exp` in one precedence list and the reverse in
another; the reference generator's own validation ("Conflicting orderings for
precedences") rejects it since 0.22, and the shipped `parser.c` (ABI 14)
predates that check. Revisit when upstream regenerates. Screened and not
admitted: `elves/tree-sitter-elvish` (2023-07, → Graveyard),
`szebniok/tree-sitter-wgsl` (no corpus, stale), `gmlarumbe/tree-sitter-systemverilog`
(no corpus), `foxyseta/tree-sitter-prolog` (no `grammar.json`),
`slackhq/tree-sitter-hack` (archived, → Graveyard).

**Injected-into-comments-and-strings:** regex, JSDoc, Doxygen, Luadoc, and
tree-sitter's own query language (`.scm` -- which ned authors 79 of and
currently edits without highlighting). All of these ride the existing injection
engine (`Editor/Injection.h`) rather than needing a mode of their own.

### R: D0+D1(tags) admitted 2026-09-14

`r-lib/tree-sitter-r` v1.3.0 (the posit/RStudio-maintained official grammar,
155★, pushed 2026-06-22), ABI 14, 31KB scanner, 4-file corpus (89 cases),
100% conformance clean against all four corpus gates (conformance, ned parse
engine, incremental-vs-scratch, MatchCache reconciliation, red-layer
differential). Unlike SQL/perl this repo commits generated
`parser.c`/`node-types.json` directly on its tag, so admission took the
ordinary `ned_add_treesitter_grammar` path with no release-tarball fetch.
Highlights and tags are upstream's own, vendored unmodified (the mechanical
`ConvertScmToJanet` round-trip needed no adaptation -- every construct R's
queries use sits inside QueryMatcher's census-measured scope). Tags gives the
symbol gutter/go-to-definition for `<-`/`=`-assigned function definitions
(both identifier- and string-named) and call references.

**Folding: measured zero, not a bug.** The Tier 0 imprint infers delimited
bodies from `grammar.json` alone, and for R it measures none at all -- the
same honest-zero outcome as the `diff` grammar. R's `{`/`}`/`(`/`)` pairs are
real named productions (`braced_expression`, `parenthesized_expression`,
`call_arguments`, ...), but their opener/closer tokens are `ALIAS`es of an
*external scanner* symbol (`_external_open_brace` etc.), not plain string
literals -- R's context-sensitive brace/newline handling needs a scanner to
disambiguate them at all, and the imprint's inference only recognizes a
literal-token opener/closer. No folding or bracket-imprint support follows
from this, and none is faked; highlighting and the D1 symbol gutter are
unaffected, since neither depends on the imprint.

Upstream also ships `locals.scm` -- deliberately **not** vendored. Scope-aware
rename's query set is a closed, individually-vetted list (see
`LocalScopes.h`'s own doc comment in `CLAUDE.md`), not something that grows by
default whenever an upstream file happens to exist; R's assignment-based
scoping (`<-`/`=` bind locally inside a function, `<<-`/`->>`  don't) would
need the same kind of scrutiny lua's decline got before it's safe to wire in.

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
- **Zig.** Researched at the 2026-09-13 batch admission alongside Dockerfile/
  Make/HCL/Nix/Ruby/gitcommit/gitrebase and set aside, not rejected on
  quality. The only actively maintained grammar
  (`tree-sitter-grammars/tree-sitter-zig`, pushed 2026-09-13) ships no
  `test/` directory at all, failing admission policy item 8 outright; the one
  alternative with real history, `maxxnino/tree-sitter-zig` (124★), is
  **archived**. **Unparks when** either a corpus lands upstream on the
  maintained grammar, or a maintained fork adds one -- Zig itself is under
  active, non-experimental development, so this is a tooling gap rather than
  a moving-target problem like Carbon's.

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
| `the-mikedavis/tree-sitter-git-commit` | Archived (13★) | Superseded by `gbprod/tree-sitter-gitcommit` |
| `shunsambongi/tree-sitter-gitignore` | Stale since 2022-05 | A maintained fork appears; `.gitignore` is one pattern per line and reads fine as plain text meanwhile |
| `antosha417/tree-sitter-hocon` | Stale since 2022-11 | A maintained fork appears |
| `monaqa/tree-sitter-mermaid` | Stale since 2024-04 | A maintained fork appears; Mermaid rides Markdown fences as plain text meanwhile |
| `lyndsysimon/tree-sitter-plantuml` | Stale since 2021-12 | A maintained fork appears |
| `nedpals/tree-sitter-v` | Stale since 2023-07 | Superseded by `vlang/v-analyzer`'s `tree_sitter_v` (admitted) |
| `jiyee/tree-sitter-objc` | Stale since 2022-08 | Superseded by `tree-sitter-grammars/tree-sitter-objc` (admitted) |
| `elves/tree-sitter-elvish` | Stale since 2023-07 | A maintained fork appears |
| `slackhq/tree-sitter-hack` | Archived upstream | A maintained fork appears |
| `serenadeai/tree-sitter-scss`, `ikatyang/tree-sitter-vue`, `alemuller/tree-sitter-vhdl` | Stale personal originals | Superseded by the `tree-sitter-grammars` forks / `jpt13653903/tree-sitter-vhdl` (admitted) |
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
