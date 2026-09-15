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
