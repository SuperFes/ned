#; Symbol-kind query, ned-authored: vala-lang/tree-sitter-vala ships
#; highlights and locals but no tags. Names are bare children, in C#'s
#; layout.
(namespace_declaration
  (symbol) @name) @definition.module
(class_declaration
  (unqualified_type) @name) @definition.class
(interface_declaration
  (unqualified_type) @name) @definition.interface
(struct_declaration
  (unqualified_type) @name) @definition.class
(enum_declaration
  (symbol) @name) @definition.enum
(errordomain_declaration
  (symbol) @name) @definition.enum
(delegate_declaration
  (symbol) @name) @definition.type
(method_declaration
  (symbol) @name) @definition.method
(creation_method_declaration
  (symbol) @name) @definition.method
(signal_declaration
  (symbol) @name) @definition.method
(property_declaration
  (symbol) @name) @definition.property
(field_declaration
  (identifier) @name) @definition.field
(constant_declaration
  (identifier) @name) @definition.constant
