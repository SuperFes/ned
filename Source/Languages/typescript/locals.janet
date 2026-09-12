#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows, and javascript-locals.scm for the shared rules
#; below, which this file repeats verbatim: tree-sitter-typescript is its own
#; grammar rather than an extension applied to the JavaScript one at query
#; time, so a query written against one is not automatically valid against
#; the other. This same text is also what tsx-mode uses -- the typescript
#; repo ships one query set covering both its typescript/ and tsx/ grammars,
#; the arrangement the highlights/folds/indents queries here already assume.
#;
#; TypeScript's own difference that matters: a parameter is wrapped in a
#; required_parameter/optional_parameter node rather than sitting directly in
#; formal_parameters, so the plain JavaScript parameter rule matches nothing
#; here and the two wrapped forms are added below. A constructor's
#; `private readonly dep: Dep` parameter property is a required_parameter
#; too, so it is captured like any other parameter -- correct for renaming
#; the parameter itself, and the field it implicitly declares is reached
#; through `this.dep`, a property_identifier this query never touches.

#; Scopes. statement_block covers `let`/`const` block scoping; the function
#; forms are listed separately because a parameter list sits outside the
#; body block.
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

#; Parameters. Every parameter is wrapped, annotated or not -- a bare
#; `(formal_parameters (identifier))` rule is an impossible pattern in this
#; grammar and the query will not compile with one, unlike in JavaScript.
[
  (required_parameter
    pattern: (identifier) @local.definition.parameter)
  (optional_parameter
    pattern: (identifier) @local.definition.parameter)
]
(arrow_function
  parameter: (identifier) @local.definition.parameter)
(catch_clause
  parameter: (identifier) @local.definition.parameter)
(assignment_pattern
  left: (identifier) @local.definition.parameter)

#; Variables
(variable_declarator
  name: (identifier) @local.definition.var)

#; Destructuring, object and array, including renamed and rest bindings.
#; `{ first }` binds `first`; `{ second: alias }` binds `alias`, never
#; `second` -- that half is the property being read, not a new name.
(object_pattern
  (shorthand_property_identifier_pattern) @local.definition.var)
(pair_pattern
  value: (identifier) @local.definition.var)
(array_pattern
  (identifier) @local.definition.var)
(rest_pattern
  (identifier) @local.definition.var)

#; for...of / for...in loop variables
(for_in_statement
  left: (identifier) @local.definition.var)

#; References
(identifier) @local.reference
