# Not a file type: the inline grammar tree-sitter-markdown injects into
# every paragraph, resolved by name from Injection.cpp. Its highlights are
# upstream's plus ned's one addition (strikethrough).

{:name "markdown-inline"

 # Upstream injections.scm files spell it with an underscore (Neovim's
 # parser-name convention).
 :injection-aliases ["markdown_inline"]
}
