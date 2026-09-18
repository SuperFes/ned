#; Symbol-kind query, ned-authored: WhatsApp/tree-sitter-erlang ships
#; highlights only. A function's name is its first clause's `name:`; the
#; module attribute, records and types are the file's other names.
(fun_decl
  clause: (function_clause
    name: (_) @name)) @definition.function
(module_attribute
  name: (_) @name) @definition.module
(record_decl
  name: (_) @name) @definition.class
(type_alias
  (type_name
    name: (_) @name)) @definition.type
