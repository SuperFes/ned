;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows (scope covers its own parameter list, the file
;; root is never a scope, a name whose binding isn't local isn't captured).
;;
;; C++ adds four scope-introducing forms C has no equivalent of: a lambda
;; (its capture list and parameters bind inside its own body), a range-for
;; (the loop variable), a catch clause (the exception parameter), and an
;; if/switch initializer. A method's own name is a field_identifier rather
;; than an identifier in this grammar, so it never reaches the reference
;; rule at the bottom -- the same reason struct members don't.

;; Scopes
[
  (function_definition)
  (compound_statement)
  (for_statement)
  (for_range_loop)
  (lambda_expression)
  (catch_clause)
] @local.scope

;; Parameters -- plain, pointer, reference, array.
(parameter_declaration
  declarator: (identifier) @local.definition.parameter)
(parameter_declaration
  declarator: (pointer_declarator
    declarator: (identifier) @local.definition.parameter))
(parameter_declaration
  declarator: (reference_declarator
    (identifier) @local.definition.parameter))
(parameter_declaration
  declarator: (array_declarator
    declarator: (identifier) @local.definition.parameter))
(optional_parameter_declaration
  declarator: (identifier) @local.definition.parameter)
(optional_parameter_declaration
  declarator: (reference_declarator
    (identifier) @local.definition.parameter))

;; Variables
(declaration
  declarator: (identifier) @local.definition.var)
(declaration
  declarator: (init_declarator
    declarator: (identifier) @local.definition.var))
(declaration
  declarator: (pointer_declarator
    declarator: (identifier) @local.definition.var))
(declaration
  declarator: (init_declarator
    declarator: (pointer_declarator
      declarator: (identifier) @local.definition.var)))
(declaration
  declarator: (reference_declarator
    (identifier) @local.definition.var))
(declaration
  declarator: (init_declarator
    declarator: (reference_declarator
      (identifier) @local.definition.var)))
(declaration
  declarator: (array_declarator
    declarator: (identifier) @local.definition.var))
(declaration
  declarator: (init_declarator
    declarator: (array_declarator
      declarator: (identifier) @local.definition.var)))

;; Structured bindings -- `auto [first, second] = pair;` binds both names.
(structured_binding_declarator
  (identifier) @local.definition.var)

;; The range-for loop variable.
(for_range_loop
  declarator: (identifier) @local.definition.var)
(for_range_loop
  declarator: (reference_declarator
    (identifier) @local.definition.var))
(for_range_loop
  declarator: (pointer_declarator
    declarator: (identifier) @local.definition.var))
(for_range_loop
  declarator: (structured_binding_declarator
    (identifier) @local.definition.var))

;; References
(identifier) @local.reference
