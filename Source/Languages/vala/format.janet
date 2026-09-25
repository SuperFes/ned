# Format captures (see Docs/FormattingRules.md for each name's pass).

(method_declaration (block) @brace.function)
(method_declaration (block . (_) .) @brace.function.simple)
(creation_method_declaration (block) @brace.function)

[(if_statement (block) @brace.control)
 (else_statement (block) @brace.control)
 (while_statement (block) @brace.control)
 (for_statement (block) @brace.control)
 (foreach_statement (block) @brace.control)
 (do_statement (block) @brace.control)
 (try_statement . (block) @brace.control)
 (catch_clause (block) @brace.control)
 (finally_clause (block) @brace.control)]

[(if_statement (block . (_) .) @brace.control.simple)
 (else_statement (block . (_) .) @brace.control.simple)
 (while_statement (block . (_) .) @brace.control.simple)
 (for_statement (block . (_) .) @brace.control.simple)
 (foreach_statement (block . (_) .) @brace.control.simple)
 (do_statement (block . (_) .) @brace.control.simple)
 (try_statement . (block . (_) .) @brace.control.simple)
 (catch_clause (block . (_) .) @brace.control.simple)
 (finally_clause (block . (_) .) @brace.control.simple)]

(class_declaration "{" @brace.class.open "}" @brace.class.close)
(interface_declaration "{" @brace.interface.open "}" @brace.interface.close)

(if_statement "(" @control.parens.open ")" @control.parens.close)
(while_statement "(" @control.parens.open ")" @control.parens.close)
(for_statement "(" @control.parens.open ")" @control.parens.close)
(foreach_statement "(" @control.parens.open ")" @control.parens.close)
(switch_statement "(" @control.parens.open ")" @control.parens.close)
(catch_clause "(" @control.parens.open ")" @control.parens.close)
(do_statement "(" @control.parens.open ")" @control.parens.close)

(else_statement "else" @control.keyword)
(catch_clause "catch" @control.keyword)
(finally_clause "finally" @control.keyword)
(do_statement "while" @control.keyword)

(class_declaration (class_member (method_declaration) @def.method))
(source_file (namespace_member [(class_declaration) (interface_declaration) (method_declaration)] @def.toplevel))
