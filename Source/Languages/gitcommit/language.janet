{:name "gitcommit"
 :filenames ["COMMIT_EDITMSG" "MERGE_MSG" "TAG_EDITMSG"]
 :line-comment "#"
 :not-applicable {:indents    "prose"
                  :locals     "no bindings to scope or rename"
                  :injections "the verbose diff is shown as text"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "prose; fill-paragraph is its formatter"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :tags       "prose, not definitions"}
}
