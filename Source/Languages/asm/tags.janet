#; Symbol-kind query, ned-authored: RubixDev/tree-sitter-asm ships
#; highlights and injections but no tags. Labels are the outline.
(label
  (ident) @name) @definition.function
(label
  name: (word) @name) @definition.function
(const
  name: (word) @name) @definition.constant
