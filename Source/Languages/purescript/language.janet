{:name "purescript"
 :extensions [".purs"]
 :injection-aliases ["purs"]
 :preserve-indent true
 :line-comment "--"
 :lsp-root-markers ["spago.yaml" "spago.dhall"]
 :import-resolution {:extensions ["purs"] :source-roots ["src" "test"]}
 :not-applicable {:continuation "indentation is syntax (`:preserve-indent`)"}
}
