#; Symbol-kind query, ned-authored (tree-sitter-css ships none). A rule is
#; named by its first selector -- the list can run across lines -- and an
#; at-rule block is a container the breadcrumbs nest under.

(rule_set
  (selectors . (_) @name)) @definition.class

(media_statement
  .
  (_) @name) @definition.namespace

(supports_statement
  .
  (_) @name) @definition.namespace

(keyframes_statement
  (keyframes_name) @name) @definition.function

#; Custom properties (`--main: red`) are the stylesheet's variables.
(declaration
  (property_name) @name
  (:match? @name "^--")) @definition.variable
