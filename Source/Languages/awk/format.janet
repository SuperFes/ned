# ned-authored. A rule's action is left out: its `{` must start on the
# pattern's own line, so no placement is safe to apply to it.
(func_def (block) @brace.function)
(func_def (block . (_) .) @brace.function.simple)

[(if_statement (block) @brace.control)
 (else_clause (block) @brace.control)
 (while_statement (block) @brace.control)
 (do_while_statement (block) @brace.control)
 (for_statement (block) @brace.control)
 (for_in_statement (block) @brace.control)]
[(if_statement (block . (_) .) @brace.control.simple)
 (else_clause (block . (_) .) @brace.control.simple)
 (while_statement (block . (_) .) @brace.control.simple)
 (do_while_statement (block . (_) .) @brace.control.simple)
 (for_statement (block . (_) .) @brace.control.simple)
 (for_in_statement (block . (_) .) @brace.control.simple)]

[(if_statement "(" @control.parens.open ")" @control.parens.close)
 (while_statement "(" @control.parens.open ")" @control.parens.close)
 (do_while_statement "(" @control.parens.open ")" @control.parens.close)
 (for_statement "(" @control.parens.open ")" @control.parens.close)
 (for_in_statement "(" @control.parens.open ")" @control.parens.close)]

(else_clause "else" @control.keyword)
(do_while_statement "while" @control.keyword)

(program [(func_def) (rule)] @def.toplevel)
(program . [(func_def) (rule)] @def.toplevel.first)
