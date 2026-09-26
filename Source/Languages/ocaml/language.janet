{:name "ocaml"
 :extensions [".ml"]
 :injection-aliases ["ml"]
 :block-comment ["(*" "*)"]
 :signature-template "let ned_sig {} = ()"
 :lsp-root-markers ["dune-project" "opam" ".ocamlformat"]
 :not-applicable {:injections   "nothing in it is written in another language"
                  :continuation "ocamlformat lines a wrapped infix chain up under its first operand; keyword bodies indent through @indent.headed"}
}
