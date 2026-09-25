#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Nim scopes a declaration to its block; a routine's parameters to the
#; routine.

[
  (proc_declaration)
  (func_declaration)
  (method_declaration)
  (iterator_declaration)
  (converter_declaration)
  (template_declaration)
  (macro_declaration)
  (proc_expression)
  (for)
  (block)
] @local.scope

(if (statement_list) @local.scope)
(elif_branch (statement_list) @local.scope)
(else_branch (statement_list) @local.scope)
(while (statement_list) @local.scope)
(of_branch (statement_list) @local.scope)

(parameter_declaration (symbol_declaration_list (symbol_declaration name: (identifier) @local.definition.parameter)))
(var_section (variable_declaration (symbol_declaration_list (symbol_declaration name: (identifier) @local.definition.var))))
(let_section (variable_declaration (symbol_declaration_list (symbol_declaration name: (identifier) @local.definition.var))))
(const_section (variable_declaration (symbol_declaration_list (symbol_declaration name: (identifier) @local.definition.constant))))
(for left: (symbol_declaration_list (symbol_declaration name: (identifier) @local.definition.var)))

(variable_declaration value: (_) @local.initializer) @local.declaration

(identifier) @local.reference

(dot_expression right: (identifier) @local.skip)
