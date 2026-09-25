{:name "pem"
 :extensions [".pem" ".crt" ".cer" ".csr"]
 :not-applicable {:indents    "line-oriented records; nothing nests"
                  :locals     "no bindings to scope or rename"
                  :tags       "encoded data, not definitions"
                  :injections "nothing in it is written in another language"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "encoded data; reformatting would break it"
                  :comments   "has no comment syntax"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
