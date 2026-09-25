{:name "rst"
 :extensions [".rst"]
 :injection-aliases ["restructuredtext"]
 :line-comment ".."
 :wrap-lines true
 # Section titles are adornment lines, not brackets; structure is depth.
 :imprint false
 :import-resolution {:extensions ["rst"]}
 :not-applicable {:indents    "indentation is syntax; a reindent would change the document"
                  :locals     "no bindings to scope or rename"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "prose; fill-paragraph is its formatter"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
