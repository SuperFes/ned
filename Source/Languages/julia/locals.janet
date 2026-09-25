#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Julia's own binding rules rather than upstream's (tree-sitter-julia),
#; which binds no parameters and reads every `name = value` as a binding,
#; including a call's keyword arguments (`plot(x, color = c)`) -- so a rename
#; of a local `color` would rewrite the keyword too.
#;
#; Only a function-like construct is a scope here. A loop, `let` or `try`
#; body assigning a name the function already has updates that local rather
#; than making a new one, so treating those bodies as scopes would split one
#; variable in two; binding their names in the function instead can only
#; merge two same-named variables, which a rename keeps consistent.

#; Scopes.
[
  (function_definition)
  (macro_definition)
  (arrow_function_expression)
  (do_clause)
  (comprehension_expression)
] @local.scope

#; `f(x) = x + 1`.
(assignment
  .
  (call_expression)) @local.scope

#; Parameters: every name in a signature's argument list, minus types and
#; default values.
(signature
  (call_expression
    (argument_list) @local.definition.parameter.pattern))
(signature
  (_
    .
    (call_expression
      (argument_list) @local.definition.parameter.pattern)))
(signature
  (_
    .
    (_
      .
      (call_expression
        (argument_list) @local.definition.parameter.pattern))))
(assignment
  .
  (call_expression
    (argument_list) @local.definition.parameter.pattern))
(arrow_function_expression
  .
  (argument_list) @local.definition.parameter.pattern)
(arrow_function_expression
  .
  (identifier) @local.definition.parameter)
(do_clause
  (identifier) @local.definition.parameter)

#; A parameter with a default is named here rather than through the list,
#; which passes over `name = value` as a call's keyword argument.
(signature
  (call_expression
    (argument_list
      (assignment
        .
        [(identifier) @local.definition.parameter
         (typed_expression . (identifier) @local.definition.parameter)]))))
(signature
  (_
    .
    (call_expression
      (argument_list
        (assignment
          .
          [(identifier) @local.definition.parameter
           (typed_expression . (identifier) @local.definition.parameter)])))))
(assignment
  .
  (call_expression
    (argument_list
      (assignment
        .
        [(identifier) @local.definition.parameter
         (typed_expression . (identifier) @local.definition.parameter)]))))

(argument_list
  (typed_expression
    .
    (_)
    (_) @local.pattern.exclude))
(argument_list
  (assignment
    .
    (_)
    (_) @local.pattern.exclude))
(argument_list
  (assignment
    .
    (typed_expression
      .
      (_)
      (_) @local.pattern.exclude)))

#; A nested function's name binds around it; a top-level one is the
#; module's.
((signature
   (call_expression
     .
     (identifier) @local.definition.function))
 (:set! definition.function.scope "parent"))
((signature
   (_
     .
     (call_expression
       .
       (identifier) @local.definition.function)))
 (:set! definition.function.scope "parent"))
((assignment
   .
   (call_expression
     .
     (identifier) @local.definition.function))
 (:set! definition.function.scope "parent"))

#; Statement-level assignments and declarations.
(block
  (assignment
    .
    [(identifier) @local.definition.var
     (typed_expression . (identifier) @local.definition.var)
     (tuple_expression (identifier) @local.definition.var)
     (open_tuple (identifier) @local.definition.var)]))
(source_file
  (assignment
    .
    [(identifier) @local.definition.var
     (typed_expression . (identifier) @local.definition.var)
     (tuple_expression (identifier) @local.definition.var)
     (open_tuple (identifier) @local.definition.var)]))
(module_definition
  (assignment
    .
    [(identifier) @local.definition.var
     (typed_expression . (identifier) @local.definition.var)
     (tuple_expression (identifier) @local.definition.var)
     (open_tuple (identifier) @local.definition.var)]))
(let_statement
  (assignment
    .
    [(identifier) @local.definition.var
     (typed_expression . (identifier) @local.definition.var)
     (tuple_expression (identifier) @local.definition.var)
     (open_tuple (identifier) @local.definition.var)]))
(local_statement
  (assignment
    .
    [(identifier) @local.definition.var
     (typed_expression . (identifier) @local.definition.var)
     (tuple_expression (identifier) @local.definition.var)
     (open_tuple (identifier) @local.definition.var)]))
(const_statement
  (assignment
    .
    [(identifier) @local.definition.var
     (typed_expression . (identifier) @local.definition.var)
     (tuple_expression (identifier) @local.definition.var)
     (open_tuple (identifier) @local.definition.var)]))
(let_statement
  (identifier) @local.definition.var)
(local_statement
  (identifier) @local.definition.var)

(for_binding
  .
  [(identifier) @local.definition.var
   (tuple_expression (identifier) @local.definition.var)])

(catch_clause
  .
  (identifier) @local.definition.var)

#; A binding's right side reads the names from before it: `let x = x`.
(let_statement
  (assignment
    .
    (_) @local.declaration
    (_) @local.initializer
    .))
(for_binding
  .
  (_) @local.declaration
  (_) @local.initializer
  .)

(identifier) @local.reference

#; Names that are no variable: a field, a keyword argument, a symbol.
(field_expression
  (_)
  .
  (identifier) @local.skip
  .)
(argument_list
  (assignment
    .
    (identifier) @local.skip))
(tuple_expression
  (assignment
    .
    (identifier) @local.skip))
(quote_expression
  (identifier) @local.skip)
