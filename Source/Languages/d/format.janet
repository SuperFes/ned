# Format captures (see Docs/FormattingRules.md for each name's pass). Every
# braced body is a block_statement; a control body wraps it in a
# scope_statement, the block alone is the brace pair.

(function_declaration (function_body (block_statement) @brace.function))
(function_declaration (function_body (block_statement . (_) .) @brace.function.simple))
(function_literal (block_statement) @brace.function)
(function_literal (block_statement . (_) .) @brace.function.simple)

[(if_statement consequence: (scope_statement (block_statement) @brace.control))
 (if_statement alternative: (scope_statement (block_statement) @brace.control))
 (while_statement body: (scope_statement (block_statement) @brace.control))
 (for_statement body: (scope_statement (block_statement) @brace.control))
 (foreach_statement body: (scope_statement (block_statement) @brace.control))
 (switch_statement body: (scope_statement (block_statement) @brace.control))
 (try_statement body: (scope_statement (block_statement) @brace.control))
 (catch_statement body: (scope_statement (block_statement) @brace.control))
 (finally_statement body: (scope_statement (block_statement) @brace.control))
 (do_statement body: (scope_statement (block_statement) @brace.control))]

[(if_statement consequence: (scope_statement (block_statement . (_) .) @brace.control.simple))
 (if_statement alternative: (scope_statement (block_statement . (_) .) @brace.control.simple))
 (while_statement body: (scope_statement (block_statement . (_) .) @brace.control.simple))
 (for_statement body: (scope_statement (block_statement . (_) .) @brace.control.simple))
 (foreach_statement body: (scope_statement (block_statement . (_) .) @brace.control.simple))
 (try_statement body: (scope_statement (block_statement . (_) .) @brace.control.simple))
 (catch_statement body: (scope_statement (block_statement . (_) .) @brace.control.simple))
 (finally_statement body: (scope_statement (block_statement . (_) .) @brace.control.simple))
 (do_statement body: (scope_statement (block_statement . (_) .) @brace.control.simple))]

[(class_declaration (aggregate_body) @brace.class)
 (struct_declaration (aggregate_body) @brace.class)
 (union_declaration (aggregate_body) @brace.class)]
(interface_declaration (aggregate_body) @brace.interface)

(if_statement (if_condition) @control.parens)
(while_statement (if_condition) @control.parens)
(for_statement "(" @control.parens.open ")" @control.parens.close)
(foreach_statement "(" @control.parens.open ")" @control.parens.close)
(switch_statement "(" @control.parens.open ")" @control.parens.close)
(catch_statement "(" @control.parens.open ")" @control.parens.close)
(do_statement "(" @control.parens.open ")" @control.parens.close)

(if_statement (else) @control.keyword)
(catch_statement (catch) @control.keyword)
(finally_statement (finally) @control.keyword)
(do_statement (while) @control.keyword)

(source_file [(function_declaration) (class_declaration) (struct_declaration) (interface_declaration)] @def.toplevel)
(source_file . [(function_declaration) (class_declaration) (struct_declaration) (interface_declaration)] @def.toplevel.first)
(aggregate_body (function_declaration) @def.method)
(aggregate_body . (function_declaration) @def.method.first)
