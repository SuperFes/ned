{:name "gitrebase"
 :filenames ["git-rebase-todo"]
 :line-comment "#"
 :not-applicable {:indents    "line-oriented records; nothing nests"
                  :locals     "no bindings to scope or rename"
                  :injections "nothing in it is written in another language"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "command lines; nothing to lay out"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :tags       "a todo list, not definitions"}
}
