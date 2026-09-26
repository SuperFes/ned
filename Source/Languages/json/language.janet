# No :line-comment -- JSON has no comment syntax at all, real or otherwise;
# toggle-line-comment correctly reports nothing configured rather than
# inserting something that would make the file invalid JSON.

{:name "json"
 :extensions [".json"]
 :injection-aliases ["jsonc"]
 # The outline stops two levels down; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :locals     "no bindings to scope or rename"
                  :injections "nothing in it is written in another language"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :comments   "has no comment syntax"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :format     "data: no functions, control flow or definitions for the formatter's rules to place"}
}
