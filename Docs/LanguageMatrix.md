# Language Matrix

Which capability each bundled language package ships, as ned resolves it (vendored
upstream queries and `:queries-from` included). **Generated** from
`Source/Languages/` by `Tests/LanguageMatrixDocTest.cpp` -- the test fails when this
file is stale. Regenerate with

    NED_BLESS_LANGUAGE_MATRIX=1 ./build/Tests/ned_tests "[LanguageMatrixDoc]"

A `✓` means the package ships the piece, not that the feature is verified to work
well for that language. A `·` is an open gap: the capability applies to the language
and the package doesn't ship it yet; the totals row counts shipped over applicable.
What doesn't apply is declared per language (`:not-applicable`, with its reason). Folds,
bracket matching and sticky scroll are not listed: they come from the grammar's own
delimited bodies, except in Markdown and Org, whose structure is not delimiters and
which fold from their own query and code. See `LanguageCoverage.md` for tiers and
admission policy.

- **hl** -- highlights query
- **ind** -- indents query (without one, indent comes from the grammar's delimited bodies alone)
- **cont** -- continuation lines -- the indents query captures `@indent.continuation`, so `x = a +` then `b` indents the `b` a continuation step
- **loc** -- locals query -- scope-aware rename, local-variable highlighting
- **tags** -- tags query -- symbol gutter, outline, breadcrumbs, class/file sync
- **inj** -- injections query -- embedded languages
- **imp** -- imports query -- go-to-file through imports, rename-file fixups
- **test** -- tests query -- test discovery for the test runner
- **sig** -- signatures + calls queries -- change-signature
- **fmt** -- format query -- capture-driven formatter rules can apply
- **style** -- bundled `style.janet` -- formatter rules apply with no user config
- **cmt** -- line-comment prefix -- toggle-line-comment, comment-aware fill
- **root** -- LSP root markers
- **res** -- import resolution config

Marks:

- `✓` -- the package ships it
- `·` -- it doesn't, and should: an open gap
- `–` -- doesn't apply to this language (`:not-applicable` in its `language.janet`, with the reason)
- `=` (**ind**) -- `:preserve-indent`: indentation is syntax, so a reindent leaves every line as written
- `i` (**loc**) -- no locals query of its own; rename reads its embedded scripts' locals, and a name bound at a script's top level declines (`:injected-locals`)
- `i` (**tags**) -- no tags query of its own; the outline is what its embedded languages define (`:injected-symbols`)
- `i` (**imp**) -- no imports query of its own; go-to-file and move fixups read its embedded scripts' imports (`:injected-imports`)
- `b` (**cmt**) -- block comments only (`/* */`, `<!-- -->`, `(* *)`); toggle-line-comment wraps each line in one

| language | hl | ind | cont | loc | tags | inj | imp | test | sig | fmt | style | cmt | root | res |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| ada | ✓ | ✓ | ✓ | ✓ | ✓ | – | · | ✓ | ✓ | ✓ | – | ✓ | ✓ | · |
| apacheconf | ✓ | ✓ | – | – | ✓ | – | ✓ | – | – | – | – | ✓ | – | – |
| asciidoc | ✓ | – | – | – | ✓ | ✓ | ✓ | – | – | – | – | ✓ | – | ✓ |
| asciidoc-inline | ✓ | – | – | – | – | ✓ | – | – | – | – | – | – | – | – |
| asm | ✓ | – | – | – | ✓ | ✓ | ✓ | – | – | – | – | ✓ | – | – |
| astro | ✓ | – | – | i | i | ✓ | i | – | – | – | – | b | ✓ | ✓ |
| awk | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ |
| bash | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | – | ✓ |
| c | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | – |
| caddy | ✓ | – | – | ✓ | ✓ | ✓ | ✓ | – | – | ✓ | · | ✓ | – | – |
| clojure | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| cmake | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ |
| commonlisp | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| cpp | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | – |
| crystal | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| csharp | ✓ | ✓ | ✓ | ✓ | ✓ | – | · | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · |
| css | ✓ | ✓ | ✓ | – | ✓ | – | ✓ | – | – | ✓ | ✓ | b | – | ✓ |
| csv | ✓ | – | – | – | – | – | – | – | – | – | – | – | – | – |
| cuda | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | – |
| cue | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | – | – | – | ✓ | ✓ | ✓ |
| d | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| dart | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| desktop | ✓ | – | – | – | ✓ | ✓ | – | – | – | – | – | ✓ | – | – |
| diff | ✓ | – | – | – | ✓ | – | – | – | – | – | – | – | – | – |
| dockerfile | ✓ | – | – | – | ✓ | ✓ | – | – | – | – | – | ✓ | – | – |
| dotenv | ✓ | – | – | – | ✓ | – | – | – | – | – | – | ✓ | – | – |
| earthfile | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | – | ✓ | – | ✓ | – | ✓ |
| editorconfig | ✓ | – | – | – | ✓ | – | – | – | – | – | – | ✓ | – | – |
| elixir | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · |
| elm | ✓ | = | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| erlang | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ |
| fennel | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | · | ✓ | – | ✓ |
| fish | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | – | ✓ |
| fortran | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ |
| fsharp | ✓ | = | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ |
| fundamental | – | – | – | – | – | – | – | – | – | – | – | – | – | – |
| gdscript | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| gitattributes | ✓ | – | – | – | – | – | – | – | – | – | – | ✓ | – | – |
| gitcommit | ✓ | – | – | – | – | – | – | – | – | – | – | ✓ | – | – |
| gitconfig | ✓ | – | – | – | ✓ | – | ✓ | – | – | – | – | ✓ | – | ✓ |
| gitignore | ✓ | – | – | – | – | – | – | – | – | – | – | ✓ | – | – |
| gitrebase | ✓ | – | – | – | – | – | – | – | – | – | – | ✓ | – | – |
| gleam | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| glsl | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ | ✓ | – | ✓ | – | – |
| go | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| groovy | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| haskell | ✓ | = | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| hcl | ✓ | ✓ | ✓ | – | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ |
| hlsl | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ | ✓ | – | ✓ | – | – |
| html | ✓ | ✓ | – | – | i | ✓ | i | – | – | – | – | b | – | ✓ |
| http | ✓ | – | – | ✓ | ✓ | ✓ | ✓ | – | – | – | – | ✓ | – | – |
| ini | ✓ | – | – | – | ✓ | – | – | – | – | – | – | ✓ | – | – |
| janet | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ |
| jank | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| java | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| javascript | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| json | ✓ | – | – | – | ✓ | – | – | – | – | – | – | – | – | – |
| json5 | ✓ | – | – | – | ✓ | – | – | – | – | – | – | ✓ | – | – |
| jsonnet | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ | ✓ | · | ✓ | ✓ | ✓ |
| julia | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ |
| just | ✓ | – | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | · | ✓ | – | ✓ |
| kdl | ✓ | – | – | ✓ | ✓ | ✓ | – | – | – | – | – | ✓ | – | – |
| kotlin | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| latex | ✓ | ✓ | – | – | ✓ | ✓ | ✓ | – | – | – | – | ✓ | ✓ | ✓ |
| lua | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| make | ✓ | – | – | – | ✓ | ✓ | ✓ | – | – | ✓ | – | ✓ | – | – |
| markdown | ✓ | – | – | · | ✓ | ✓ | ✓ | – | – | – | – | b | ✓ | – |
| markdown-inline | ✓ | – | – | – | – | ✓ | ✓ | – | – | – | – | – | – | – |
| matlab | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | – | ✓ | ✓ | – |
| meson | ✓ | ✓ | – | – | – | – | ✓ | ✓ | – | – | – | ✓ | ✓ | ✓ |
| nginx | ✓ | – | – | – | ✓ | ✓ | ✓ | – | – | ✓ | – | ✓ | – | – |
| nim | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ |
| nix | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | – | – | – | ✓ | ✓ | ✓ |
| nu | ✓ | – | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ |
| objc | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | – |
| ocaml | ✓ | ✓ | – | ✓ | ✓ | – | · | ✓ | ✓ | ✓ | · | b | ✓ | · |
| ocaml-interface | ✓ | ✓ | – | ✓ | ✓ | – | · | – | – | ✓ | · | b | ✓ | · |
| odin | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| org | ✓ | – | – | – | – | ✓ | ✓ | – | – | – | – | ✓ | – | ✓ |
| pascal | ✓ | ✓ | ✓ | ✓ | ✓ | – | · | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · |
| pem | ✓ | – | – | – | – | – | – | – | – | – | – | – | – | – |
| perl | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| php | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| pkl | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ |
| powershell | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| properties | ✓ | – | – | – | ✓ | – | – | – | – | – | – | ✓ | – | – |
| proto | ✓ | ✓ | – | – | ✓ | ✓ | ✓ | – | – | ✓ | ✓ | ✓ | ✓ | ✓ |
| psv | ✓ | – | – | – | – | – | – | – | – | – | – | – | – | – |
| purescript | ✓ | = | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ |
| python | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| r | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| racket | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ |
| requirements | ✓ | – | – | – | – | – | – | – | – | – | – | ✓ | – | – |
| rescript | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · |
| ron | ✓ | – | – | ✓ | ✓ | ✓ | – | – | – | – | – | ✓ | – | – |
| rst | ✓ | – | – | – | ✓ | ✓ | ✓ | – | – | – | – | ✓ | – | ✓ |
| ruby | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| rust | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| scala | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| scheme | ✓ | ✓ | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ |
| scss | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ |
| solidity | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| sql | ✓ | ✓ | – | ✓ | ✓ | – | – | ✓ | ✓ | ✓ | – | ✓ | – | – |
| ssh_config | ✓ | – | – | – | ✓ | ✓ | ✓ | – | – | – | – | ✓ | – | ✓ |
| starlark | ✓ | – | – | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ | · | ✓ | ✓ | · |
| svelte | ✓ | – | – | i | i | ✓ | i | – | – | – | – | b | ✓ | ✓ |
| swift | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · |
| systemd | ✓ | – | – | – | ✓ | – | – | – | – | – | – | ✓ | – | – |
| tcl | ✓ | – | – | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ |
| thrift | ✓ | – | – | ✓ | ✓ | ✓ | ✓ | – | – | ✓ | – | ✓ | – | ✓ |
| toml | ✓ | – | – | – | ✓ | – | – | – | – | – | – | ✓ | ✓ | – |
| tsv | ✓ | – | – | – | – | – | – | – | – | – | – | – | – | – |
| tsx | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| typescript | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| typst | ✓ | – | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | – | ✓ | ✓ | ✓ |
| udev | ✓ | – | – | – | ✓ | ✓ | – | – | – | – | – | ✓ | – | – |
| v | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| vala | ✓ | ✓ | ✓ | ✓ | ✓ | – | · | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · |
| verilog | ✓ | ✓ | ✓ | ✓ | ✓ | – | ✓ | ✓ | ✓ | ✓ | – | ✓ | – | ✓ |
| vhdl | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ | – | ✓ | ✓ | · |
| vue | ✓ | – | – | i | i | ✓ | i | – | – | – | – | b | ✓ | ✓ |
| wgsl | ✓ | ✓ | ✓ | ✓ | ✓ | – | – | – | ✓ | ✓ | – | ✓ | – | – |
| xml | ✓ | ✓ | – | – | – | – | ✓ | – | – | – | – | b | – | – |
| yaml | ✓ | ✓ | – | ✓ | ✓ | – | – | – | – | – | – | ✓ | – | – |
| **124 languages** | 123/123 | 78/78 | 53/53 | 83/84 | 109/109 | 54/54 | 84/95 | 64/64 | 68/68 | 78/78 | 43/56 | 115/115 | 67/67 | 69/80 |
