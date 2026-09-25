# Format captures (see Docs/FormattingRules.md for each name's pass). V's
# braces stay on their header's line (:braces-on-header-line), so these
# only ever feed collapse and spacing rules.

(function_declaration body: (block) @brace.function)
(function_declaration body: (block . (_) .) @brace.function.simple)

[(if_expression block: (block) @brace.control)
 (else_branch block: (block) @brace.control)
 (for_statement body: (block) @brace.control)
 (match_arm block: (block) @brace.control)
 (match_else_arm_clause block: (block) @brace.control)]

[(if_expression block: (block . (_) .) @brace.control.simple)
 (else_branch block: (block . (_) .) @brace.control.simple)
 (for_statement body: (block . (_) .) @brace.control.simple)
 (match_arm block: (block . (_) .) @brace.control.simple)
 (match_else_arm_clause block: (block . (_) .) @brace.control.simple)]

(source_file [(function_declaration) (struct_declaration)] @def.toplevel)
(source_file . [(function_declaration) (struct_declaration)] @def.toplevel.first)
