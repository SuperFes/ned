#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; A code block `{...}` and a content block `[...]` are scopes (a heading's
#; section is not), and so are a function's `let`, a lambda and a `for`, over
#; their parameters and loop variables. A top-level `let` is a module member
#; another file imports, so it stays file-level. A named argument's or a
#; dictionary's key (`n: 3`) and a field (`d.key`) are no variables.

(block) @local.scope
((content) @local.scope
 (:not-has-parent? @local.scope "section"))
(let pattern: (call)) @local.scope
(lambda) @local.scope
(for) @local.scope

(let pattern: (ident) @local.definition.var)
(let pattern: (group (ident) @local.definition.var))
((let pattern: (call item: (ident) @local.definition.function))
 (:set! definition.function.scope "parent"))
(let pattern: (call (group (ident) @local.definition.parameter)))
(let pattern: (call (group (tagged field: (ident) @local.definition.parameter))))
(lambda pattern: (ident) @local.definition.parameter)
(lambda pattern: (group (ident) @local.definition.parameter))
(for pattern: (ident) @local.definition.var)
(for pattern: (group (ident) @local.definition.var))

#; `let x = x + 1` reads the enclosing x; a function's parameters are its
#; body's own.
(let pattern: [(ident) (group)] value: (_) @local.initializer) @local.declaration

(ident) @local.reference
(field field: (ident) @local.skip)
(tagged field: (ident) @local.skip)
