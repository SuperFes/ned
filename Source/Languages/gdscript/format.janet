(if_statement condition: (parenthesized_expression) @control.parens)
(elif_clause condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)

(source [(function_definition) (class_definition)] @def.toplevel)
(source . [(function_definition) (class_definition)] @def.toplevel.first)
(class_body (function_definition) @def.method)
(class_body . (function_definition) @def.method.first)
