# The file is ".env" and its per-environment variants (".env.local",
# ".env.production"); the variants carry ".env" as a basename prefix, not
# an extension, so only the exact spellings are claimed.
{:name "dotenv"
 :extensions [".env"]
 :injection-aliases ["env"]
 :filenames [".env" ".env.local" ".env.development" ".env.production" ".env.test" ".env.example"]
 :line-comment "#"
 :not-applicable {:indents    "line-oriented records; nothing nests"
                  :locals     "no bindings to scope or rename"
                  :injections "nothing in it is written in another language"
                  :imports    "nothing in it names another file"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "key/value lines; nothing to lay out"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
