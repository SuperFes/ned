(if_statement (simple_expression . (parenthesis_expression) @control.parens .))
(elsif_statement (simple_expression . (parenthesis_expression) @control.parens .))

(design_file (design_unit) @def.toplevel)
(design_file . (design_unit) @def.toplevel.first)
