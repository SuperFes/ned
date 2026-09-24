#; Symbol-kind query, ned-authored (tree-sitter-json ships none). Object keys
#; two levels deep -- package.json's scripts and each script -- and no
#; deeper, or the outline would be the file again. A key holding an object
#; is a container the breadcrumbs nest under; any other is a field.

(document
  (object
    (pair
      key: (string (string_content) @name)
      value: (object)) @definition.namespace))

(document
  (object
    (pair
      key: (string (string_content) @name)
      value: [(string) (number) (array) (true) (false) (null)]) @definition.field))

(document
  (object
    (pair
      value: (object
        (pair
          key: (string (string_content) @name)
          value: (object)) @definition.namespace))))

(document
  (object
    (pair
      value: (object
        (pair
          key: (string (string_content) @name)
          value: [(string) (number) (array) (true) (false) (null)]) @definition.field))))
