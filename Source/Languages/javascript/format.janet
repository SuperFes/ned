# configurable-formatter-rules follow-up: cpp/format.janet's own two pilot
# captures, rolled out to a second language deliberately -- proving the
# "<language>/<capture>" override tier (Editor/FormatRules.h) gets exercised
# by a real grammar difference, not just a hand-built test. Same capture
# NAMES as cpp's (a rule written once applies to both); different grammar
# node types underneath, per tree-sitter-javascript's own node-types.json --
# a function declaration's body is "statement_block" (cpp: "compound_statement"),
# and an if/while's own condition is "parenthesized_expression" (cpp:
# "condition_clause"). Both still span exactly their own brace/parens pair,
# which is the only contract Editor/FormatBracePlacement.h and
# Editor/FormatSpacing.h actually depend on.
(function_declaration body: (statement_block) @brace.function)
(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)

# capture-coverage-widening follow-up: same names as cpp's own widened set
# (see that file's own comment), JavaScript's node types underneath. Each
# field-typed child keeps a braceless body ("if (x) return;") from ever
# matching -- there's no brace there to place.
(if_statement consequence: (statement_block) @brace.control)
(while_statement body: (statement_block) @brace.control)
(for_statement body: (statement_block) @brace.control)
(switch_statement body: (switch_body) @brace.control)
(catch_clause body: (statement_block) @brace.control)
(class_declaration body: (class_body) @brace.class)

# switch's own condition is "value:", not "condition:" -- tree-sitter-
# javascript's own field name, confirmed against node-types.json rather
# than assumed from cpp's. Its span is exactly "(...)" the same way
# if/while's parenthesized_expression is, so the same capture name and
# contract apply with no new C++ code.
(switch_statement value: (parenthesized_expression) @control.parens)

# Deliberately NOT captured, both for the same "no single delimited node to
# attach to" reason cpp/format.janet's own for-loop exclusion documents:
# - a for-loop's own "(init; condition; update)" -- three independent
#   fields around bare anonymous "(" ")" tokens, not one node.
# - a catch clause's own parens -- tree-sitter-javascript's catch_clause
#   has a bare "parameter:" field (identifier/array_pattern/object_pattern,
#   no wrapping parens node at all, unlike cpp's own parameter_list), and
#   ES2019+ allows `catch { ... }` with no parameter/parens at all. cpp's
#   own catch_clause DOES get :space coverage (a real, load-bearing
#   difference between what two languages' grammars can express, not an
#   oversight here).
