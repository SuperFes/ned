# Format captures (see Docs/FormattingRules.md for each name's pass). A
# function's or method's signature and body are siblings, not one node, so
# its definition is paired from the two (.open/.close).

(function_body (block) @brace.function)
(function_body (block . (_) .) @brace.function.simple)
(function_expression_body (block) @brace.function)
(function_expression_body (block . (_) .) @brace.function.simple)

[(if_statement consequence: (block) @brace.control)
 (if_statement alternative: (block) @brace.control)
 (while_statement body: (block) @brace.control)
 (for_statement body: (block) @brace.control)
 (do_statement body: (block) @brace.control)
 (switch_statement body: (switch_block) @brace.control)
 (try_statement body: (block) @brace.control)
 (try_statement (catch_clause) . (block) @brace.control)
 (finally_clause (block) @brace.control)]

[(if_statement consequence: (block . (_) .) @brace.control.simple)
 (if_statement alternative: (block . (_) .) @brace.control.simple)
 (while_statement body: (block . (_) .) @brace.control.simple)
 (for_statement body: (block . (_) .) @brace.control.simple)
 (do_statement body: (block . (_) .) @brace.control.simple)
 (try_statement body: (block . (_) .) @brace.control.simple)
 (try_statement (catch_clause) . (block . (_) .) @brace.control.simple)
 (finally_clause (block . (_) .) @brace.control.simple)]

[(class_definition body: (class_body) @brace.class)
 (mixin_declaration (class_body) @brace.class)
 (extension_declaration body: (extension_body) @brace.class)
 (enum_declaration body: (enum_body) @brace.class)]

(if_statement "(" @control.parens.open ")" @control.parens.close)
(for_statement "(" @control.parens.open ")" @control.parens.close)
(while_statement condition: (parenthesized_expression) @control.parens)
(do_statement condition: (parenthesized_expression) @control.parens)
(switch_statement condition: (parenthesized_expression) @control.parens)

(if_statement "else" @control.keyword)
(try_statement "catch" @control.keyword)
(finally_clause "finally" @control.keyword)
(do_statement "while" @control.keyword)

(program [(class_definition) (mixin_declaration) (extension_declaration) (enum_declaration)] @def.toplevel)
(program
  [(function_signature) (getter_signature) (setter_signature)] @def.toplevel.open
  .
  (function_body) @def.toplevel.close)
(program
  .
  [(class_definition) (mixin_declaration) (extension_declaration) (enum_declaration)
   (function_signature) (getter_signature) (setter_signature)] @def.toplevel.first)

(class_body (method_signature) @def.method.open . (function_body) @def.method.close)
(class_body . (method_signature) @def.method.first)
