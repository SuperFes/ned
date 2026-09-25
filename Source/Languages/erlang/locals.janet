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

((expr_args args: (var) @local.definition.parameter)
 (:not-eq? @local.definition.parameter "_"))

((match_expr lhs: (var) @local.definition.var)
 (:not-eq? @local.definition.var "_")
 (:set! local.assignment "true"))
((match_expr lhs: (_ (var) @local.definition.var))
 (:not-eq? @local.definition.var "_")
 (:set! local.assignment "true"))
((cr_clause pat: (var) @local.definition.var)
 (:not-eq? @local.definition.var "_")
 (:set! local.assignment "true"))
((cr_clause pat: (_ (var) @local.definition.var))
 (:not-eq? @local.definition.var "_")
 (:set! local.assignment "true"))

((var) @local.reference
 (:not-eq? @local.reference "_"))
