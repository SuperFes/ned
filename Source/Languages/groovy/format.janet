# Format captures (see Docs/FormattingRules.md for each name's pass). Every
# braced body is a closure node, a class's included.

(function_definition body: (closure) @brace.function)
(function_definition body: (closure . (_) .) @brace.function.simple)

[(if_statement body: (closure) @brace.control)
 (if_statement else_body: (closure) @brace.control)
 (while_loop body: (closure) @brace.control)
 (for_loop body: (closure) @brace.control)
 (for_in_loop body: (closure) @brace.control)
 (switch_statement body: (switch_block) @brace.control)
 (try_statement body: (closure) @brace.control)
 (try_statement catch_body: (closure) @brace.control)
 (try_statement finally_body: (closure) @brace.control)]

[(if_statement body: (closure . (_) .) @brace.control.simple)
 (if_statement else_body: (closure . (_) .) @brace.control.simple)
 (while_loop body: (closure . (_) .) @brace.control.simple)
 (for_loop body: (closure . (_) .) @brace.control.simple)
 (for_in_loop body: (closure . (_) .) @brace.control.simple)
 (try_statement body: (closure . (_) .) @brace.control.simple)
 (try_statement catch_body: (closure . (_) .) @brace.control.simple)
 (try_statement finally_body: (closure . (_) .) @brace.control.simple)]

(class_definition body: (closure) @brace.class)

(if_statement condition: (parenthesized_expression) @control.parens)
(while_loop condition: (parenthesized_expression) @control.parens)
(switch_statement value: (parenthesized_expression) @control.parens)
(for_in_loop "(" @control.parens.open ")" @control.parens.close)
(for_loop "(" @control.parens.open ")" @control.parens.close)

(if_statement "else" @control.keyword)
(try_statement "catch" @control.keyword)
(try_statement "finally" @control.keyword)

(source_file [(class_definition) (function_definition)] @def.toplevel)
(source_file . [(class_definition) (function_definition)] @def.toplevel.first)
(class_definition body: (closure (function_definition) @def.method))
(class_definition body: (closure . (function_definition) @def.method.first))
