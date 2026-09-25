#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; GDScript's variables are block-scoped; a script's or an inner class's
#; top-level `var` is a member, which sits outside every scope here and so
#; stays a language server's business.

[
  (function_definition)
  (constructor_definition)
  (lambda)
  (for_statement)
  (pattern_section)
] @local.scope

(if_statement body: (body) @local.scope)
(elif_clause body: (body) @local.scope)
(else_clause body: (body) @local.scope)
(while_statement body: (body) @local.scope)

(variable_statement name: (name) @local.definition.var)
(const_statement name: (name) @local.definition.constant)

(variable_statement value: (_) @local.initializer) @local.declaration

(parameters (identifier) @local.definition.parameter)
(typed_parameter (identifier) @local.definition.parameter)
(default_parameter (identifier) @local.definition.parameter)
(typed_default_parameter (identifier) @local.definition.parameter)

(for_statement left: (identifier) @local.definition.var)
(pattern_binding (identifier) @local.definition.var)

(identifier) @local.reference

#; A member after a dot is the object's, not a variable.
(attribute . (_) (identifier) @local.skip)
(attribute . (_) (attribute_call (identifier) @local.skip))
