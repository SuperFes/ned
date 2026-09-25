# Not a file type: the inline grammar tree-sitter-asciidoc injects into
# paragraphs, table cells and macro targets, resolved by name from
# Injection.cpp.
{:name "asciidoc-inline"
 :imprint false
 # Upstream injections.scm spells it with an underscore.
 :injection-aliases ["asciidoc_inline"]
 :not-applicable {:indents    "an inline sub-language; asciidoc owns this"
                  :locals     "an inline sub-language; asciidoc owns this"
                  :tags       "an inline sub-language; asciidoc owns this"
                  :imports    "an inline sub-language; asciidoc owns this"
                  :tests      "an inline sub-language; asciidoc owns this"
                  :signatures "an inline sub-language; asciidoc owns this"
                  :format     "an inline sub-language; asciidoc owns this"
                  :comments   "an inline sub-language; asciidoc owns this"
                  :lsp-root   "an inline sub-language; asciidoc owns this"}
}
