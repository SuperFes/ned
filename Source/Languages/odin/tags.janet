#; Symbol-kind query, ned-authored: tree-sitter-grammars/tree-sitter-odin
#; ships highlights, injections and locals but no tags. A declaration's
#; name is the expression before its `::`.
(procedure_declaration
  (expression
    (identifier) @name)) @definition.function
(overloaded_procedure_declaration
  (expression
    (identifier) @name)) @definition.function
(struct_declaration
  (expression
    (identifier) @name)) @definition.class
(enum_declaration
  (expression
    (identifier) @name)) @definition.enum
(union_declaration
  (expression
    (identifier) @name)) @definition.type
(bit_field_declaration
  (expression
    (identifier) @name)) @definition.class
(const_declaration
  (expression
    (identifier) @name)) @definition.constant
(package_declaration
  (identifier) @name) @definition.module
