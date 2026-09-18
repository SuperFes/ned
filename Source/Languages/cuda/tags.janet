#; Symbol-kind query, ned-authored: C's declarator chain for functions,
#; structs by name (this grammar is tree-sitter-c derived).
(function_definition
  declarator: (function_declarator
    declarator: (identifier) @name)) @definition.function
(function_definition
  declarator: (pointer_declarator
    declarator: (function_declarator
      declarator: (identifier) @name))) @definition.function
(declaration
  declarator: (function_declarator
    declarator: (identifier) @name)) @definition.function
(struct_specifier
  name: (type_identifier) @name
  body: (_)) @definition.class
(type_definition
  declarator: (type_identifier) @name) @definition.type
