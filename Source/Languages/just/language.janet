{:name "just"
 :extensions [".just"]
 :injection-aliases ["justfile"]
 :filenames ["justfile" "Justfile" "JUSTFILE" ".justfile" ".Justfile" ".JUSTFILE"]
 :line-comment "#"
 :import-resolution {:extensions ["just"] :index-basenames ["mod"]}
 :not-applicable {:indents  "the grammar's delimited bodies are its whole indent structure"
                  :tests    "no test framework runs tests written in it"
                  :lsp-root "no project file of its own; the root falls through to the project's"}
}
