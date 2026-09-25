# `import std/strutils, ./foo, bar` -- each module is a path; `pkg/[a, b]`
# groups and `x as y` renames name no one file.
(import_statement
  (expression_list
    [(identifier) (infix_expression) (prefix_expression)] @import.target
    (:not-match? @import.target "[\\[ ]"))) @import.statement
(include_statement (expression_list (identifier) @import.target)) @import.statement
(import_from_statement module: [(identifier) (infix_expression) (prefix_expression)] @import.target) @import.statement
