#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;

[
  (function_declaration)
  (block_statement)
  (foreach_statement)
  (for_statement)
  (function_literal)
] @local.scope

(parameter (identifier) @local.definition.parameter)
(foreach_type (identifier) @local.definition.var)

(auto_declaration variable: (identifier) @local.definition.var)
(auto_declaration value: (_) @local.initializer) @local.declaration
(declarator . (identifier) @local.definition.var)

(identifier) @local.reference

(property_expression (_) (identifier) @local.skip)
(named_argument . (identifier) @local.skip)
(function_declaration (identifier) @local.skip)
