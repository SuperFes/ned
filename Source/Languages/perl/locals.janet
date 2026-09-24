#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Perl's lexicals are `my`, `state` and a class's `field`, visible from the
#; declaration to the end of the enclosing block. `our` and `local` name
#; package variables, which other files can reach, so neither binds here. A
#; file-level `my` is still private to its file (`local.file-private`).
#;
#; The sigil picks the variable: `$x`, `@x` and `%x` are three of them, and
#; an element or slice names its container by the sigil of what it yields
#; (`$x[0]` and `@x[1, 2]` are both @x, `@h{...}` is %h). The bare name is
#; captured, so a rename keeps every sigil, and `local.namespace` keeps the
#; three apart.

#; Scopes: blocks, plus the constructs whose variables reach into one -- a
#; sub's signature, a loop's variable, a variable declared in a condition.
[
  (block)
  (subroutine_declaration_statement)
  (method_declaration_statement)
  (anonymous_subroutine_expression)
  (anonymous_method_expression)
  (for_statement)
  (cstyle_for_statement)
  (loop_statement)
  (conditional_statement)
] @local.scope

#; Declarations, one to two levels of `my ($a, ($b))` grouping deep.
((variable_declaration ["my" "state" "field"] (scalar (varname) @local.definition.var))
 (:set! local.namespace "scalar") (:set! local.file-private "true"))
((variable_declaration ["my" "state" "field"] (array (varname) @local.definition.var))
 (:set! local.namespace "array") (:set! local.file-private "true"))
((variable_declaration ["my" "state" "field"] (hash (varname) @local.definition.var))
 (:set! local.namespace "hash") (:set! local.file-private "true"))
((variable_declaration ["my" "state"] (variable_group (scalar (varname) @local.definition.var)))
 (:set! local.namespace "scalar") (:set! local.file-private "true"))
((variable_declaration ["my" "state"] (variable_group (array (varname) @local.definition.var)))
 (:set! local.namespace "array") (:set! local.file-private "true"))
((variable_declaration ["my" "state"] (variable_group (hash (varname) @local.definition.var)))
 (:set! local.namespace "hash") (:set! local.file-private "true"))
((variable_declaration ["my" "state"] (variable_group (variable_group (scalar (varname) @local.definition.var))))
 (:set! local.namespace "scalar") (:set! local.file-private "true"))
((variable_declaration ["my" "state"] (variable_group (variable_group (array (varname) @local.definition.var))))
 (:set! local.namespace "array") (:set! local.file-private "true"))
((variable_declaration ["my" "state"] (variable_group (variable_group (hash (varname) @local.definition.var))))
 (:set! local.namespace "hash") (:set! local.file-private "true"))

#; `for my $x (...)`; without `my` the loop aliases an existing variable.
((for_statement ["my" "state"] variable: (scalar (varname) @local.definition.var))
 (:set! local.namespace "scalar"))

#; Signature parameters, each wrapped in its kind (mandatory, optional,
#; named, slurpy).
((signature (_ (scalar (varname) @local.definition.parameter)))
 (:set! local.namespace "scalar"))
((signature (_ (array (varname) @local.definition.parameter)))
 (:set! local.namespace "array"))
((signature (_ (hash (varname) @local.definition.parameter)))
 (:set! local.namespace "hash"))

#; Uses.
((scalar (varname) @local.reference)
 (:set! local.namespace "scalar"))
((array (varname) @local.reference)
 (:set! local.namespace "array"))
((arraylen (varname) @local.reference)
 (:set! local.namespace "array"))
((hash (varname) @local.reference)
 (:set! local.namespace "hash"))
((array_element_expression array: (container_variable (varname) @local.reference))
 (:set! local.namespace "array"))
((hash_element_expression hash: (container_variable (varname) @local.reference))
 (:set! local.namespace "hash"))
((slice_expression array: (slice_container_variable (varname) @local.reference))
 (:set! local.namespace "array"))
((slice_expression hash: (slice_container_variable (varname) @local.reference))
 (:set! local.namespace "hash"))
((keyval_expression array: (keyval_container_variable (varname) @local.reference))
 (:set! local.namespace "array"))
((keyval_expression hash: (keyval_container_variable (varname) @local.reference))
 (:set! local.namespace "hash"))
