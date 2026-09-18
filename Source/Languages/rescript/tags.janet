#; Symbol-kind query, ned-authored: rescript-lang/tree-sitter-rescript
#; ships highlights, injections and locals but no tags.
(let_declaration
  (let_binding
    pattern: (value_identifier) @name)) @definition.var
(let_declaration
  (let_binding
    pattern: (value_identifier) @name
    body: (function))) @definition.function
(type_declaration
  (type_binding
    name: (_) @name)) @definition.type
(module_declaration
  (module_binding
    name: (_) @name)) @definition.module
(external_declaration
  (value_identifier) @name) @definition.function
(exception_declaration
  (variant_identifier) @name) @definition.type
