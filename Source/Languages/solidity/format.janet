# Format captures (see Docs/FormattingRules.md for each name's pass). A control
# body is a block_statement inside a statement wrapper.

(function_definition body: (function_body) @brace.function)
(function_definition body: (function_body . (_) .) @brace.function.simple)
(constructor_definition body: (function_body) @brace.function)
(modifier_definition body: (function_body) @brace.function)

[(if_statement body: (statement (block_statement) @brace.control))
 (while_statement body: (statement (block_statement) @brace.control))
 (for_statement body: (statement (block_statement) @brace.control))
 (do_while_statement body: (statement (block_statement) @brace.control))
 (try_statement body: (block_statement) @brace.control)
 (catch_clause body: (block_statement) @brace.control)]

[(if_statement body: (statement (block_statement . (_) .) @brace.control.simple))
 (while_statement body: (statement (block_statement . (_) .) @brace.control.simple))
 (for_statement body: (statement (block_statement . (_) .) @brace.control.simple))
 (do_while_statement body: (statement (block_statement . (_) .) @brace.control.simple))
 (try_statement body: (block_statement . (_) .) @brace.control.simple)
 (catch_clause body: (block_statement . (_) .) @brace.control.simple)]

[(contract_declaration body: (contract_body) @brace.class)
 (library_declaration body: (contract_body) @brace.class)
 (struct_declaration body: (struct_body) @brace.class)]
(interface_declaration body: (contract_body) @brace.interface)

(if_statement "(" @control.parens.open ")" @control.parens.close)
(while_statement "(" @control.parens.open ")" @control.parens.close)
(for_statement "(" @control.parens.open ")" @control.parens.close)
(do_while_statement "(" @control.parens.open ")" @control.parens.close)

(if_statement "else" @control.keyword)
(catch_clause "catch" @control.keyword)
(do_while_statement "while" @control.keyword)

(source_file [(contract_declaration) (interface_declaration) (library_declaration) (function_definition)] @def.toplevel)
(source_file . [(contract_declaration) (interface_declaration) (library_declaration) (function_definition)] @def.toplevel.first)
(contract_body [(function_definition) (constructor_definition) (modifier_definition)] @def.method)
(contract_body . [(function_definition) (constructor_definition) (modifier_definition)] @def.method.first)
