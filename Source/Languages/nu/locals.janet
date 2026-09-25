#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;

[
  (decl_def)
  (block)
  (val_closure)
] @local.scope

(parameter param_name: (_) @local.definition.parameter)
(stmt_let var_name: (_) @local.definition.var)
(stmt_mut var_name: (_) @local.definition.var)
(stmt_const var_name: (_) @local.definition.constant)
(ctrl_for var_name: (_) @local.definition.var)

(stmt_let value: (_) @local.initializer) @local.declaration
(stmt_mut value: (_) @local.initializer) @local.declaration

(val_variable name: (identifier) @local.reference)
