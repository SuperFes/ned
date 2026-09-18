#; Symbol-kind query, ned-authored: briot/tree-sitter-ada ships highlights
#; and locals but no tags. Packages and subprograms carry their name as a
#; field; a type declaration's name is its bare identifier.
(package_declaration
  name: (_) @name) @definition.module
(package_body
  name: (_) @name) @definition.module
(subprogram_body
  (procedure_specification
    name: (_) @name)) @definition.function
(subprogram_body
  (function_specification
    name: (_) @name)) @definition.function
(subprogram_declaration
  (procedure_specification
    name: (_) @name)) @definition.function
(subprogram_declaration
  (function_specification
    name: (_) @name)) @definition.function
(full_type_declaration
  (identifier) @name) @definition.type
(private_type_declaration
  (identifier) @name) @definition.type
(entry_declaration
  entry_name: (identifier) @name) @definition.method
