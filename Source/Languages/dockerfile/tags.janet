#; Symbol-kind query, ned-authored (tree-sitter-dockerfile ships none). Each
#; build stage, named by its `AS` alias, or by its image when it has none,
#; plus the build arguments and environment it declares.

(from_instruction
  as: (image_alias) @name) @definition.namespace

(from_instruction
  (image_spec
    (image_name) @name)
  !as) @definition.namespace

(arg_instruction
  name: (_) @name) @definition.variable

(env_pair
  name: (_) @name) @definition.variable
