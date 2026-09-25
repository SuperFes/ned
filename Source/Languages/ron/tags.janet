#; Symbol-kind query, ned-authored (tree-sitter-ron ships none). The root
#; value's entries two levels deep, like JSON's keys: one holding a struct or
#; map is a container the breadcrumbs nest under; any other is a field.

(source_file
  (struct
    (struct_entry
      .
      (identifier) @name
      .
      [(struct) (map)]) @definition.namespace))

(source_file
  (struct
    (struct_entry
      .
      (identifier) @name
      .
      [(string) (char) (boolean) (integer) (float) (negative) (array) (tuple) (enum_variant)]) @definition.field))

(source_file
  (struct
    (struct_entry
      (struct
        (struct_entry
          .
          (identifier) @name) @definition.field))))

(source_file
  (map
    (map_entry
      .
      (string) @name) @definition.field))
