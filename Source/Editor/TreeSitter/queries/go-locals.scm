;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows.
;;
;; Go is the easiest of the bundled languages here: `obj.field` is a
;; field_identifier and a struct literal key a field_identifier too, so the
;; reference rule needs no exclusions at all. Every binding form funnels
;; through one of four nodes -- a parameter, a `var` declaration, a `:=`
;; short declaration, or a range clause.
;;
;; A function's own name is not captured, per rule 3: it sits inside the
;; function_declaration node that is its scope. A package-level `var` does
;; get captured, since it sits outside every scope and reading as file-level
;; is right for it -- an exported one is visible to the whole package, which
;; is a language server's business, not this file's.

;; Scopes. The function forms are listed alongside block because a parameter
;; list sits outside the body block; if/for/switch are scopes in Go because
;; each can carry its own `:=` initializer.
[
  (function_declaration)
  (method_declaration)
  (func_literal)
  (block)
  (if_statement)
  (for_statement)
  (expression_switch_statement)
  (type_switch_statement)
  (select_statement)
] @local.scope

;; Parameters, including a variadic one and a method's receiver.
(parameter_declaration
  name: (identifier) @local.definition.parameter)
(variadic_parameter_declaration
  name: (identifier) @local.definition.parameter)

;; Declarations -- `var x T`, `const x = ...`, `x := ...` (which binds every
;; name on its left, so `v, err := call()` binds both).
(var_spec
  name: (identifier) @local.definition.var)
(const_spec
  name: (identifier) @local.definition.var)
(short_var_declaration
  left: (expression_list
    (identifier) @local.definition.var))

;; `for i, item := range items` and the type-switch binding.
(range_clause
  left: (expression_list
    (identifier) @local.definition.var))
(type_switch_statement
  (expression_list
    (identifier) @local.definition.var))

;; References
(identifier) @local.reference
