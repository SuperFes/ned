{:name "typst"
 :extensions [".typ"]
 :injection-aliases ["typ"]
 :line-comment "//"
 :signature-template "#let __ned_sig({}) = none"
 :lsp-root-markers ["typst.toml"]
 :wrap-lines true
 :first-pattern-wins true
 :import-resolution {:extensions ["typ"]}
 :not-applicable {:indents "the grammar's delimited bodies are its whole indent structure"
                  :tests   "no test framework runs tests written in it"
                  :format  "markup first; its code's braces and `else` can't leave their line, and a document has no definitions to space"}
}
