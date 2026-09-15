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

Top-level keys, and the fields inside each `:indent`/`:space`/`:break` entry. `:indent` is
keyed by a language key (`python`, `cpp`, the same key the defaults table above and
`ned/set-lsp-command` both use). This list is held against the real schema on every build:

<!-- format-keys:begin -->

`indent` `space` `break` `trim-trailing-whitespace` `ensure-final-newline`
`max-consecutive-blank-lines` `tabs` `width` `before` `after` `within` `placement`
`collapse-empty` `collapse-simple`

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

**Two pilots exist today, cpp and JavaScript** (`Source/Languages/cpp/format.janet`,
`Source/Languages/javascript/format.janet` -- the same two capture NAMES, over each
grammar's own different node types: cpp's `compound_statement`/`condition_clause` vs.
JavaScript's `statement_block`/`parenthesized_expression`), both wired into
`format-buffer`'s and `--format`'s Native chain (reindent, then Break, then Space, then
Hygiene) and both shipping no built-in default -- neither does anything until you
configure a rule:

- **Break-kind captures** (`Editor/FormatBracePlacement.h`'s `ComputeBracePlacementEdits`,
  reading only `:break`'s `:placement` field -- `:collapse-empty`/`:collapse-simple` are
  still unconsumed): `brace.function` (a function definition's own body), `brace.control`
  (an `if`/`while`/`for`/`switch`/`catch` statement's own body -- one shared name, matching
  JetBrains' own "Other statements and blocks" grouping), `brace.class` (a class/struct
  body), and, cpp only, `brace.namespace`.
  ```janet
  (ned/set-format-brace-placement "brace.function" "next-line")
  (ned/set-format-brace-placement "brace.control" "same-line")
  ```
- **Space-kind captures** (`Editor/FormatSpacing.h`'s `ComputeSpaceEdits`, reading all
  three `:space` fields; deliberately never touches a whitespace run that crosses a
  newline -- a Space rule never second-guesses wherever a line break already is):
  `control.parens`, covering `if`/`while`/`switch`'s own condition and, cpp only, a
  `catch` clause's own parameter parens.
  ```janet
  (ned/set-format-space-before "control.parens" true)
  (ned/set-format-space-after "control.parens" true)
  ```
  **Deliberately not captured, in either language:** a `for` loop's own
  `(init; condition; update)` -- neither grammar has one node spanning the whole
  parenthesized clause the way if/while/switch do (it's three independent, individually
  optional fields around bare anonymous `(`/`)` tokens), and `ComputeSpaceEdits`'/
  `ComputeBracePlacementEdits`' shared model requires a capture's own first/last byte to
  BE the delimiter pair. Declined rather than approximated. JavaScript's own `catch`
  clause is declined for the same structural reason (no parens node at all in its
  grammar -- and ES2019+ allows a parameter-less `catch { }`), which is a real,
  load-bearing difference from cpp's own `catch_clause`, not an oversight.
  Combined in one `format.janet`, with cpp's own brace-placement exception:
  ```janet
  {:break {"brace.function" {:placement :same-line}       # the shared rule (JavaScript gets this)
           "cpp/brace.function" {:placement :next-line}}  # cpp's own override
   :space {"control.parens" {:before true}}}              # every language's if/while parens
  ```
  Formatting a `.cpp` and a `.js` file with this one config genuinely produces two
  different brace styles: cpp's function gets Allman (its own override), JavaScript's
  gets K&R (the shared rule) -- both get the same `if (x)` spacing.

No other bundled language has a `format.janet` yet, and no capture yet reads `:within` on
an empty pair, `:collapse-empty`, or `:collapse-simple`. This is the proof that the full
chain (query -> `Mode::formatCaptures` -> `FormatRules` resolution -> a computed edit ->
applied to a live buffer) works end to end for both rule kinds AND across two real
languages sharing one rule set with one exception, ahead of rolling the remaining rule
kinds/languages out (see `Docs/FormattingCapabilities.md`'s Tier B1).

The same rules are settable live from `init.janet`, per-field, mirroring
`ned/set-capture-*`'s own shape:

```janet
(ned/set-format-space-before "cpp/control.parens" false)
(ned/set-format-brace-placement "brace.function" "next-line")
```

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
