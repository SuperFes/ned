#; Symbol-kind query, ned-authored: nushell/tree-sitter-nu ships
#; highlights, injections, folds and indents but no tags. Definitions,
#; aliases, externs and modules name themselves through the hidden
#; _command_name, a bare cmd_identifier child.
(decl_def
  (cmd_identifier) @name) @definition.function
(decl_alias
  (cmd_identifier) @name) @definition.function
(decl_extern
  (cmd_identifier) @name) @definition.function
(decl_module
  (cmd_identifier) @name) @definition.module
