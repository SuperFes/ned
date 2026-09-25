# Format captures (see Docs/FormattingRules.md for each name's pass).
#
# A function's body is its own function_body node, braces included. A
# control statement's body is not: `if`, `guard`, the loops, `do`, `catch`
# and `switch` own their braces and the `statements` between them directly
# (an if/else owns both pairs), so each pair is anchored to what sits
# between its braces -- a `statements` node or nothing.

[(function_declaration body: (function_body) @brace.function)
 (init_declaration body: (function_body) @brace.function)
 (deinit_declaration body: (function_body) @brace.function)]
[(function_declaration body: (function_body (statements . (_) .)) @brace.function.simple)
 (init_declaration body: (function_body (statements . (_) .)) @brace.function.simple)]

[(class_declaration body: [(class_body) (enum_class_body)] @brace.class)
 (protocol_declaration body: (protocol_body) @brace.class)]

(if_statement "{" @brace.control.open . (statements) . "}" @brace.control.close)
(if_statement "{" @brace.control.open . "}" @brace.control.close)
(guard_statement "{" @brace.control.open . (statements) . "}" @brace.control.close)
(guard_statement "{" @brace.control.open . "}" @brace.control.close)
(while_statement "{" @brace.control.open . (statements) . "}" @brace.control.close)
(while_statement "{" @brace.control.open . "}" @brace.control.close)
(for_statement "{" @brace.control.open . (statements) . "}" @brace.control.close)
(for_statement "{" @brace.control.open . "}" @brace.control.close)
(repeat_while_statement "{" @brace.control.open . (statements) . "}" @brace.control.close)
(repeat_while_statement "{" @brace.control.open . "}" @brace.control.close)
(do_statement "{" @brace.control.open . (statements) . "}" @brace.control.close)
(do_statement "{" @brace.control.open . "}" @brace.control.close)
(catch_block "{" @brace.control.open . (statements) . "}" @brace.control.close)
(catch_block "{" @brace.control.open . "}" @brace.control.close)
(switch_statement "{" @brace.control.open "}" @brace.control.close)

(if_statement (else) @control.keyword)
(guard_statement (else) @control.keyword)
(catch_block (catch_keyword) @control.keyword)
(repeat_while_statement "while" @control.keyword)

(source_file [(function_declaration) (class_declaration) (protocol_declaration)] @def.toplevel)
(source_file . [(function_declaration) (class_declaration) (protocol_declaration)] @def.toplevel.first)
([(class_body) (enum_class_body)] [(function_declaration) (init_declaration) (deinit_declaration)] @def.method)
([(class_body) (enum_class_body)] . [(function_declaration) (init_declaration) (deinit_declaration)] @def.method.first)
(protocol_body (protocol_function_declaration) @def.method)
(protocol_body . (protocol_function_declaration) @def.method.first)
