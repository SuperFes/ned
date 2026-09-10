;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows.
;;
;; PHP makes the reference rule exact rather than approximate: a variable is
;; always a variable_name wrapping a name, while a function name, a method
;; name, a class name and the member half of `$obj->local` are all BARE name
;; nodes. Capturing only the wrapped form therefore picks up variables and
;; nothing else -- no exclusion predicates needed, unlike Java/C#/Python.
;; The capture lands on the inner name, not on variable_name, so a rename
;; rewrites `local` and leaves the `$` alone.
;;
;; PHP has no block scope: a variable assigned inside an `if` or a `foreach`
;; body is visible for the rest of the function. So a compound_statement is
;; deliberately NOT a scope here -- only the four things that really do
;; introduce one.
;;
;; A closure's `use ($local)` clause is deliberately not a definition. Left
;; as a reference it resolves outward to the enclosing function's own
;; binding, which is exactly right: renaming that local rewrites the use
;; clause and the closure body along with everything else.

;; Scopes
[
  (function_definition)
  (method_declaration)
  (anonymous_function)
  (arrow_function)
] @local.scope

;; Parameters. A promoted constructor parameter (`private $x`) declares a
;; property rather than a local and is left out, the same call
;; java-locals.scm makes about fields.
(simple_parameter
  (variable_name
    (name) @local.definition.parameter))
(variadic_parameter
  (variable_name
    (name) @local.definition.parameter))
(catch_clause
  (variable_name
    (name) @local.definition.parameter))

;; Assignment targets, including list destructuring.
(assignment_expression
  left: (variable_name
    (name) @local.definition.var))
(list_literal
  (variable_name
    (name) @local.definition.var))

;; The foreach value, with or without a key. The second rule's anchor is
;; what keeps it off the subject being iterated -- `foreach ($items as $item)`
;; spells subject and value as two sibling variable_names.
(foreach_statement
  (pair
    (variable_name
      (name) @local.definition.var)))
(foreach_statement
  (variable_name) . (variable_name
    (name) @local.definition.var))

;; References
(variable_name
  (name) @local.reference)
