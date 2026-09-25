{:name "typst"
 :extensions [".typ"]
 :injection-aliases ["typ"]
 :line-comment "//"
 :lsp-root-markers ["typst.toml"]
 :wrap-lines true
 :first-pattern-wins true
 :import-resolution {:extensions ["typ"]}
 :not-applicable {:indents "the grammar's delimited bodies are its whole indent structure"
                  :tests   "no test framework runs tests written in it"}
}
