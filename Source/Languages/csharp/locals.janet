#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows, and java-locals.scm for why a class body is not
#; a scope and a field is not a definition -- C# is the same situation and
#; takes the same answer.
#;
#; `obj.local` spells its member half as a plain identifier here, so
#; member_access_expression is excluded from the reference rule with its
#; expression half added back, the same shape java-locals.scm uses.

#; Scopes
[
  (method_declaration)
  (constructor_declaration)
  (local_function_statement)
  (lambda_expression)
  (anonymous_method_expression)
  (block)
  (for_statement)
  (foreach_statement)
  (catch_clause)
  (using_statement)
] @local.scope

#; Parameters
(parameter
  name: (identifier) @local.definition.parameter)
(catch_declaration
  name: (identifier) @local.definition.parameter)

#; Local variables. A field_declaration wraps the same variable_declaration
#; node a local does, which is why the local rule is anchored on
#; local_declaration_statement rather than on variable_declaration itself.
(local_declaration_statement
  (variable_declaration
    (variable_declarator
      name: (identifier) @local.definition.var)))
(for_statement
  initializer: (variable_declaration
    (variable_declarator
      name: (identifier) @local.definition.var)))

#; The foreach loop variable, and a `x is T y` pattern binding.
(foreach_statement
  left: (identifier) @local.definition.var)
(declaration_pattern
  name: (identifier) @local.definition.var)

#; References
(member_access_expression
  expression: (identifier) @local.reference)
((identifier) @local.reference
  (:not-has-parent? @local.reference "member_access_expression"))
