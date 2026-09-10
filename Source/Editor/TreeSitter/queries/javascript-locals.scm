;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows.
;;
;; This grammar makes the reference rule at the bottom unusually safe: a
;; property in `obj.local` is a property_identifier and a key in
;; `{ local: 2 }` a shorthand_property_identifier, both distinct node types
;; from identifier, so neither is ever mistaken for a variable read. Python
;; and Java need an explicit exclusion for the same situation; here the
;; grammar already draws the line.
;;
;; `var` hoisting is not modelled -- a `var` is treated as bound where it is
;; written, like `let`. The resolver's own position rule then resolves a use
;; that precedes a hoisted `var` outward and reports the miss rather than
;; renaming a partial set (Editor/LocalScopes.h).

;; Scopes. statement_block covers `let`/`const` block scoping; the function
;; forms are listed separately because a parameter list sits outside the
;; body block.
[
  (statement_block)
  (function_declaration)
  (function_expression)
  (generator_function)
  (generator_function_declaration)
  (arrow_function)
  (method_definition)
  (class_static_block)
  (for_statement)
  (for_in_statement)
  (catch_clause)
] @local.scope

;; Parameters
(formal_parameters
  (identifier) @local.definition.parameter)
(arrow_function
  parameter: (identifier) @local.definition.parameter)
(catch_clause
  parameter: (identifier) @local.definition.parameter)
(assignment_pattern
  left: (identifier) @local.definition.parameter)

;; Variables
(variable_declarator
  name: (identifier) @local.definition.var)

;; Destructuring, object and array, including renamed and rest bindings.
;; `{ first }` binds `first`; `{ second: alias }` binds `alias`, never
;; `second` -- that half is the property being read, not a new name.
(object_pattern
  (shorthand_property_identifier_pattern) @local.definition.var)
(pair_pattern
  value: (identifier) @local.definition.var)
(array_pattern
  (identifier) @local.definition.var)
(rest_pattern
  (identifier) @local.definition.var)

;; for...of / for...in loop variables
(for_in_statement
  left: (identifier) @local.definition.var)

;; References
(identifier) @local.reference
