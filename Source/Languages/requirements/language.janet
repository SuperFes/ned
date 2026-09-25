{:name "requirements"
 :extensions [".pip"]
 :filenames ["requirements.txt" "requirements-dev.txt" "requirements_dev.txt"
             "dev-requirements.txt" "test-requirements.txt" "constraints.txt"]
 :line-comment "#"
 :not-applicable {:indents    "line-oriented records; nothing nests"
                  :locals     "no bindings to scope or rename"
                  :injections "nothing in it is written in another language"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "pin lines; nothing to lay out"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :tags       "package pins, not definitions"}
}
