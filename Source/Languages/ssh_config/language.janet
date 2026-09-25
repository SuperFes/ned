{:name "ssh_config"
 :extensions [".ssh_config"]
 :injection-aliases ["ssh-config"]
 :filenames ["ssh_config" ".ssh/config"]
 :line-comment "#"
 :import-resolution {:home-prefix true}
 :not-applicable {:indents    "the grammar's delimited bodies are its whole indent structure"
                  :locals     "no bindings to scope or rename"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "key/value lines; nothing to lay out"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
