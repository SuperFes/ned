{:name "yaml"
 :extensions [".yaml" ".yml"]
 :line-comment "#"
 :injection-aliases ["yml"]
 # The outline stops two levels down; breadcrumbs follow every level.
 :sticky-scroll-from-folds true
 :not-applicable {:continuation "no operator expressions; multi-line scalars keep their own indentation"
                  :injections   "a key's language depends on the schema, which YAML doesn't name"
                  :imports      "YAML has no includes"
                  :tests        "no test framework runs tests written in it"
                  :signatures   "no user-defined functions with parameter lists"
                  :lsp-root     "no project file of its own; the root falls through to the project's"
                  :format       "data: no functions, control flow or definitions for the formatter's rules to place"}
}
