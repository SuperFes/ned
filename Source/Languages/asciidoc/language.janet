# Prose wraps. Inline markup is the asciidoc-inline grammar, injected into
# every paragraph the way markdown-inline is.
{:name "asciidoc"
 :extensions [".adoc" ".asciidoc" ".asc"]
 :injection-aliases ["adoc"]
 :line-comment "//"
 :wrap-lines true
 # Structure is section depth; delimited blocks are fence lines, not brackets.
 :imprint false
 :import-resolution {:extensions ["adoc"]}
 :not-applicable {:indents    "blocks are delimiter lines, not indentation"
                  :locals     "no bindings to scope or rename"
                  :tests      "no test framework runs tests written in it"
                  :signatures "no user-defined functions with parameter lists"
                  :format     "prose; fill-paragraph is its formatter"
                  :lsp-root   "no project file of its own; the root falls through to the project's"}
}
