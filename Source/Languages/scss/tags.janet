#; Symbol-kind query, ned-authored (tree-sitter-scss ships none). CSS's rules
#; and at-rules, plus what SCSS adds: mixins, functions and file-level
#; `$variables` (one set inside a rule is that rule's own).

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

(mixin_statement
  .
  (identifier) @name) @definition.function

(function_statement
  .
  (identifier) @name) @definition.function

(stylesheet
  (declaration
    (property_name) @name
    (:match? @name "^\\$")) @definition.variable)

(declaration
  (property_name) @name
  (:match? @name "^--")) @definition.variable
