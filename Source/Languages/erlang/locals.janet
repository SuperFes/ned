#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; A variable belongs to its whole function clause. In a pattern, a
#; variable already bound is matched rather than rebound, which is
#; `local.assignment`: only its first binding introduces it. A fun's own
#; parameters shadow.

[
  (function_clause)
  (fun_clause)
] @local.scope

#; Every variable anywhere in a pattern binds: `{A, [B | T]}` binds all three.
((expr_args args: (_) @local.definition.parameter.pattern)
 (:set! local.pattern.name "var"))

((match_expr lhs: (_) @local.definition.var.pattern)
 (:set! local.pattern.name "var")
 (:set! local.assignment "true"))
((cr_clause pat: (_) @local.definition.var.pattern)
 (:set! local.pattern.name "var")
 (:set! local.assignment "true"))

((var) @local.reference
 (:not-eq? @local.reference "_"))
