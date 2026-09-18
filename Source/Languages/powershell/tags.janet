#; Symbol-kind query, ned-authored: airbus-cert/tree-sitter-powershell
#; ships highlights and folds but no tags.
(function_statement
  (function_name) @name) @definition.function
(class_statement
  (simple_name) @name) @definition.class
(enum_statement
  (simple_name) @name) @definition.enum
(class_method_definition
  (simple_name) @name) @definition.method
