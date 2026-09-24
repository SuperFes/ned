# The interface grammar of tree-sitter-ocaml, for .mli files.
{:name "ocaml-interface"
 :extensions [".mli"]
 :block-comment ["(*" "*)"]
 :queries {:indents ["ocaml/indents.janet"]}
 :lsp-root-markers ["dune-project" "opam" ".ocamlformat"]
}
