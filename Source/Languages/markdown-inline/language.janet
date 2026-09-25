# Not a file type: the inline grammar tree-sitter-markdown injects into
# every paragraph, resolved by name from Injection.cpp. Generated with
# upstream's ALL_EXTENSIONS, so wiki links and `#tags` parse, then changed
# where upstream misparses (corpus/spec-checked.txt): an HTML attribute may
# have no value, and an angle-bracket link destination or a quoted attribute
# value outweighs the HTML tags its bytes could also read as. Its highlights
# are upstream's plus ned's additions (strikethrough, wiki links, tags).

{:name "markdown-inline"
 :imprint false

 # Upstream injections.scm files spell it with an underscore (Neovim's
 # parser-name convention).
 :injection-aliases ["markdown_inline"]
}
