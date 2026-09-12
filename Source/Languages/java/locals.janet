#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; A class body is deliberately NOT a scope and a field is deliberately not a
#; definition. A field's visibility is a property of the class, not of this
#; file -- a public field is reachable from anywhere in the program -- so
#; treating one as a local would rename it here and nowhere else. A reference
#; to a field therefore resolves to nothing and rename-symbol declines,
#; which is the right answer without a language server.
#;
#; `obj.local` and `use(arg)` both spell their right-hand half as a plain
#; identifier in this grammar (unlike JavaScript's property_identifier or
#; Go's field_identifier), so field_access and method_invocation are excluded
#; from the reference rule and their object halves added back explicitly --
#; otherwise renaming a local that shares a spelling with a field or a method
#; would rewrite the access or the call.

#; Scopes
[
  (method_declaration)
  (constructor_declaration)
  (compact_constructor_declaration)
  (lambda_expression)
  (block)
  (for_statement)
  (enhanced_for_statement)
  (catch_clause)
  (switch_block)
  (try_with_resources_statement)
] @local.scope

#; Parameters
(formal_parameter
  name: (identifier) @local.definition.parameter)
(spread_parameter
  (variable_declarator
    name: (identifier) @local.definition.parameter))
(catch_formal_parameter
  name: (identifier) @local.definition.parameter)
(inferred_parameters
  (identifier) @local.definition.parameter)
(lambda_expression
  parameters: (identifier) @local.definition.parameter)

#; Local variables, including a try-with-resources binding.
(local_variable_declaration
  declarator: (variable_declarator
    name: (identifier) @local.definition.var))
(resource
  name: (identifier) @local.definition.var)

#; The enhanced-for loop variable.
(enhanced_for_statement
  name: (identifier) @local.definition.var)

#; References -- see this file's header for the two exclusions.
(field_access
  object: (identifier) @local.reference)
(method_invocation
  object: (identifier) @local.reference)
((identifier) @local.reference
  (:not-has-parent? @local.reference "field_access")
  (:not-has-parent? @local.reference "method_invocation"))
