(command
  head: (cmd_identifier) @_command
  arg_str: (val_string) @import.target
  (:any-of? @_command "source" "source-env")) @import.statement
(decl_use module: (_) @import.target) @import.statement
