# Prose wraps. Inline markup is the asciidoc-inline grammar, injected into
# every paragraph the way markdown-inline is.
{:name "asciidoc"
 :extensions [".adoc" ".asciidoc" ".asc"]
 :line-comment "//"
 :wrap-lines true
 # Structure is section depth; delimited blocks are fence lines, not brackets.
 :imprint false
}
