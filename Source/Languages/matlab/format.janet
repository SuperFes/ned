(if_statement condition: (parenthesis) @control.parens)
(elseif_clause condition: (parenthesis) @control.parens)
(while_statement condition: (parenthesis) @control.parens)

(source_file (function_definition) @def.toplevel)
(source_file . (function_definition) @def.toplevel.first)
