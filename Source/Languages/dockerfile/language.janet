{:name "dockerfile"
 :extensions [".dockerfile"]
 :injection-aliases ["docker"]
 :filenames ["Dockerfile" "Containerfile"]
 :line-comment "#"
 :line-continuation "\\"
 :not-applicable {:indents    "instructions are flat; continued lines keep their indent (`:line-continuation`)"
                  :imports    "FROM names images, not files"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :lsp-root   "no project file of its own; the root falls through to the project's"
                  :format     "a flat list of instructions: no bodies, conditions or definitions"}
}
