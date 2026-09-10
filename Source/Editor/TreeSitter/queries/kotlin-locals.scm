;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows, and java-locals.scm for why a class body is not
;; a scope -- Kotlin takes the same answer, so a `val` declared in a class
;; body (or a constructor's `private val` class_parameter, which really
;; declares a property) is left uncaptured and reads as not-local.
;;
;; This grammar spells one node, variable_declaration, for every binding
;; site that isn't a function parameter -- a `val`/`var`, a for-loop
;; variable, and a lambda parameter all go through it -- so the definition
;; rules here are unusually short.
;;
;; `obj.local` puts its member half under a navigation_suffix, which is the
;; one exclusion the reference rule needs; the object half is a plain
;; simple_identifier outside that suffix and so is kept without having to be
;; re-added, unlike in Java or C#.

;; Scopes. function_declaration and lambda_literal each cover their own
;; parameter list; function_body is listed too so a local declared in a body
;; does not escape into a single-expression function's own scope.
[
  (function_declaration)
  (anonymous_function)
  (function_body)
  (lambda_literal)
  (for_statement)
  (while_statement)
  (do_while_statement)
  (if_expression)
  (when_entry)
  (control_structure_body)
] @local.scope

;; Parameters. class_parameter is deliberately absent -- see the header.
(parameter
  (simple_identifier) @local.definition.parameter)

;; Every other binding site: `val`/`var`, the for-loop variable, and a
;; lambda's own parameters all wrap their name in a variable_declaration.
(variable_declaration
  (simple_identifier) @local.definition.var)

;; References
((simple_identifier) @local.reference
  (#not-has-parent? @local.reference "navigation_suffix"))
