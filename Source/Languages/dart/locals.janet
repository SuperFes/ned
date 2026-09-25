#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; A Dart function's signature and body are siblings with no node covering
#; both, so a function's scope joins the two (`@local.scope.join`) and holds
#; its parameters along with their uses. The same goes for a method, and for
#; a catch clause and its block. Class members are not locals: a bare field
#; name inside a method binds nothing here.

#; Scopes
((program
   (function_signature) @local.scope.join
   .
   (function_body) @local.scope.join))

((class_body
   (method_signature) @local.scope.join
   .
   (function_body) @local.scope.join))

((extension_body
   (method_signature) @local.scope.join
   .
   (function_body) @local.scope.join))

((try_statement
   (catch_clause) @local.scope.join
   .
   (block) @local.scope.join))

[
  (block)
  (lambda_expression)
  (function_expression)
  (for_statement)
  (if_statement)
  (switch_statement_case)
  (switch_expression_case)
] @local.scope

#; Parameters, typed (`int a`) or not (`a`).
(formal_parameter
  name: (identifier) @local.definition.parameter)

(formal_parameter
  .
  (identifier) @local.definition.parameter)

#; A function-typed parameter (`int cb(int x)`): its name binds, its own
#; parameter list only describes the type.
(formal_parameter
  (identifier) @local.definition.parameter
  .
  (formal_parameter_list) @local.skip)

(catch_parameters
  (identifier) @local.definition.parameter)

#; Local variables and loop variables.
(initialized_variable_definition
  name: (identifier) @local.definition.var)

(initialized_identifier
  .
  (identifier) @local.definition.var)

(for_loop_parts
  name: (identifier) @local.definition.var)

#; A local function's name is visible around it, not only inside.
((local_function_declaration
   (lambda_expression
     parameters: (function_signature
       name: (identifier) @local.definition.function)))
 (:set! definition.function.scope "parent"))

#; Patterns: a declaration's pattern binds every name in it; a case pattern
#; binds only its `var x` / `final T x` parts (a bare name there is a
#; constant). A record or object pattern's field names bind nothing.
(pattern_variable_declaration
  [
    (record_pattern)
    (list_pattern)
    (map_pattern)
    (object_pattern)
  ] @local.definition.var.pattern)

(variable_pattern
  (identifier) @local.definition.var
  .)

(record_pattern
  (identifier) @local.pattern.exclude)

(object_pattern
  (identifier) @local.pattern.exclude)

(identifier) @local.reference

#; Identifiers that name no variable: members after `.`, `?.` and `..`,
#; named-argument labels, `this.x` constructor parameters, a function's own
#; name, and pattern field names.
(unconditional_assignable_selector
  (identifier) @local.skip)

(conditional_assignable_selector
  (identifier) @local.skip)

(cascade_selector
  (identifier) @local.skip)

(label
  (identifier) @local.skip)

(constructor_param
  (identifier) @local.skip)

(function_signature
  name: (identifier) @local.skip)

(record_pattern
  (identifier) @local.skip)

(object_pattern
  (identifier) @local.skip)
