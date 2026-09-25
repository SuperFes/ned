#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; A function is a scope; its blocks are not. The first assignment in a
#; scope binds, later ones write the same variable. A variable's `$` is its
#; sigil, not its name; scope-qualified (`$script:x`) and braced (`${x}`)
#; variables are left alone.

[
  (function_statement)
  (class_method_definition)
] @local.scope

((script_parameter (variable) @local.definition.parameter)
 (:match? @local.definition.parameter "^\\$[A-Za-z_][A-Za-z0-9_]*$")
 (:offset! @local.definition.parameter 0 1 0 0))

((foreach_statement (variable) @local.definition.var)
 (:match? @local.definition.var "^\\$[A-Za-z_][A-Za-z0-9_]*$")
 (:offset! @local.definition.var 0 1 0 0))

((left_assignment_expression
   (logical_expression
     (bitwise_expression
       (comparison_expression
         (additive_expression
           (multiplicative_expression
             (format_expression
               (range_expression
                 (array_literal_expression
                   (unary_expression (variable) @local.definition.var))))))))))
 (:match? @local.definition.var "^\\$[A-Za-z_][A-Za-z0-9_]*$")
 (:offset! @local.definition.var 0 1 0 0))

((variable) @local.reference
 (:match? @local.reference "^\\$[A-Za-z_][A-Za-z0-9_]*$")
 (:offset! @local.reference 0 1 0 0))
