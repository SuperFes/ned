#; Symbol-kind query, ned-authored: murtaza64/tree-sitter-groovy ships
#; highlights, injections and locals but no tags. Java's declaration
#; shape.
(class_definition
  name: (identifier) @name) @definition.class
(function_definition
  function: (identifier) @name) @definition.function
(function_declaration
  function: (identifier) @name) @definition.function
