{:name "lua"
 :extensions [".lua"]
 :line-comment "--"

 # lua-language-server's own root convention (nvim-lspconfig's lua_ls
 # root_dir walks for the same two names).
 :lsp-root-markers [".luarc.json" ".luarc.jsonc"]
 :snippets
 {
   "fn"
   "function ${1:name}(${2:args})\n    $0\nend"
   "local"
   "local ${1:name} = $0"
   "if"
   "if ${1:condition} then\n    $0\nend"
   "for"
   "for ${1:i} = ${2:1}, ${3:n} do\n    $0\nend"
   "forin"
   "for ${1:k}, ${2:v} in pairs(${3:t}) do\n    $0\nend"
   "while"
   "while ${1:condition} do\n    $0\nend"}
}
