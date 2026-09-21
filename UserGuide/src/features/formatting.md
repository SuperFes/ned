# Formatting

ned formats code two ways, and you can use either, both, or neither. It can shell out to
whatever formatter your project already uses (`clang-format`, `prettier`, `black`, `gofmt`),
or it can format the buffer itself — a per-language reindent plus a set of rules you
configure, with no external tool installed at all.

Nothing here is on by default beyond indentation and a whitespace cleanup. Every rule
described below is inert until you configure it, so ned never silently restyles a file you
opened.

## Running a format

`format-buffer` (via `M-x` — it ships with no keybinding) formats the whole buffer. It tries
four things in order and stops at the first that produces output:

1. **Your external formatter**, if `ned/set-format-command` is set.
2. **Your language server**, if one is running for the buffer and the external command
   didn't claim the save (`textDocument/formatting`).
3. **ned's native chain** — the per-language reindent, then whichever of the Rewrite,
   Arrange, Blank, Wrap, Break, Space, and Align rules you've configured.
4. **The Hygiene pass** — trailing whitespace, blank-line runs, final newline.

Steps 3 and 4 always run when 1 and 2 are unavailable *or fail at runtime*, so
`format-buffer` never does nothing. `indent-buffer` runs the reindent alone, if that's all
you want.

## Indentation

Every bundled language ships a safe default, taken from that language's own canonical style
where one exists — PEP 8 for Python, rustfmt for Rust, gofmt's mandatory tabs for Go, PSR-12
for PHP, Prettier for JavaScript and TypeScript. A few languages have no settled convention
(C/C++, Java, SQL); those defaults are judgment calls and are the ones you're most likely to
want to change. The full table, with a source citation per row, is in `Docs/FormattingRules.md`
(the [Developer Docs](../../dev/FormattingRules.html) book).

Two are syntax, not taste: Make requires a literal tab to introduce a recipe line, and YAML
forbids literal tabs for indentation entirely.

To override a whole language:

```janet
(ned/set-indent-style "python-mode" false 4)   # mode name, use-tabs?, width
(ned/set-indent-style "" false 4)              # empty name sets the global default
```

To override one construct — keyed by its **grammar node type**, not a capture name:

```janet
(ned/set-indent-rule "access_specifier" "offset" -2)      # C++ `public:` sits 2 columns left
(ned/set-indent-rule "cpp/access_specifier" "offset" -2)  # same, scoped to cpp alone
(ned/set-indent-rule "preproc_def" "absolute" 0)          # a #define always at column 0
(ned/set-indent-rule "access_specifier" "" 0)             # empty policy clears it
```

## Whitespace hygiene

Three rules, each independently settable, all on by default:

| Setting | Default | What it does |
|---|---|---|
| `ned/set-trim-trailing-whitespace-on-save` | on | Strips trailing spaces/tabs; collapses trailing blank lines at end of file |
| `ned/set-max-consecutive-blank-lines` | 2 | Collapses any longer run of blank lines; a negative value disables |
| `ned/set-ensure-final-newline` | on | Exactly one `\n` at end of file |

The first and third also apply to an ordinary `save-buffer`, independently of any formatting.
As part of `format-buffer` they edit the live buffer as one undo step, so you see the result
without saving first.

## `format.janet` — per-project and personal settings

Some preferences belong in the repo, not in your personal config. `format.janet` is a plain
data file (Janet data syntax — structs, keywords, `#` comments; no code, no VM) read by both
the editor and the headless CLI:

- **Personal**: `$XDG_CONFIG_HOME/ned/format.janet` (falls back to `~/.config/ned/format.janet`)
- **Project**: `<root>/.ned/format.janet`

```janet
# .ned/format.janet — committed to the repo, applies to everyone on this project
{:indent {:python {:width 2}      # this team writes 2-space Python, not PEP 8's 4
          :go     {:width 8}}     # display width only; gofmt's tabs are unaffected
 :max-consecutive-blank-lines 1   # stricter than the built-in default of 2
 :space {"control.parens" {:before true}}
 :break {"brace.function" {:placement :next-line}}}
```

**Precedence**: `init.janet` (editor only) > project `format.janet` > personal
`format.janet` > built-in default.

Resolution is a **per-field cascade**, git-config style, not nearest-file-wins. A project
file that sets only `:width` for one language still inherits that language's `:tabs` from
your personal file, which inherits from the built-in table. A team's checked-in style only
has to state what the team actually cares about; everything else falls through to your taste.

Run `reload-format-config` (`M-x`) to re-read both files without restarting — the intended
loop is edit, reload, `format-buffer`, repeat.

Unlike `.ned/init.janet`, `format.janet` is **not** trust-gated: its schema holds only
booleans, integers, and keyword enums, with no field naming a command or path. An unfamiliar
project's `format.janet` applies silently, which is intentional but worth knowing.

## The rule kinds

Beyond indentation, seven rule kinds are configurable. All are keyed by **capture name** — a
dotted identity like `control.parens` or `brace.function` that a language's query file
declares, the same vocabulary highlighting already uses. A bare name is the shared rule every
language with that capture gets; prefix it with a language key (`cpp/brace.function`) to
narrow it to one language's quirk.

