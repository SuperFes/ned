{:name "haskell"
 :extensions [".hs" ".hs-boot"]
 :injection-aliases ["hs"]
 :preserve-indent true
 :line-comment "--"
 :signature-template "nedSig {} = ()"
 :signature-clauses true
 :lsp-root-markers ["stack.yaml" "cabal.project" "hie.yaml"]
 :import-resolution {:extensions ["hs" "lhs"] :source-roots ["src" "app" "lib" "test"]}
 :not-applicable {:continuation "indentation is syntax (`:preserve-indent`)"}
}
