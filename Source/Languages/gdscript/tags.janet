#; Symbol-kind query, ned-authored: PrestonKnopp/tree-sitter-gdscript
#; ships no queries.
(class_definition
  name: (name) @name) @definition.class
(class_name_statement
  name: (name) @name) @definition.class
(function_definition
  name: (name) @name) @definition.function
(constructor_definition) @definition.method
(signal_statement
  name: (name) @name) @definition.function
(const_statement
  name: (name) @name) @definition.constant
(enum_definition
  name: (name) @name) @definition.enum
