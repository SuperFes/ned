#; Symbol-kind query, ned-authored (tree-sitter-json5 ships none). Object keys
#; two levels deep, like JSON's: a key holding an object is a container the
#; breadcrumbs nest under; any other is a field. A quoted key is named
#; without its quotes.

(file
  (object
    (member
      name: (identifier) @name
      value: (object)) @definition.namespace))

(file
  (object
    (member
      name: (string) @name
      value: (object)
      (:offset! @name 0 1 0 -1)) @definition.namespace))

(file
  (object
    (member
      name: (identifier) @name
      value: [(string) (number) (array) (true) (false) (null)]) @definition.field))

(file
  (object
    (member
      name: (string) @name
      value: [(string) (number) (array) (true) (false) (null)]
      (:offset! @name 0 1 0 -1)) @definition.field))

(file
  (object
    (member
      value: (object
        (member
          name: (identifier) @name) @definition.field))))

(file
  (object
    (member
      value: (object
        (member
          name: (string) @name
          (:offset! @name 0 1 0 -1)) @definition.field))))
