# The interface grammar of tree-sitter-ocaml, for .mli files.
{:name "ocaml-interface"
 :extensions [".mli"]
 :block-comment ["(*" "*)"]
 :queries {:indents ["ocaml/indents.janet"]}
 :lsp-root-markers ["dune-project" "opam" ".ocamlformat"]
 :not-applicable {:injections "nothing in it is written in another language"
                  :tests      "interfaces declare; tests live in implementations"
                  :signatures "declarations only; its implementation's change-signature owns the call sites"}
}
