#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Only `:=` declares; `=` assigns to an existing binding.

[
  (function_declaration)
  (function_literal)
  (block)
  (for_statement)
] @local.scope

(parameter_declaration name: (identifier) @local.definition.parameter)
(receiver name: (identifier) @local.definition.parameter)
(var_definition name: (identifier) @local.definition.var)

(var_declaration var_list: (expression_list (reference_expression (identifier) @local.definition.var)))
(var_declaration
  var_list: (expression_list (mutable_expression (reference_expression (identifier) @local.definition.var))))
(var_declaration expression_list: (_) @local.initializer) @local.declaration

(identifier) @local.reference

(selector_expression field: (_) @local.skip)
(function_declaration name: (identifier) @local.skip)
