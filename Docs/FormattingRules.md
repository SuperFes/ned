# Formatting

How ned decides indent style, what's bundled as a safe default, and how to override it for
your own project or personal taste. Design rationale and the full nine-rule-kind catalogue
(what a complete configurable formatter needs, sorted into what's shipped/planned/out of
scope): `Docs/FormattingCapabilities.md`. This page is the settings reference -- `Themes.md`'s
role, for the formatter.

## What's here today

One native pass: structural indentation, per language, via a compiled-in safe default plus
your own overrides. `format-buffer` (`C-c f f`) runs it (or your configured external
formatter, if one's set -- `ned/set-format-command`) over the whole buffer; `ned --format
<files...>` runs the same chain headlessly, for a pre-commit hook or a script, with no editor
UI and no `init.janet`.

Trailing-whitespace cleanup, blank-line collapsing, and a capture-scoped
per-construct indent override (`ned/set-indent-rule`) are planned but not built yet -- see
`Docs/FormattingCapabilities.md` and `ROADMAP.md`.

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
{:indent {:python {:width 2}}   # this team writes 2-space Python, not PEP 8's 4
          :go     {:width 8}}   # display width only -- gofmt's own tabs are unaffected
 :trim-trailing-whitespace true
 :ensure-final-newline true}
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

Top-level keys, and the two fields inside each `:indent` entry (keyed by a language key --
`python`, `cpp`, the same key the defaults table above and `ned/set-lsp-command` both use).
This list is held against the real schema on every build:

<!-- format-keys:begin -->

`indent` `trim-trailing-whitespace` `ensure-final-newline` `tabs` `width`

<!-- format-keys:end -->

## The `--format` CLI

```sh
ned --format file1.py file2.js
```

Headless -- no Notcurses, no event loop, no `init.janet` (format.janet only, both tiers).
Runs your configured external formatter if one's set (`ned/set-format-command` -- but see
the note below), falling back to a native per-language reindent when none is configured or
the external one fails. Whole-file only: a fresh headless process has no edit history to
scope a smaller pass against.

**External formatter configuration is Janet-only today**, so it has no effect in headless
mode -- `--format` never loads `init.janet`, and `ned/set-format-command` has no other way
to be set. The fallback check stays in the chain anyway, both so it reads honestly as
"external, then native" and so a future `format.janet` field for an external command (which
would need its own trust-gate entry first, being a shell command) lights it up for free.
Until then, `--format` always runs the native reindent.

Exit code is non-zero if any file couldn't be formatted (missing, a directory, unreadable,
...); everything else is still attempted.
