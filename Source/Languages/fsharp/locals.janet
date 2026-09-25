#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; F#'s own binding rules rather than upstream's (ionide/tree-sitter-fsharp),
#; which scopes each `let` to itself: there a value bound inside a function is
#; invisible to the lines after it, and `let x = x + 1` binds its own right
#; side.
#;
#; A `let` inside an expression is a declaration_expression whose `in:` is
#; the rest of the block, and that is the scope its names bind in; a `let`
#; with no `in:` is a module's, so it reads as file-level. A binding's body
#; reads the names from before it unless the `let` is `rec`. Patterns bind
#; every lower-case name in them: a union case is upper-case by convention
#; (the compiler warns on an upper-case variable pattern), and a case with
#; arguments is its pattern's head.

#; Scopes.
[
  (fun_expression)
  (rule)
  (for_expression)
] @local.scope

(declaration_expression
  in: (_)) @local.scope

#; A function covers its parameters and body.
(function_or_value_defn
  (function_declaration_left)) @local.scope

#; A function's name, bound around it.
((function_declaration_left
   (identifier) @local.definition.function)
 (:set! definition.function.scope "parent"))

(argument_patterns) @local.definition.parameter.pattern

#; A class: its primary constructor's arguments and class-level lets are
#; visible to every member; a member binds its self identifier and its
#; arguments.
(anon_type_defn) @local.scope
(member_defn) @local.scope
(primary_constr_args) @local.definition.parameter.pattern
(property_or_ident instance: (identifier) @local.definition.parameter)
(method_or_prop_defn args: (_) @local.definition.parameter.pattern)

(value_declaration_left) @local.definition.var.pattern

(rule
  pattern: (_) @local.definition.var.pattern)

(for_expression
  .
  (_) @local.definition.var.pattern)

(declaration_expression
  .
  (identifier) @local.definition.var)

#; Without `rec`, a binding's body reads the names from before it.
((function_or_value_defn
   (function_declaration_left
     (identifier) @local.declaration)
   body: (_) @local.initializer) @_definition
 (:not-match? @_definition "^let!?\\s+rec\\b"))

((function_or_value_defn
   (value_declaration_left) @local.declaration
   body: (_) @local.initializer) @_definition
 (:not-match? @_definition "^let!?\\s+rec\\b"))

(declaration_expression
  .
  (identifier) @local.declaration
  .
  (_) @local.initializer)

#; What a pattern reads rather than binds: a union case, a case applied to
#; arguments, a qualified name, a type annotation.
((identifier) @local.pattern.exclude
 (:match? @local.pattern.exclude "^[A-Z]"))

(identifier_pattern
  .
  (long_identifier_or_op) @local.pattern.exclude
  .
  (_))

(long_identifier
  (identifier)
  (identifier)) @local.pattern.exclude

(typed_pattern
  .
  (_)
  .
  (_) @local.pattern.exclude)

#; A use is the first name of a dotted path; the rest are members.
(long_identifier_or_op
  .
  (identifier) @local.reference)

(long_identifier
  .
  (identifier) @local.reference)

#; Names that are no variable: a record field being set, a member access.
(field_initializer
  .
  (long_identifier) @local.skip)

(dot_expression
  field: (_) @local.skip)
