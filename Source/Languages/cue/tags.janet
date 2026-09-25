#; Symbol-kind query, ned-authored (tree-sitter-cue ships none). Fields two
#; levels deep, like JSON's keys: one holding a struct is a container the
#; breadcrumbs nest under; any other is a field. A `#Definition` is a schema.

(package_clause
  (package_identifier) @name) @definition.module

(source_file
  (field
    .
    (label (identifier) @name)
    (:match? @name "^_?#")) @definition.type)

(source_file
  (field
    .
    (label (identifier) @name)
    .
    (value (struct_lit))
    (:not-match? @name "^_?#")) @definition.namespace)

(source_file
  (field
    .
    (label (identifier) @name)
    .
    (value [(parenthesized_expression) (selector_expression) (index_expression) (call_expression)
            (identifier) (unary_expression) (binary_expression) (list_lit) (string) (number) (float)
            (si_unit) (boolean) (null) (top) (bottom) (primitive_type)])
    (:not-match? @name "^_?#")) @definition.field)

(source_file
  (field
    (value
      (struct_lit
        (field
          .
          (label (identifier) @name)) @definition.field))))

(source_file
  (let_clause
    .
    (identifier) @name) @definition.variable)
