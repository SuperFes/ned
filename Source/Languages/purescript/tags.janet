#; Symbol-kind query, ned-authored: postsolar/tree-sitter-purescript
#; ships highlights, injections and locals but no tags. The declaration
#; rules are aliased in the tree (decl_data is `data`, decl_newtype
#; `newtype`, decl_type `type_alias`, decl_foreign_import
#; `foreign_import`), each naming itself in `name:`.
(data
  name: (type) @name) @definition.type
(newtype
  name: (type) @name) @definition.type
(type_alias
  name: (type) @name) @definition.type
(kind_declaration
  name: (type) @name) @definition.type
(class_declaration
  (class_head
    (class_name) @name)) @definition.interface
(function
  name: (variable) @name) @definition.function
(signature
  name: (variable) @name) @definition.function
(foreign_import
  name: (variable) @name) @definition.function
