#; Symbol-kind query, ned-authored: tree-sitter-grammars/tree-sitter-thrift
#; ships locals but no tags.
(struct_definition
  (identifier) @name) @definition.class
(union_definition
  (identifier) @name) @definition.class
(exception_definition
  (identifier) @name) @definition.class
(enum_definition
  (identifier) @name) @definition.enum
(service_definition
  (identifier) @name) @definition.interface
(function_definition
  (identifier) @name) @definition.method
(typedef_identifier) @name
(const_definition
  (identifier) @name) @definition.constant
(namespace_declaration
  (namespace_uri) @name) @definition.module
