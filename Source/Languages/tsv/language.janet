# Shares tree-sitter-csv's corpus with csv, routed by :language(tsv).
{:name "tsv"
 :extensions [".tsv" ".tab"]
 :not-applicable {:indents    "line-oriented records; nothing nests"
                  :locals     "no bindings to scope or rename"
                  :tags       "records, not definitions"
                  :injections "nothing in it is written in another language"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "records are data; reformatting would change values"
                  :comments   "has no comment syntax"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
