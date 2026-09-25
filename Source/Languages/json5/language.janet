{:name "json5"
 :extensions [".json5"]
 :line-comment "//"
 # The outline stops two levels down; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :locals     "no bindings to scope or rename"
                  :injections "nothing in it is written in another language"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
