{:name "diff"
 :extensions [".diff" ".patch"]
 :injection-aliases ["udiff" "patch"]
 :not-applicable {:indents    "line-oriented records; nothing nests"
                  :locals     "no bindings to scope or rename"
                  :injections "hunks are shown as text, not re-parsed"
                  :imports    "file headers are navigation, not imports"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "a patch is data; reformatting would break it"
                  :comments   "has no comment syntax"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
