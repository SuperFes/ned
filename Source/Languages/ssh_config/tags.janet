#; Symbol-kind query. tree-sitter-grammars/tree-sitter-ssh-config ships
#; highlights and injections only. Host and Match blocks are the file's
#; structure: each one scopes the parameters under it.
(host_declaration
  (pattern) @name) @definition.module

(match_declaration) @definition.module
