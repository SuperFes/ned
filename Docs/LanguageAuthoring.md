# Authoring a language

A language in ned is a directory -- a *package*:

```
<name>/
  language.janet     what the language is: extensions, comment syntax, keymap, ...
  grammar.janet      the grammar, as Janet data
  tables             the grammar compiled (ned --compile-language; rebuilt from grammar.janet)
  highlights.janet   ned's own queries, one file per kind
  tags.janet
  indents.janet
  upstream/          queries taken from the grammar's own repository, unmodified
    highlights.janet
  corpus/            test cases: input and the tree it must parse to
  scanner/           the external scanner, when the grammar needs one
```

The bundled languages under `Source/Languages/` are exactly this shape, and a
language you write yourself goes in `$XDG_CONFIG_HOME/ned/languages/<name>/` (or a
project's `.ned/languages/<name>/`, trust-gated) where it loads at startup. Nothing
about a language is compiled into the editor: the grammar is `grammar.janet`, ned's
own generator turns it into parse tables, and ned's own engine runs them.

Three commands cover the loop; each is also reachable as its own tool, so it
tab-completes on its own name.

| command | also | does |
| --- | --- | --- |
| `ned --import-language <url-or-dir> [--name n] [--subdir s] [--ref tag-or-commit] [--into root]` | `ned-import-language` (symlink) | a tree-sitter grammar repository → a package |
| `ned --compile-language <dir>... [-o file]` | `ned-langc` (program) | `grammar.janet` → `tables` |
| `ned --test-language <dir>... [--bless]` | `ned-test-language` (symlink) | run the corpus; `--bless` rewrites stale expected trees |

`ned-langc` is a separate binary rather than a symlink onto `ned`: the generator
depends on no editor code, and keeping it its own program is what stops an edit
anywhere in the editor from invalidating every compiled table. It takes the same
command line either way — `ned-langc <dir>` and `ned-langc --compile-language <dir>`
both work — and produces byte-identical output to `ned --compile-language`.

## Importing a tree-sitter grammar

```sh
ned --import-language https://github.com/justinmk/tree-sitter-ini
```

clones the repository shallowly and writes `~/.config/ned/languages/ini/`:

- `grammar.janet` converted from `src/grammar.json` (rule for rule; see the
  vocabulary below).
- `upstream/<kind>.janet` from each of `queries/{highlights,tags,injections,locals}.scm`
  the repository ships, converted to ned's spelling.
- `corpus/` copied from `test/corpus/`.
- `scanner/` with the repository's `scanner.c` staged and, when the `port-scanner`
  helper is beside ned (`libexec/ned/port-scanner`, or `Tools/port-scanner.py` in a
  source tree), a `<Name>Scanner.cpp` ported the mechanical half of the way -- see
  "External scanners" for the rest.
- `language.janet`, a skeleton: the name, the file types the repository declares
  (`:extensions`, and `:filenames` for dotfiles and whole basenames), and
  a header recording what the admission policy asks for (`Docs/LanguageCoverage.md`:
  the generated ABI version, the scanner's size, the corpus size), plus the keys to
  fill in.

It then compiles the tables and runs the corpus, printing the scorecard. A grammar with
external tokens stops before the corpus with the scanner still to build; everything else
ends with `N cases, N passed`.

`--subdir` handles a repository holding several grammars (`tree-sitter-typescript`'s
`typescript` and `tsx`, `tree-sitter-php`'s `php`); `--into` writes somewhere other than
the user languages root -- `Source/Languages` when bundling.

## Writing a grammar by hand

`grammar.janet` is one Janet struct. It reads exactly like a tree-sitter `grammar.js`,
with the same rule combinators as keyword-headed tuples:

```janet
{:name "ini"
 :extras [(:pattern "\\s") comment]
 :rules {document (:repeat (:choice section setting))
         section (:seq "[" (:field :name (:pattern "[^\\]]+")) "]" (:repeat setting))
         setting (:seq (:field :key name) "=" (:field :value value))
         name (:pattern "[A-Za-z_][A-Za-z0-9_.-]*")
         value (:pattern "[^\\n]*")
         comment (:token (:seq (:choice ";" "#") (:pattern ".*")))}}
```

| grammar.js | grammar.janet |
| --- | --- |
| `$.rule` | `rule` (a bare symbol; `(:ref "nil")` for a name Janet would read as a value) |
| `'text'` | `"text"` |
| `/re/i` | `(:pattern "re" "i")` |
| `blank()` | `:blank` |
| `seq(a, b)`, `choice(a, b)` | `(:seq a b)`, `(:choice a b)` |
| `repeat(x)`, `repeat1(x)` | `(:repeat x)`, `(:repeat1 x)` |
| `optional(x)` | `(:choice x :blank)` |
| `prec(1, x)`, `prec.left(x)`, `prec.right('name', x)`, `prec.dynamic(2, x)` | `(:prec 1 x)`, `(:prec-left x)`, `(:prec-right "name" x)`, `(:prec-dynamic 2 x)` |
| `token(x)`, `token.immediate(x)` | `(:token x)`, `(:token-immediate x)` |
| `alias(x, $.name)`, `alias(x, 'str')` | `(:alias x name)`, `(:alias x "str")` |
| `field('name', x)` | `(:field :name x)` |
| `reserved('ctx', x)` | `(:reserved :ctx x)` |

Top-level keys: `:name`, `:rules` (an ordered struct; the first rule is the start
rule), `:extras`, `:externals`, `:conflicts`, `:precedences`, `:inline`,
`:supertypes`, `:word`, `:reserved`. The semantics are tree-sitter's -- precedence and
associativity resolve conflicts the same way, an undeclared conflict is an error naming
the two interpretations, and the generator's own error messages are the ones
`ned --compile-language` prints, with the file and line.

Regular expressions are the subset the reference generator accepts: classes with
the reference's set operations (`[a-z&&[^aeiou]]`, `--`, `~~`), `\p{Letter}`-style
Unicode properties including the identifier (`XID_Start`) and emoji (`Emoji`, `EMod`)
ones, quantifiers, groups, alternation; `\w`, `\s` and `\d` are ASCII.

The loop is: edit `grammar.janet`, run `ned --test-language <dir>`, read the trees that
changed, `--bless` the ones that are right.

## Corpus files

`corpus/*.txt` in tree-sitter's format -- a fenced header naming the case, the input, a
`---` divider, the expected tree:

```
==================
A setting
==================

key = value

---

(document
  (setting
    key: (name)
    value: (value)))
```

`:skip`, `:error` (the input must fail to parse), `:platform(linux)`,
`:language(name)` and `:cst` markers after the case name work as they do upstream.
Fields in an expected tree are compared only when the tree spells them. A `:cst`
case's expected text is the reference's concrete-syntax listing -- every node with
its `row:column` range, anonymous tokens quoted, leaf text in backticks -- compared
verbatim, which pins positions the S-expression does not:

```
==================
A setting
:cst
==================

key = value

---

0:0  - 1:0    document
0:0  - 0:11     setting
0:0  - 0:3        key: name `key`
0:4  - 0:5        "="
0:6  - 0:11       value: value `value`
```

## External scanners

Some tokens no regular expression can express -- heredocs, indentation, string
interpolation. A grammar lists them under `:externals`, and a scanner supplies them:
five functions over the engine's lexer, compiled against three headers ned installs
under `include/ned/Editor/Parse/`:

```cpp
#include "Editor/Parse/Scanner.h"

namespace {
using namespace ned::editor::parse::scanner;

enum TokenType { LINE_END };

void*    Create() { return nullptr; }
void     Destroy(void*) {}
unsigned Serialize(void*, char*) { return 0; }
void     Deserialize(void*, const char*, unsigned) {}
bool     Scan(void*, Lexer* lexer, const bool* validSymbols) {
    if (!validSymbols[LINE_END] || lexer->lookahead != '\n')
        return false;
    lexer->advance(lexer, false);
    lexer->markEnd(lexer);
    lexer->resultSymbol = LINE_END;
    return true;
}

const ned::editor::parse::ScannerVTable kScanner = {Create, Destroy, Scan, Serialize, Deserialize};
} // namespace

NED_SCANNER_LIBRARY_EXPORT(ini, kScanner)
```

`validSymbols` is indexed in `:externals` order; `Serialize`/`Deserialize` carry the
scanner's state across incremental reparses in at most `kSerializationBufferSize`
bytes. `Editor/Parse/ScannerSupport.h` adds the growable-array vocabulary
(`Array(T)`, `array_push`, ...) tree-sitter scanners are written with.

Build it as a shared library and name it in `language.janet`:

```sh
c++ -std=c++23 -shared -fPIC -I/usr/include/ned scanner/IniScanner.cpp -o scanner/libned-ini-scanner.so
```

```janet
{:name "ini"
 :extensions [".ini"]
 :scanner-library "/home/me/.config/ned/languages/ini/scanner/libned-ini-scanner.so"}
```

A scanner imported from a tree-sitter repository arrives with the mechanical part of
the port done (`port-scanner`: the headers, the lexer's member names, the entry
points); what remains is the C-to-C++ friction a compiler names -- a `void*` that
needs a cast, an enum assigned an `int`. The bundled scanners under
`Source/Editor/Languages/Scanners/` are 75 worked examples.

## Queries

ned's queries are Janet files, one per kind, discovered beside `language.janet`:
`highlights.janet`, `tags.janet`, `indents.janet`, `locals.janet`, `injections.janet`,
`imports.janet`, `tests.janet`, `format.janet`, `signatures.janet`, `calls.janet`. An
`upstream/<kind>.janet` is read first and ned's own file after it, so a grammar's
shipped queries are consumed unmodified and ned's additions are a delta.
`Docs/Scripting.md`'s `ned/register-language` entry lists every `language.janet` key;
the bundled languages are the reference for the query dialect
(`Source/Languages/python/` is a complete one). `Docs/LanguageMatrix.md` shows which
bundled language ships which kind.

### Indentation

Without an indents query, indentation comes from the grammar's delimited bodies alone.
`:preserve-indent true` marks a language whose indentation is syntax (Haskell, Elm, F#,
PureScript): a reindent leaves every line as written and Enter copies the previous
line's indent.

A statement or expression written across lines (`x = a +` then `b`, a method chain, a
ternary) goes a continuation step past its first line wherever the indents query
captures `@indent.continuation`; Enter after an unfinished one lands there too. The
step is `IndentStyle::continuation` (`ned/set-continuation-indent`; two levels for Java
and Dart). A line continued with a `:line-continuation` marker (`\` in C's
preprocessor, shells, Python, Ruby, awk and Dockerfiles), which most grammars skip as
whitespace, is left as written. `@aligned.colons` lines an Objective-C message's
selector parts up on their colons.

An unbraced control-statement body (`if (x)` then `foo();`) is captured
`@indent.branch`, with `(:not-match? @indent.branch "^\\{")` keeping a braced one
out: it sits one level in when it starts its own line, and adds nothing when it shares
its header's line (`if (x) foo();`, or the `if` of `else if`).

### Locals

`locals.janet` marks scopes, definitions and references. A definition's
`.pattern` suffix binds every name inside a pattern (`local.pattern.name` picks the
node type); `local.assignment` binds only where no outer binding is visible;
`local.namespace` keeps same-spelled names apart (Perl's sigils);
`local.case-insensitive` folds case (PowerShell); `local.pun` marks an occurrence that
is also a label or key spelled the same (`{ x }`, `~x`) with the text a rename writes
there, `{old}` and `{new}` filled in (`"{old}: {new}"` in JavaScript).

### change-signature

`signatures.janet` and `calls.janet` pair up: `@signature.definition`/`@signature.name`/
`@signature.parameters` and `@call.definition`/`@call.callee`/`@call.arguments`. Each
language describes its own parameters (`@parameter` with `.name`, `.default`,
`.variadic`, `.keyword`, `.group`, `.skip`), receivers (`@parameter.receiver` for
`self`, `cls`, Lua's colon calls, Nim's dot calls, C#'s extension `this`), and
named/spread arguments (`@argument.named` with `@argument.name`, `@argument.spread`);
`:signature-template` parses the retyped list. A list written flat, as the definition's
or call's own children, is named by its opening paren (`@signature.parameters.open`,
`@call.arguments.open`: Swift, Solidity, Vala, Odin), and a Swift parameter's label
(`@parameter.label`) goes with a new default. A constructor is found wherever it is
called: by its class (`new Box(...)`, Swift's `Box(...)`, Solidity's `is Vault(...)`,
`@signature.callee`), and through the class or its base (`parent::__construct`,
`super().__init__`, `this(...)`, `: base(...)` -- `@call.class` with `@call.base`/
`@call.class.name`, and a call marked `@call.callee.base`/`.class`). A pipe fills the
first parameter (Gleam's `|>`, `@call.receiver.first`), so moving that parameter
declines the piped call. C and C++ read their declarators directly instead.

### Imports

`imports.janet` marks the specifiers; `:import-resolution` in `language.janet` says how
one becomes a path: `:source-roots` under the package root (the nearest LSP root marker)
and the project root, `:module-separator` (Perl's `::`), `:module-substitutions`
(Clojure's `-` for `_`), `:partial-prefix` (Sass's `_`), `:root-prefixes` (Godot's
`res://`) and `:package-scheme` (Dart's `package:`, through
`.dart_tool/package_config.json`, else the enclosing pubspec). `:go-modules` reads Go
import paths through the nearest `go.mod`: its own module, then its `replace`
directives and requirements (in the module cache), then the standard library under
`GOROOT`. `:package-directories` says an import names a directory (Go, V, Odin):
go-to-file opens `doc.<ext>`, else the file named after the directory, else its first
non-`_test` source file, and a move rewrites the import only when every source file in
the directory went to the same place. `:odin-collections` reads `name:path` imports
through the nearest `ols.json`'s collections, then the toolchain's own under
`ODIN_ROOT`; `:home-prefix` counts a `~/` path from `$HOME` (ssh_config, gitconfig). A
project's
`importResolution.<language>.sourceRoots` replaces the roots. A host language with
`:injected-imports` (Svelte, Vue, Astro, HTML) also reads its embedded scripts'
imports, resolved with the host's own `:import-resolution`. `@import.package` marks
a file's own package declaration (Java, Kotlin, Groovy, Scala): when a move takes the
file to another directory under the same source root, its package is rewritten, and
`:import-statement` (`"import {};"`) is how the imports it and its old package's other
files then need are written. The same resolution drives
go-to-file and the fixups when a file is renamed or moved.

### Formatting

`format.janet` names a language's braces, control parens, `else`/`catch` keywords and
top-level/method definitions for the capture-driven formatter rules
(`Docs/FormattingRules.md`); `:braces-on-header-line` (Go, Odin, V) refuses a placement
rule that would move a brace off its header's line. A bundled `style.janet` gives the
rules defaults with no user config, following the language's official style guide or
canonical formatter.

## Bundling

A language becomes bundled by landing its package under `Source/Languages/<name>/`; the
build compiles its tables (`CMake/LanguageTables.cmake`), the install ships them, and
the corpus joins the conformance suite (`Tests/ParseConformanceTest.cpp`'s
`CorpusSources`). A bundled scanner is a `<Name>Scanner.cpp` under
`Source/Editor/Languages/Scanners/` registered in `Scanners.cpp`, not a shared library.
Admission is `Docs/LanguageCoverage.md`'s policy; the import's skeleton header records
the facts it asks for.

Every bundled package is a row of `Docs/LanguageMatrix.md`, generated by
`Tests/LanguageMatrixDocTest.cpp`. A column that doesn't exist for the language -- csv
has no bindings, JSON no comments -- is declared in `language.janet` with its reason,
`:not-applicable {:locals "no bindings to scope or rename"}`, and shows as `–`; every
`·` left is an open gap. Declaring one the package ships fails the test. Keys are the
matrix's capabilities (`kNotApplicableCapabilities` in `LanguageDefinition.h`); no
indentation implies no continuation lines, no imports nothing to resolve, and no format
query no style.
