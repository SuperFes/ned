#; Symbol-kind query. tree-sitter-grammars/tree-sitter-starlark ships
#; highlights, injections and locals but no tags; the shape is python's
#; (the grammar is derived from tree-sitter-python), so this is python's
#; upstream tags.scm minus the class rule Starlark has no syntax for.
(module
  (expression_statement
    (assignment
      left: (identifier) @name) @definition.constant))

(function_definition
  name: (identifier) @name) @definition.function

(call
  function: [
    (identifier) @name
    (attribute
      attribute: (identifier) @name)
  ]) @reference.call
