#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;

[
  (function_declaration)
  (compound_statement)
  (for_statement)
] @local.scope

(variable_identifier_declaration name: (identifier) @local.definition.parameter)
(variable_statement (variable_declaration (identifier) @local.definition.var))
(variable_statement . (identifier) @local.definition.var)

(identifier) @local.reference

(composite_value_decomposition_expression accessor: (identifier) @local.skip)
(function_declaration name: (identifier) @local.skip)
