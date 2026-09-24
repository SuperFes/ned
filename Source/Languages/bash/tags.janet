#; Symbol-kind query. tree-sitter-bash ships none.

(function_definition
  name: (word) @name) @definition.function

#; A variable assigned inside a function is that function's working state, so
#; only a script-level one names a symbol -- plain, or through
#; export/declare/readonly/local.
(program
  (variable_assignment
    name: (variable_name) @name) @definition.variable)

(program
  (declaration_command
    (variable_assignment
      name: (variable_name) @name) @definition.variable))
