# Format captures (see Docs/FormattingRules.md for each name's pass). Odin's
# braces stay on their header's line (:braces-on-header-line), so these
# only ever feed collapse and spacing rules.

(procedure (block) @brace.function)
(procedure (block . (_) .) @brace.function.simple)

[(if_statement consequence: (block) @brace.control)
 (else_clause consequence: (block) @brace.control)
 (for_statement consequence: (block) @brace.control)
 (when_statement consequence: (block) @brace.control)]

[(if_statement consequence: (block . (_) .) @brace.control.simple)
 (else_clause consequence: (block . (_) .) @brace.control.simple)
 (for_statement consequence: (block . (_) .) @brace.control.simple)
 (when_statement consequence: (block . (_) .) @brace.control.simple)]

(source_file [(procedure_declaration) (struct_declaration)] @def.toplevel)
(source_file . [(procedure_declaration) (struct_declaration)] @def.toplevel.first)
