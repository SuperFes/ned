#; Symbol-kind query, ned-authored (tree-sitter-fsharp ships none). A `let`
#; names a symbol only at module level -- inside a function body it is a
#; local -- so each is matched under the four containers a module-level
#; binding can sit in.

(namespace
  name: (long_identifier) @name) @definition.module

(named_module
  name: (long_identifier) @name) @definition.module

(module_defn
  .
  (identifier) @name) @definition.module

(module_abbrev
  .
  (identifier) @name) @definition.module

[
  (file (declaration_expression
    (function_or_value_defn
      (function_declaration_left (identifier) @name)) @definition.function))
  (namespace (declaration_expression
    (function_or_value_defn
      (function_declaration_left (identifier) @name)) @definition.function))
  (named_module (declaration_expression
    (function_or_value_defn
      (function_declaration_left (identifier) @name)) @definition.function))
  (module_defn (declaration_expression
    (function_or_value_defn
      (function_declaration_left (identifier) @name)) @definition.function))
]

[
  (file (declaration_expression
    (function_or_value_defn
      (value_declaration_left
        (identifier_pattern (long_identifier_or_op (identifier) @name)))) @definition.variable))
  (namespace (declaration_expression
    (function_or_value_defn
      (value_declaration_left
        (identifier_pattern (long_identifier_or_op (identifier) @name)))) @definition.variable))
  (named_module (declaration_expression
    (function_or_value_defn
      (value_declaration_left
        (identifier_pattern (long_identifier_or_op (identifier) @name)))) @definition.variable))
  (module_defn (declaration_expression
    (function_or_value_defn
      (value_declaration_left
        (identifier_pattern (long_identifier_or_op (identifier) @name)))) @definition.variable))
]

(record_type_defn
  (type_name type_name: (_) @name)) @definition.struct

(union_type_defn
  (type_name type_name: (_) @name)) @definition.enum

(enum_type_defn
  (type_name type_name: (_) @name)) @definition.enum

(interface_type_defn
  (type_name type_name: (_) @name)) @definition.interface

[
  (anon_type_defn (type_name type_name: (_) @name))
  (type_extension (type_name type_name: (_) @name))
] @definition.class

[
  (type_abbrev_defn (type_name type_name: (_) @name))
  (delegate_type_defn (type_name type_name: (_) @name))
] @definition.type

(exception_definition
  exception_name: (long_identifier) @name) @definition.class

(record_field
  (identifier) @name) @definition.field

(union_type_case
  .
  (identifier) @name) @definition.enum_member

(enum_type_case
  .
  (identifier) @name) @definition.enum_member

#; `member this.Next() = ...` names the method after the dot; a static
#; member has no instance.
(member_defn
  (method_or_prop_defn
    (property_or_ident method: (identifier) @name)
    args: (_))) @definition.method

(member_defn
  (method_or_prop_defn
    (property_or_ident . (identifier) @name .)
    args: (_))) @definition.method

(member_defn
  (method_or_prop_defn
    (property_or_ident method: (identifier) @name)
    !args)) @definition.property

(member_defn
  (method_or_prop_defn
    (property_or_ident . (identifier) @name .)
    !args)) @definition.property

(member_defn
  "val"
  (property_or_ident (identifier) @name)) @definition.property

(member_defn
  (member_signature . (identifier) @name)) @definition.method
