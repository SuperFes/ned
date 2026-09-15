# configurable-formatter-rules follow-up: the twelfth language. tree-sitter-c
# is a separate grammar from tree-sitter-cpp (not a subset/superset
# relationship at the query level) -- every shape here verified live
# against the real C grammar rather than assumed to carry over from
# cpp/format.janet, and one real difference found doing so: C's own
# if/while/switch condition field is typed `parenthesized_expression`
# directly, NOT cpp's own `condition_clause` -- a different node type
# name for the identical "(...)"-spanning shape.
(function_definition body: (compound_statement) @brace.function)

# brace.control: every other brace-carrying statement. C has no
# try/catch (that's cpp-only), so no analogous capture here at all.
(if_statement consequence: (compound_statement) @brace.control)
(while_statement body: (compound_statement) @brace.control)
(for_statement body: (compound_statement) @brace.control)
(switch_statement body: (compound_statement) @brace.control)

# brace.class: struct AND union bodies fold together (both are plain
# data-field aggregates in C, the same "close enough" call cpp's own
# brace.class already makes for struct+class). enum_specifier's own body
# is a distinct node type (`enumerator_list`, holding `enumerator`s, not
# `field_declaration`s) with nothing to brace-place inside it the way a
# struct/union field list has, so it is deliberately not given a
# brace.class capture here -- def.toplevel below still names it.
(struct_specifier body: (field_declaration_list) @brace.class)
(union_specifier body: (field_declaration_list) @brace.class)

# control.parens: if/while/switch's own condition is a REQUIRED
# `parenthesized_expression` (mandatory parens, matching cpp/java/
# javascript's own shape) -- but note the different node type name from
# cpp's own `condition_clause`, confirmed live rather than assumed.
(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)
(switch_statement condition: (parenthesized_expression) @control.parens)

# paired-delimiter-captures follow-up: a for-loop's own
# "(init; condition; update)" has no single spanning node here either,
# the same shape every other C-family language's for-loop has.
(for_statement "(" @control.parens.open ")" @control.parens.close)

# collapse-simple follow-up: same "<name>.simple" marker convention, one
# per statement-block construct above (brace.class excluded, the same
# statement-block-only scope every prior language draws).
(function_definition body: (compound_statement . (_) .) @brace.function.simple)
(if_statement consequence: (compound_statement . (_) .) @brace.control.simple)
(while_statement body: (compound_statement . (_) .) @brace.control.simple)
(for_statement body: (compound_statement . (_) .) @brace.control.simple)
(switch_statement body: (compound_statement . (_) .) @brace.control.simple)

# blank-lines-kind follow-up: def.toplevel, the same name every prior
# language's file carries -- but no def.method at all, a real language
# absence rather than a scope cut: C structs/unions hold only data
# fields, never functions, so there is no "method nested in a type body"
# concept here at all (the same real difference go/format.janet's own
# file documents for Go).
(translation_unit [(function_definition) (struct_specifier) (union_specifier) (enum_specifier)] @def.toplevel)
(translation_unit . [(function_definition) (struct_specifier) (union_specifier) (enum_specifier)]
  @def.toplevel.first)

# coverage-audit follow-up: do-while's own body field is typed "statement"
# (grammar.json, the same abstract supertype every other construct here
# already narrows to compound_statement) -- simply never added.
(do_statement body: (compound_statement) @brace.control)
(do_statement body: (compound_statement . (_) .) @brace.control.simple)
