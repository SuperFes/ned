(function body: (block) @brace.function)
(function body: (block . (_) .) @brace.function.simple)

(if_expression (block) @brace.control)
(else_if_clause (block) @brace.control)
(else_clause (block) @brace.control)
(while_expression (block) @brace.control)
(for_expression (block) @brace.control)
(if_expression (block . (_) .) @brace.control.simple)
(else_if_clause (block . (_) .) @brace.control.simple)
(else_clause (block . (_) .) @brace.control.simple)
(while_expression (block . (_) .) @brace.control.simple)
(for_expression (block . (_) .) @brace.control.simple)
(switch_expression "{" @brace.control.open "}" @brace.control.close)

(module_binding definition: (block) @brace.namespace)

(else_if_clause "else" @control.keyword)
(else_clause "else" @control.keyword)

(source_file [(let_declaration) (type_declaration) (module_declaration)] @def.toplevel)
(source_file . [(let_declaration) (type_declaration) (module_declaration)] @def.toplevel.first)
