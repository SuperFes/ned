# The interface grammar of tree-sitter-ocaml, for .mli files.
{:name "ocaml-interface"
 :extensions [".mli"]
 # Not offside, but nothing yet indents it better than the author did.
 :preserve-indent true
 :line-comment "(*"
 :lsp-root-markers ["dune-project" "opam" ".ocamlformat"]
}
