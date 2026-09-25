#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Like Lua: `local` and `var` bind from the next form to the end of their
#; body; one at file level is private to the file.

[
  (fn)
  (lambda)
  (let)
  (each)
  (for)
] @local.scope

(parameters (binding (symbol) @local.definition.parameter))
(let_clause (binding (symbol) @local.definition.var))
(iter_bindings (binding (symbol) @local.definition.var))
(for_clause . (symbol) @local.definition.var)

((local (binding (symbol) @local.definition.var))
 (:set! local.file-private "true"))
((var (binding (symbol) @local.definition.var))
 (:set! local.file-private "true"))

(local . (binding) . (_) @local.initializer) @local.declaration
(var . (binding) . (_) @local.initializer) @local.declaration
(let_clause . (binding) . (_) @local.initializer) @local.declaration

(symbol) @local.reference
(multi_symbol . (symbol) @local.reference)

(multi_symbol (symbol) @local.skip)
(fn name: (symbol) @local.skip)
