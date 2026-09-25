{:name "kdl"
 :extensions [".kdl"]
 :line-comment "//"
 # The outline stops two levels down; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
