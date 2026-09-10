;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows.
;;
;; Python's scoping is FUNCTION-level, not block-level, so a block/for/while/
;; with/try body is deliberately not a scope: capturing one would split a
;; single function-local binding into several and rename only part of it.
;; The scopes are the four things that really do introduce a namespace -- a
;; function, a lambda, a class body, and a comprehension (whose loop
;; variable has not leaked since Python 3).
;;
;; A function's or class's own name is not captured as a definition, per rule
;; 3: it sits inside the very node that is its scope. Module-level
;; assignments are captured, since they sit outside every scope and reading
;; as file-level is exactly right for them.
;;
;; One Python-specific consequence of the resolver's position rule
;; (Editor/LocalScopes.h): a use that textually precedes its own function-
;; local assignment binds to that local in real Python (and raises
;; UnboundLocalError at run time), but resolves outward here. That is
;; reported rather than silent -- LocalBinding::usedBeforeDefinition -- and
;; rename-symbol declines instead of renaming a partial occurrence set.

;; Scopes
[
  (function_definition)
  (lambda)
  (class_definition)
  (list_comprehension)
  (set_comprehension)
  (dictionary_comprehension)
  (generator_expression)
] @local.scope

;; Parameters, in every form the grammar spells separately.
(parameters
  (identifier) @local.definition.parameter)
(lambda_parameters
  (identifier) @local.definition.parameter)
(default_parameter
  name: (identifier) @local.definition.parameter)
(typed_parameter
  (identifier) @local.definition.parameter)
(typed_default_parameter
  name: (identifier) @local.definition.parameter)
(list_splat_pattern
  (identifier) @local.definition.parameter)
(dictionary_splat_pattern
  (identifier) @local.definition.parameter)

;; Assignment targets, including tuple/list unpacking. An augmented
;; assignment (`x += 1`) is deliberately NOT a definition: it is a read as
;; much as a write, and treating it as a binding site would make a module
;; global that a function mutates look like two separate bindings.
(assignment
  left: (identifier) @local.definition.var)
(assignment
  left: (pattern_list
    (identifier) @local.definition.var))
(assignment
  left: (tuple_pattern
    (identifier) @local.definition.var))
(assignment
  left: (list_pattern
    (identifier) @local.definition.var))

;; Loop variables, `with ... as`, `except ... as`, and the walrus operator.
(for_statement
  left: (identifier) @local.definition.var)
(for_statement
  left: (pattern_list
    (identifier) @local.definition.var))
(for_statement
  left: (tuple_pattern
    (identifier) @local.definition.var))
(for_in_clause
  left: (identifier) @local.definition.var)
(for_in_clause
  left: (pattern_list
    (identifier) @local.definition.var))
(for_in_clause
  left: (tuple_pattern
    (identifier) @local.definition.var))
(as_pattern_target
  (identifier) @local.definition.var)
(named_expression
  name: (identifier) @local.definition.var)

;; References. The attribute half of `obj.attr` and the name half of a
;; `f(key=value)` keyword argument are identifiers in this grammar but are
;; NOT variable references -- renaming a local that happens to share a
;; spelling with a method or a keyword argument would otherwise rewrite the
;; call. The object half genuinely is a reference, so it is added back
;; explicitly.
(attribute
  object: (identifier) @local.reference)
((identifier) @local.reference
  (#not-has-parent? @local.reference "attribute")
  (#not-has-parent? @local.reference "keyword_argument"))
