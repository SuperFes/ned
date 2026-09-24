# Language Matrix

Which capability each bundled language package ships, as ned resolves it (vendored
upstream queries and `:queries-from` included). **Generated** from
`Source/Languages/` by `Tests/LanguageMatrixDocTest.cpp` -- the test fails when this
file is stale. Regenerate with

    NED_BLESS_LANGUAGE_MATRIX=1 ./build/Tests/ned_tests "[LanguageMatrixDoc]"

A `✓` means the package ships the piece, not that the feature is verified to work
well for that language; known behavioural gaps are tracked in `ROADMAP.md`. Folds,
bracket matching and sticky scroll are not listed: they come from the grammar itself
for every language. See `LanguageCoverage.md` for tiers and admission policy.

- **hl** -- highlights query
- **ind** -- indents query (without one, indent comes from the grammar's delimited bodies alone)
- **loc** -- locals query -- scope-aware rename, local-variable highlighting
- **tags** -- tags query -- symbol gutter, outline, breadcrumbs, class/file sync
- **inj** -- injections query -- embedded languages
- **imp** -- imports query -- go-to-file through imports, rename-file fixups
- **test** -- tests query -- test discovery for the test runner
- **sig** -- signatures + calls queries -- change-signature
- **fmt** -- format query -- capture-driven formatter rules can apply
- **fmt-cmt** -- format query names `@comment` -- joins never pull code onto a comment
- **style** -- bundled `style.janet` -- formatter rules apply with no user config
- **cmt** -- line-comment prefix -- toggle-line-comment, comment-aware fill
- **root** -- LSP root markers
- **res** -- import resolution config

| language | hl | ind | loc | tags | inj | imp | test | sig | fmt | fmt-cmt | style | cmt | root | res |
|---|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| ada | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| apacheconf | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| asciidoc | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| asciidoc-inline | ✓ | · | · | · | ✓ | · | · | · | · | · | · | · | · | · |
| asm | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| astro | ✓ | · | · | · | ✓ | · | · | · | · | · | · | · | · | · |
| awk | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| bash | ✓ | ✓ | ✓ | · | · | ✓ | · | · | ✓ | · | · | ✓ | · | ✓ |
| c | ✓ | ✓ | ✓ | ✓ | · | ✓ | · | · | ✓ | · | · | ✓ | ✓ | · |
| caddy | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| clojure | ✓ | ✓ | ✓ | · | · | ✓ | · | · | ✓ | · | · | ✓ | · | ✓ |
| cmake | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| commonlisp | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| cpp | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ | ✓ | · | · | ✓ | ✓ | · |
| crystal | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| csharp | ✓ | ✓ | ✓ | ✓ | · | · | ✓ | · | ✓ | · | · | ✓ | ✓ | · |
| css | ✓ | · | · | · | · | ✓ | · | · | · | · | · | · | · | ✓ |
| csv | ✓ | · | · | · | · | · | · | · | · | · | · | · | · | · |
| cuda | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| cue | ✓ | · | ✓ | · | ✓ | · | · | · | · | · | · | ✓ | · | · |
| d | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| dart | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| desktop | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| diff | ✓ | · | · | · | · | · | · | · | · | · | · | · | · | · |
| dockerfile | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| dotenv | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| earthfile | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| editorconfig | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| elixir | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| elm | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| erlang | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| fennel | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| fish | ✓ | ✓ | ✓ | · | · | · | · | · | ✓ | · | · | ✓ | · | · |
| fortran | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| fsharp | ✓ | · | ✓ | · | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| fundamental | · | · | · | · | · | · | · | · | · | · | · | · | · | · |
| gdscript | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| gitattributes | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| gitcommit | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| gitconfig | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| gitignore | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| gitrebase | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| gleam | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| glsl | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| go | ✓ | ✓ | ✓ | ✓ | · | · | ✓ | · | ✓ | · | · | ✓ | ✓ | · |
| groovy | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| haskell | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| hcl | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| hlsl | · | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| html | ✓ | ✓ | · | · | ✓ | · | · | · | · | · | · | · | · | · |
| http | ✓ | · | · | · | ✓ | · | · | · | · | · | · | ✓ | · | · |
| ini | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| janet | ✓ | ✓ | ✓ | · | · | ✓ | · | · | ✓ | · | · | ✓ | · | ✓ |
| jank | ✓ | ✓ | ✓ | · | · | ✓ | · | · | ✓ | · | · | ✓ | · | ✓ |
| java | ✓ | ✓ | ✓ | ✓ | · | · | ✓ | · | ✓ | · | · | ✓ | ✓ | · |
| javascript | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | · | ✓ | · | · | ✓ | ✓ | ✓ |
| json | ✓ | · | · | · | · | · | · | · | · | · | · | · | · | · |
| json5 | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| jsonnet | ✓ | · | ✓ | · | · | · | · | · | · | · | · | ✓ | · | · |
| julia | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| just | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| kdl | ✓ | · | ✓ | · | ✓ | · | · | · | · | · | · | ✓ | · | · |
| kotlin | ✓ | ✓ | ✓ | ✓ | · | · | ✓ | · | ✓ | · | · | ✓ | ✓ | · |
| latex | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| lua | ✓ | ✓ | · | ✓ | · | · | · | · | ✓ | · | · | ✓ | ✓ | · |
| make | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| markdown | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | · | · | · |
| markdown-inline | ✓ | · | · | · | · | · | · | · | · | · | · | · | · | · |
| matlab | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| meson | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| nginx | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| nim | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| nix | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| nu | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| objc | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| ocaml | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| ocaml-interface | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| odin | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| org | ✓ | · | · | · | ✓ | · | · | · | · | · | · | ✓ | · | · |
| pascal | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| pem | ✓ | · | · | · | · | · | · | · | · | · | · | · | · | · |
| perl | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| php | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| pkl | ✓ | · | ✓ | · | ✓ | · | · | · | · | · | · | ✓ | · | · |
| powershell | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| properties | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| proto | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| psv | ✓ | · | · | · | · | · | · | · | · | · | · | · | · | · |
| purescript | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| python | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | · | ✓ | · | · | ✓ | ✓ | ✓ |
| r | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| racket | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| requirements | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| rescript | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| ron | ✓ | · | ✓ | · | ✓ | · | · | · | · | · | · | ✓ | · | · |
| rst | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| ruby | ✓ | ✓ | · | ✓ | · | · | · | · | ✓ | · | · | ✓ | · | · |
| rust | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | · | ✓ | · | · | ✓ | ✓ | ✓ |
| scala | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| scheme | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| scss | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| solidity | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| sql | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| ssh_config | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| starlark | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| svelte | ✓ | · | ✓ | · | ✓ | · | · | · | · | · | · | · | · | · |
| swift | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | ✓ | · |
| systemd | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| tcl | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| thrift | ✓ | · | ✓ | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| toml | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| tsv | ✓ | · | · | · | · | · | · | · | · | · | · | · | · | · |
| tsx | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | · | ✓ | · | · | ✓ | ✓ | ✓ |
| typescript | ✓ | ✓ | ✓ | ✓ | · | ✓ | ✓ | · | ✓ | · | · | ✓ | ✓ | ✓ |
| typst | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| udev | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| v | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | ✓ | · |
| vala | ✓ | · | ✓ | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| verilog | ✓ | · | · | ✓ | · | · | · | · | · | · | · | ✓ | · | · |
| vhdl | ✓ | · | · | ✓ | ✓ | · | · | · | · | · | · | ✓ | · | · |
| vue | ✓ | · | · | · | ✓ | · | · | · | · | · | · | · | · | · |
| wgsl | ✓ | · | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| xml | ✓ | ✓ | · | · | · | · | · | · | · | · | · | · | · | · |
| yaml | ✓ | ✓ | · | · | · | · | · | · | · | · | · | ✓ | · | · |
| **124 languages** | 122 | 22 | 48 | 79 | 44 | 13 | 11 | 1 | 19 | 1 | 1 | 108 | 40 | 11 |
