#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Lua's own binding rules rather than upstream's (tree-sitter-grammars/
#; tree-sitter-lua), which binds every assignment: there `count = count + 1`
#; inside a function shadows an outer `local count`, so a rename of the local
#; would miss it. Only `local`, parameters, loop variables and `local
#; function` bind; a plain assignment is a use, and an unbound name is a
#; global -- nothing this file resolves.

#; Scopes: each block, plus the constructs whose names reach into one -- a
#; function's parameters, a for loop's variables.
[
  (block)
  (function_declaration)
  (function_definition)
  (for_statement)
] @local.scope

#; A chunk-level `local` is private to its file (`local.file-private`).
((variable_declaration
   "local"
   (variable_list
     name: (identifier) @local.definition.var))
 (:set! local.file-private "true"))

((variable_declaration
   "local"
   (assignment_statement
     (variable_list
       name: (identifier) @local.definition.var)))
 (:set! local.file-private "true"))

#; A `local` is visible from the next statement: `local print = print` keeps
#; the global on its right.
(variable_declaration
  (assignment_statement
    (expression_list) @local.initializer)) @local.declaration

#; The name is visible after the function and inside it (recursion), so it
#; binds in the scope around the function's own.
((function_declaration
   "local"
   name: (identifier) @local.definition.function)
 (:set! definition.function.scope "parent")
 (:set! local.file-private "true"))

(parameters
  name: (identifier) @local.definition.parameter)

(for_generic_clause
  (variable_list
    name: (identifier) @local.definition.var))

(for_numeric_clause
  name: (identifier) @local.definition.var)

(identifier) @local.reference

#; Identifiers that name no variable: fields, methods, table keys,
#; attributes and goto labels.
(dot_index_expression
  field: (identifier) @local.skip)

(method_index_expression
  method: (identifier) @local.skip)

(field
  name: (identifier) @local.skip)

(attribute
  (identifier) @local.skip)

(label_statement
  (identifier) @local.skip)

(goto_statement
  (identifier) @local.skip)
