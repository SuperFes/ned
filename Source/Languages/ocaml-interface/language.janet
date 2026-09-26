# The interface grammar of tree-sitter-ocaml, for .mli files.
{:name "ocaml-interface"
 :extensions [".mli"]
 :block-comment ["(*" "*)"]
 :queries {:indents ["ocaml/indents.janet"]}
 :lsp-root-markers ["dune-project" "opam" ".ocamlformat"]
 :import-resolution {:extensions ["mli" "ml"] :flat-modules true}
 :not-applicable {:injections   "nothing in it is written in another language"
                  :tests        "interfaces declare; tests live in implementations"
                  :signatures   "declarations only: its implementation's change-signature rewrites a `val`'s type"
                  :continuation "ocamlformat lines a wrapped infix chain up under its first operand; keyword bodies indent through @indent.headed"}
}
