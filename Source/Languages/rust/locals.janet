#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; Rust patterns bind and destructure with the same node shapes used to NAME
#; a variant or a struct: in `Some(value)` and `Pattern::A(inner)` the
#; binding and the constructor are both plain identifiers, distinguished by
#; nothing structural. They are told apart here by Rust's own casing
#; convention (`#match? "^[_a-z]"`), which the compiler enforces with
#; non_snake_case/non_camel_case_types warnings -- a heuristic, but a
#; heuristic the language itself upholds, and the alternative is capturing
#; every enum variant in every match arm as a local binding.
#;
#; `obj.field` is a field_identifier and so never reaches the reference rule.
#; A path segment is an identifier, though (`Pattern::A` is two of them), and
#; a path segment is never a local -- so scoped_identifier's children are
#; excluded explicitly, the same shape python-locals.scm uses for attribute
#; access.

#; Scopes. function_item and closure_expression are listed alongside block
#; because a parameter list sits outside the body block; the `if`/`while`
#; forms are there for `if let`/`while let`, whose pattern binds for the
#; body that follows it.
[
  (function_item)
  (block)
  (closure_expression)
  (match_arm)
  (for_expression)
  (loop_expression)
  (while_expression)
  (if_expression)
] @local.scope

#; Parameters, including a closure's.
(parameter
  pattern: (identifier) @local.definition.parameter)
(parameter
  pattern: (ref_pattern
    (identifier) @local.definition.parameter))
(parameter
  pattern: (mut_pattern
    (identifier) @local.definition.parameter))

#; `let` bindings, plain and destructured.
(let_declaration
  pattern: (identifier) @local.definition.var)
(let_declaration
  pattern: (tuple_pattern
    (identifier) @local.definition.var))
(let_declaration
  pattern: (ref_pattern
    (identifier) @local.definition.var))
(let_declaration
  pattern: (mut_pattern
    (identifier) @local.definition.var))

#; `let ... else` and `if let`/`while let`/`match` pattern bindings. A
#; lowercase identifier anywhere inside one of these patterns is a binding;
#; an uppercase one is a variant or struct name -- see this file's header.
((tuple_struct_pattern
  (identifier) @local.definition.var)
 (:match? @local.definition.var "^[_a-z]"))
((struct_pattern
  (field_pattern
    (identifier) @local.definition.var))
 (:match? @local.definition.var "^[_a-z]"))
((slice_pattern
  (identifier) @local.definition.var)
 (:match? @local.definition.var "^[_a-z]"))
((match_pattern
  (identifier) @local.definition.var)
 (:match? @local.definition.var "^[_a-z]"))
((or_pattern
  (identifier) @local.definition.var)
 (:match? @local.definition.var "^[_a-z]"))

#; File-level items. Captured (rather than left out like a function's own
#; name) because they sit outside every scope, so they read as file-level and
#; rename-symbol declines on them -- which is what tells "shadowed by an
#; outer binding" apart from "nothing in this file binds this name".
(static_item
  name: (identifier) @local.definition.var)
(const_item
  name: (identifier) @local.definition.var)

#; The for-loop pattern.
(for_expression
  pattern: (identifier) @local.definition.var)
(for_expression
  pattern: (tuple_pattern
    (identifier) @local.definition.var))

#; References
((identifier) @local.reference
  (:not-has-parent? @local.reference "scoped_identifier"))
