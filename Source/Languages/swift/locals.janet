#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Swift's own binding rules rather than upstream's (alex-pinkus/
#; tree-sitter-swift), which binds only imports and function names. A type
#; body is not a scope, the same answer java/locals.janet gives: a stored
#; property is a member, so it reads as not-local.
#;
#; `guard let` binds for the rest of the enclosing block; `if let`/`while
#; let` for their body. The shorthand `if let x {` binds nothing new here: its
#; `x` is both the new name and the one it unwraps, so it stays with the
#; outer binding and a rename of that carries it along. A pattern binds only
#; under `let`/`var`; `case .some(x)` compares against x.

#; Scopes.
[
  (function_declaration)
  (init_declaration)
  (lambda_literal)
  (statements)
  (if_statement)
  (guard_statement)
  (while_statement)
  (repeat_while_statement)
  (for_statement)
  (switch_entry)
  (catch_block)
] @local.scope

#; A nested function binds in the block around it; a top-level one or a
#; method reads as not-local.
((function_declaration
   name: (simple_identifier) @local.definition.function)
 (:set! definition.function.scope "parent"))

(parameter
  name: (simple_identifier) @local.definition.parameter)

(lambda_parameter
  name: (simple_identifier) @local.definition.parameter)

((property_declaration
   (pattern) @local.definition.var.pattern)
 (:set! local.pattern.name "simple_identifier"))

#; `if let x = y`, with or without a type annotation.
(if_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.definition.var
  .
  "=")
(guard_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.definition.var
  .
  "=")
(while_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.definition.var
  .
  "=")
(if_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.definition.var
  .
  (type_annotation)
  .
  "=")
(guard_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.definition.var
  .
  (type_annotation)
  .
  "=")
(while_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.definition.var
  .
  (type_annotation)
  .
  "=")

#; A pattern under `let`/`var`: `case .some(let x)`, `case let (a, b)`.
((pattern
   (value_binding_pattern)) @local.definition.var.pattern
 (:set! local.pattern.name "simple_identifier"))

((for_statement
   item: (pattern) @local.definition.var.pattern)
 (:set! local.pattern.name "simple_identifier"))

#; A declared name is visible after its initializer: `if let x = x` unwraps
#; the outer x.
(property_declaration
  (pattern) @local.declaration
  .
  "="
  .
  (_) @local.initializer)
(property_declaration
  (pattern) @local.declaration
  .
  (type_annotation)
  .
  "="
  .
  (_) @local.initializer)
(if_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.declaration
  .
  "="
  .
  (_) @local.initializer)
(guard_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.declaration
  .
  "="
  .
  (_) @local.initializer)
(while_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.declaration
  .
  "="
  .
  (_) @local.initializer)
(if_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.declaration
  .
  (type_annotation)
  .
  "="
  .
  (_) @local.initializer)
(guard_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.declaration
  .
  (type_annotation)
  .
  "="
  .
  (_) @local.initializer)
(while_statement
  (value_binding_pattern)
  .
  (simple_identifier) @local.declaration
  .
  (type_annotation)
  .
  "="
  .
  (_) @local.initializer)
(for_statement
  item: (_) @local.declaration
  collection: (_) @local.initializer)

(simple_identifier) @local.reference

#; Names that are no variable: a member, an argument label, a parameter's
#; external name, an enum case in a pattern.
(navigation_suffix
  (simple_identifier) @local.skip)

(value_argument_label
  (simple_identifier) @local.skip)

(parameter
  external_name: (simple_identifier) @local.skip)

(pattern
  "."
  .
  (simple_identifier) @local.skip)
