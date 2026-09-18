#; Symbol-kind query. glehmann/tree-sitter-earthfile ships highlights and
#; injections but no tags. A target is what `earthly +name` invokes, the
#; callable unit of an Earthfile.
(target
  name: (identifier) @name) @definition.function
