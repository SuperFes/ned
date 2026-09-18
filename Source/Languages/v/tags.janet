#; Symbol-kind query, ned-authored: vlang/v-analyzer's grammar ships
#; highlights only. Go's layout: fn, struct, enum, interface, type, const.
(function_declaration
  name: (_) @name) @definition.function
(static_method_declaration
  name: (_) @name) @definition.method
(struct_declaration
  name: (identifier) @name) @definition.class
(enum_declaration
  name: (identifier) @name) @definition.enum
(interface_declaration
  name: (identifier) @name) @definition.interface
(type_declaration
  name: (identifier) @name) @definition.type
(const_definition
  name: (identifier) @name) @definition.constant
(global_var_definition
  name: (identifier) @name) @definition.var
(module_clause
  (identifier) @name) @definition.module
