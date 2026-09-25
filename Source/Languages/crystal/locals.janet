#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Crystal binds like Ruby: an assignment introduces a local, a method body
#; sees nothing from outside it, and a block sees its enclosing locals.

((method_def) @local.scope
 (:set! local.scope-inherits false))

[
  (block)
  (proc)
] @local.scope

(param name: (identifier) @local.definition.parameter)

#; An assignment inside a block to a local the block can already see writes
#; that local; only otherwise does it introduce one (`local.assignment`).
((assign lhs: (identifier) @local.definition.var)
 (:set! local.assignment "true"))
((op_assign lhs: (identifier) @local.definition.var)
 (:set! local.assignment "true"))

(identifier) @local.reference

(call method: (identifier) @local.skip)
