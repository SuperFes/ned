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

Three commands cover the loop; each is also reachable through a symlink so it
tab-completes as its own tool.

| command | symlink | does |
| --- | --- | --- |
| `ned --import-language <url-or-dir> [--name n] [--subdir s] [--ref tag] [--into root]` | `ned-import-language` | a tree-sitter grammar repository → a package |
| `ned --compile-language <dir>... [-o file]` | `ned-langc` | `grammar.janet` → `tables` |
| `ned --test-language <dir>... [--bless]` | `ned-test-language` | run the corpus; `--bless` rewrites stale expected trees |

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
- `language.janet`, a skeleton: the name, the extensions the repository declares, and
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

Regular expressions are the subset the reference generator accepts: classes,
`\p{Letter}`-style Unicode properties, quantifiers, groups, alternation; `\w`, `\s`
and `\d` are ASCII.

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

`:skip`, `:error` (the input must fail to parse), `:platform(linux)` and
`:language(name)` markers after the case name work as they do upstream. Fields in an
expected tree are compared only when the tree spells them.

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
`Source/Editor/Languages/Scanners/` are 29 worked examples.

## Queries

ned's queries are Janet files, one per kind, discovered beside `language.janet`:
`highlights.janet`, `tags.janet`, `indents.janet`, `locals.janet`, `injections.janet`,
`imports.janet`, `tests.janet`, `format.janet`. An `upstream/<kind>.janet` is read
first and ned's own file after it, so a grammar's shipped queries are consumed
unmodified and ned's additions are a delta. `Docs/Scripting.md`'s `ned/register-language`
entry lists every `language.janet` key; the bundled languages are the reference for the
query dialect (`Source/Languages/python/` is a complete one).

## Bundling

A language becomes bundled by landing its package under `Source/Languages/<name>/`; the
build compiles its tables (`CMake/LanguageTables.cmake`), the install ships them, and
the corpus joins the conformance suite (`Tests/ParseConformanceTest.cpp`'s
`CorpusSources`). A bundled scanner is a `<Name>Scanner.cpp` under
`Source/Editor/Languages/Scanners/` registered in `Scanners.cpp`, not a shared library.
Admission is `Docs/LanguageCoverage.md`'s policy; the import's skeleton header records
the facts it asks for.
