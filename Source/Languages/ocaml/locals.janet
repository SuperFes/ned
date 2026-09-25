#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; OCaml's own binding rules rather than upstream's (tree-sitter-ocaml), which
#; captures only `value_pattern` and scopes each `let` binding to itself:
#; there `let x = x + 1 in x` never binds its `x`, so the last `x` reads as
#; the outer one and a rename of that would rewrite it too.
#;
#; `let p = e in body` binds p's names for the body, and for e only under
#; `rec`. A let pattern spells its names `value_name`; every other pattern --
#; a parameter, a match case, a `for` variable -- spells them `value_pattern`.
#; A top-level `let` has no enclosing `let ... in`, so it reads as file-level.
#;
#; Not modelled: a punned label or field (`f ~x`, `{ x }`) names the variable
#; and the label at once, so it is left alone and a rename misses it.

#; Scopes: a `let ... in` for its body, a binding for its parameters, and
#; every construct whose pattern binds for what follows it.
[
  (let_expression)
  (let_binding)
  (fun_expression)
  (match_case)
  (for_expression)
  (class_binding)
  (class_function)
  (method_definition)
  (object_expression)
] @local.scope

#; A binding's names, in the `let ... in` around it.
((let_binding
   pattern: (_) @local.definition.var.pattern)
 (:set! local.pattern.name "value_name")
 (:set! definition.var.scope "parent"))

#; Without `rec`, a binding's body reads the names from before it.
((value_definition
   (let_binding
     pattern: (_) @local.declaration
     body: (_) @local.initializer)) @_definition
 (:not-match? @_definition "^let\\s+rec\\b"))

((parameter
   pattern: (_) @local.definition.parameter.pattern)
 (:set! local.pattern.name "value_pattern"))

((match_case
   pattern: (_) @local.definition.var.pattern)
 (:set! local.pattern.name "value_pattern"))

(for_expression
  name: (value_pattern) @local.definition.var)

#; A punned label is both the label and the variable: `~x` and `?x` as a
#; parameter or an argument, `{ x }` in a record pattern or expression. A
#; rename writes `~x:renamed` and `{ x = renamed }`.
((parameter (value_pattern) @local.definition.parameter) @_parameter
 (:match? @_parameter "^[~?][a-z_]")
 (:not-match? @_parameter ":")
 (:set! local.pun "{old}:{new}"))
((labeled_argument . (label_name) @local.reference .)
 (:set! local.pun "{old}:{new}"))
((field_pattern . (field_path . (field_name) @local.definition.var .) .)
 (:set! local.pun "{old} = {new}"))
((field_expression . (field_path . (field_name) @local.reference .) .)
 (:set! local.pun "{old} = {new}"))

#; A use is a value path's own name, never one reached through a module.
(value_path
  .
  (value_name) @local.reference)
