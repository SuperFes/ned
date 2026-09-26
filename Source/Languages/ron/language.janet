# grammar.janet adds the `#![enable(...)]` extension header upstream's
# grammar lacks.

{:name "ron"
 :extensions [".ron"]
 :line-comment "//"
 :queries {:highlights ["ron/highlights.janet"
                        "ron/upstream/highlights.janet"]}
 # The outline stops two levels down; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :format     "data: no functions, control flow or definitions for the formatter's rules to place"}
}
