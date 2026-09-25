# Dot-sourcing: `. ./lib.ps1`.
(command
  (command_invokation_operator) @_operator
  command_name: (command_name_expr (command_name) @import.target)
  (:eq? @_operator ".")) @import.statement
(command
  command_name: (command_name) @_command
  command_elements: (command_elements . (command_argument_sep) . (generic_token) @import.target)
  (:any-of? @_command "Import-Module" "import-module" "ipmo")) @import.statement
