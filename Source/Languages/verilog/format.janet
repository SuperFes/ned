(conditional_statement (statement_or_null (statement (statement_item (seq_block "begin" @brace.control.open "end" @brace.control.close)))))
(loop_statement (statement_or_null (statement (statement_item (seq_block "begin" @brace.control.open "end" @brace.control.close)))))
(always_construct (statement (statement_item (procedural_timing_control_statement (statement_or_null (statement (statement_item (seq_block "begin" @brace.control.open "end" @brace.control.close))))))))
(always_construct (statement (statement_item (seq_block "begin" @brace.control.open "end" @brace.control.close))))

(conditional_statement "(" @control.parens.open ")" @control.parens.close)
(loop_statement "(" @control.parens.open ")" @control.parens.close)
(case_statement "(" @control.parens.open ")" @control.parens.close)

(conditional_statement "else" @control.keyword)

(source_file (module_declaration) @def.toplevel)
(source_file . (module_declaration) @def.toplevel.first)