| Kind | Key | Configures |
|---|---|---|
| Space | `:space` | `:before` / `:after` / `:within` a token or delimiter pair |
| Break | `:break` | `:placement` (`:same-line` K&R, `:next-line` Allman, `:next-line-indented` GNU), `:collapse-empty`, `:collapse-simple`, `:before`/`:after` |
| Wrap | `:wrap` | `:policy` (`:never` collapse a list to one line, `:always` one item per line), `:force-trailing-comma` |
| Blank | `:blank` | `:min-before` / `:max-before` — blank lines above a construct |
| Align | `:align` | `:enabled` — pad adjacent same-indent lines' anchors to a shared column |
| Arrange | `:arrange` | `:enabled`, `:case-insensitive` — sort a run of adjacent siblings (imports) |
| Rewrite | `:rewrite` | `:quote-style` (`:single`/`:double`), `:expand-elseif` |

A worked example — brace style that differs between two languages sharing one rule set:

```janet
{:break {"brace.function" {:placement :same-line}       # shared: JavaScript gets K&R
         "cpp/brace.function" {:placement :next-line}   # cpp's own override: Allman
         "brace.control" {:collapse-simple true}}       # one-statement if bodies stay inline
 :space {"control.parens" {:before true :after false}}  # `if (x)`, in every language
 :blank {"def.toplevel" {:min-before 2 :max-before 2}   # PEP 8's two blank lines
         "def.method" {:min-before 1}}
 :align {"align.assignment" {:enabled true}}
 :arrange {"arrange.import" {:enabled true :case-insensitive true}}}
```

The same rules are settable live from `init.janet`, one field per call:

```janet
(ned/set-format-brace-placement "brace.function" "next-line")
(ned/set-format-space-before "cpp/control.parens" false)
(ned/set-format-blank-min-before "def.toplevel" 2)
(ned/set-format-wrap-policy "wrap.args" "always")
(ned/set-format-align-enabled "align.assignment" true)
```

Which capture names a given language actually declares varies — sixteen languages ship
brace/space captures today, fewer ship wrap or align ones, and a rule configured against a
name a language doesn't declare is a clean no-op rather than an error. The per-language
inventory is in `Docs/FormattingRules.md`.

Two behaviours worth knowing, because they're deliberate and look like bugs otherwise: a
Space rule never touches a whitespace run that crosses a newline (it won't second-guess a
line break you put there), and `:collapse-simple` declines in both directions when the
statement itself spans multiple physical lines (it won't join something you deliberately
wrapped).

## Naming conventions

Naming is a **checker**, never part of a format. Renaming identifiers on save would be
hostile, so nothing in the format chain touches a name.

```janet
(ned/set-format-case-convention "function" "camel-case")
(ned/set-format-case-convention "parameter" "snake-case")
(ned/set-format-case-convention "cpp/type" "pascal-case")
```

Entity kinds are `function`, `parameter`, `local`, `type`, and `namespace` (optionally
language-scoped). Conventions are `none`, `lowercase`, `uppercase`, `camel-case`,
`pascal-case`, `snake-case`, `leading-snake-case`, `upper-snake-case`,
`screaming-snake-case`, and `lisp-case`.

Run `check-format-conventions` to scan the project into a `*case violations*` buffer, and
`fix-case-violation-at-point` to apply a suggested rename through the same scope-aware
rename pipeline `rename-symbol` uses. A separate setting,
`ned/set-new-file-name-convention`, checks a *new file's own basename* against a language's
convention and notes a mismatch in the status line.

## Formatting on save

Three independent mechanisms, in precedence order:

- **`ned/set-format-command`** — a shell command `save-buffer` pipes the buffer through
  before writing. Whole-buffer, and always wins.
- **`ned/set-lsp-format-on-save`** (default off) — format via the language server on save.
  Ignored when an external command is configured.
- **`ned/set-auto-format-on-save`** (default off) — run ned's own native rules on save, but
  **scoped to the lines you've touched** since the buffer was last loaded or saved, so a file
  converges gradually as you work on it instead of being wholly restyled on first save. A
  rule whose edit would straddle the scope boundary is declined for the scoped pass and still
  applies via an explicit `format-buffer`. Skipped entirely when either mechanism above
  claims the save.

## From the command line

```sh
ned --format file1.py file2.js
ned-format file1.py file2.js      # installed symlink, same thing
```

Headless — no UI, no event loop, and **no `init.janet`**: only the two `format.janet` tiers
are read. Since an external formatter can only be configured from Janet today, `--format`
always runs the native chain in practice. Whole-file only (a fresh process has no edit
history to scope against). Exit code is non-zero if any file couldn't be formatted; the rest
are still attempted.

A file past the huge-file threshold is skipped with a message. `--format --force-huge`
reindents it anyway, via a streaming lexical engine that tracks bracket depth instead of
parsing — all-or-nothing: if depth doesn't return to exactly zero, the whole sweep aborts and
the file is left untouched rather than risk a wrong result somewhere in a file too large to
review by eye.

## Next steps

- `Docs/FormattingRules.md` — the full per-language reference and design record, in the
  [Developer Docs](../../dev/FormattingRules.html) book
- [Language Intelligence](language-intelligence.md)
- [Configuration](../configuration.md)
