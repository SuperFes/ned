(function_declaration body: (compound_statement) @brace.function)
(function_declaration body: (compound_statement . (_) .) @brace.function.simple)

(if_statement consequence: (compound_statement) @brace.control)
(if_statement consequence: (compound_statement . (_) .) @brace.control.simple)
(else_statement (compound_statement) @brace.control)
(else_statement (compound_statement . (_) .) @brace.control.simple)
(while_statement (compound_statement) @brace.control)
(while_statement (compound_statement . (_) .) @brace.control.simple)
(for_statement (compound_statement) @brace.control)
(for_statement (compound_statement . (_) .) @brace.control.simple)
(loop_statement "{" @brace.control.open "}" @brace.control.close)
(switch_statement "{" @brace.control.open "}" @brace.control.close)
(struct_declaration "{" @brace.class.open "}" @brace.class.close)

(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)
(for_statement "(" @control.parens.open ")" @control.parens.close)

(if_statement "else" @control.keyword)

(source_file [(function_declaration) (struct_declaration)] @def.toplevel)
(source_file . [(function_declaration) (struct_declaration)] @def.toplevel.first)
