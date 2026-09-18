#; Symbol-kind query, ned-authored: tree-sitter/tree-sitter-julia ships
#; highlights, injections and locals but no tags. A definition's name is
#; the head of its signature or type head.
(module_definition
  name: (identifier) @name) @definition.module
(function_definition
  (signature
    (identifier) @name)) @definition.function
(function_definition
  (signature
    (call_expression
      (identifier) @name))) @definition.function
(macro_definition
  (signature
    (call_expression
      (identifier) @name))) @definition.function
(struct_definition
  (type_head
    (identifier) @name)) @definition.class
(abstract_definition
  (type_head
    (identifier) @name)) @definition.type
