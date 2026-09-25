{:name "tcl"
 :extensions [".tcl" ".tk" ".exp"]
 :line-comment "#"
 :import-resolution {:extensions ["tcl"]}
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :injections "nothing in it is written in another language"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
