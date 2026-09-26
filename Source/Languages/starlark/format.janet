(if_statement condition: (parenthesized_expression) @control.parens)
(elif_clause condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)

(module (function_definition) @def.toplevel)
(module . (function_definition) @def.toplevel.first)
