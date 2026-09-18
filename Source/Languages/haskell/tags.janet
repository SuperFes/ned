#; Symbol-kind query, ned-authored: tree-sitter/tree-sitter-haskell ships
#; highlights, injections and locals but no tags. Type-level declarations
#; name themselves through the hidden _type_head (a bare `name` child);
#; a function binding's name is its `name:` field, a signature's too.
(data_type
  (name) @name) @definition.type
(newtype
  (name) @name) @definition.type
(type_synomym
  (name) @name) @definition.type
(class
  (name) @name) @definition.interface
(type_family
  (name) @name) @definition.type
(function
  name: (_) @name) @definition.function
(signature
  name: (_) @name) @definition.function
(bind
  name: (_) @name) @definition.var
