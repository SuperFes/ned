#; Symbol-kind query, ned-authored (tree-sitter-jsonnet ships none). A file is
#; one expression: a chain of `local`s ending in the value it evaluates to.
#; The chain's bindings are the outline, then that value's fields, like
#; JSON's.

[
  (document (local_bind (local) (bind function: (id) @name) @definition.function))
  (local_bind (local_bind (local) (bind function: (id) @name) @definition.function))
]

[
  (document (local_bind (local) (bind . (id) @name !function) @definition.variable))
  (local_bind (local_bind (local) (bind . (id) @name !function) @definition.variable))
]

[
  (document (object (member (field function: (fieldname [(id) @name (string (string_content) @name)])) @definition.method)))
  (local_bind (object (member (field function: (fieldname [(id) @name (string (string_content) @name)])) @definition.method)))
]

[
  (document (object (member (field . (fieldname [(id) @name (string (string_content) @name)]) (object) . !function) @definition.namespace)))
  (local_bind (object (member (field . (fieldname [(id) @name (string (string_content) @name)]) (object) . !function) @definition.namespace)))
]

[
  (document (object (member (field . (fieldname [(id) @name (string (string_content) @name)]) [(string) (number) (array) (true) (false) (null) (id) (binary)] . !function) @definition.field)))
  (local_bind (object (member (field . (fieldname [(id) @name (string (string_content) @name)]) [(string) (number) (array) (true) (false) (null) (id) (binary)] . !function) @definition.field)))
]
