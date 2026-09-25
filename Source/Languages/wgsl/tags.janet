#; Symbol-kind query, ned-authored (tree-sitter-wgsl-bevy ships none).

(function_declaration
  name: (identifier) @name) @definition.function

(struct_declaration
  name: (identifier) @name) @definition.struct

(struct_member
  (variable_identifier_declaration
    .
    (identifier) @name)) @definition.field

(type_alias_declaration
  .
  (identifier) @name) @definition.type

(global_constant_declaration
  [(identifier) @name
   (variable_identifier_declaration . (identifier) @name)]) @definition.constant

(global_variable_declaration
  (variable_declaration
    [(identifier) @name
     (variable_identifier_declaration . (identifier) @name)])) @definition.variable
