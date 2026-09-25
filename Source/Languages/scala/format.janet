# Format captures (see Docs/FormattingRules.md for each name's pass). A body
# written with braces is a block; Scala 3's indented bodies have none and
# are never named here.

(function_definition body: (block) @brace.function)
(function_definition body: (block . (_) .) @brace.function.simple)

[(if_expression consequence: (block) @brace.control)
 (if_expression alternative: (block) @brace.control)
 (while_expression body: (block) @brace.control)
 (for_expression body: (block) @brace.control)
 (match_expression body: (case_block) @brace.control)
 (try_expression body: (block) @brace.control)
 (finally_clause (block) @brace.control)]

[(if_expression consequence: (block . (_) .) @brace.control.simple)
 (if_expression alternative: (block . (_) .) @brace.control.simple)
 (while_expression body: (block . (_) .) @brace.control.simple)
 (for_expression body: (block . (_) .) @brace.control.simple)
 (try_expression body: (block . (_) .) @brace.control.simple)
 (finally_clause (block . (_) .) @brace.control.simple)]

((class_definition body: (template_body) @brace.class) (:match? @brace.class "^\\{"))
((object_definition body: (template_body) @brace.class) (:match? @brace.class "^\\{"))
((trait_definition body: (template_body) @brace.interface) (:match? @brace.interface "^\\{"))

(if_expression condition: (parenthesized_expression) @control.parens)
(while_expression condition: (parenthesized_expression) @control.parens)

(if_expression "else" @control.keyword)
(try_expression (catch_clause "catch" @control.keyword))
(finally_clause "finally" @control.keyword)

(compilation_unit [(class_definition) (object_definition) (trait_definition) (function_definition)] @def.toplevel)
(compilation_unit . [(class_definition) (object_definition) (trait_definition) (function_definition)] @def.toplevel.first)
(template_body (function_definition) @def.method)
(template_body . (function_definition) @def.method.first)
