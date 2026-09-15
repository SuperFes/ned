# configurable-formatter-rules follow-up: the third language over cpp/
# javascript's own capture template -- same capture NAMES throughout,
# tree-sitter-java's own node types underneath, every shape verified live
# against the real grammar before this file was written (a multi-
# init/update for-loop's own outer parens in particular, since Java allows
# several comma-separated init/update expressions unlike cpp/javascript's
# single ones -- confirmed the paired "(" ")" capture still grabs the
# OUTER pair regardless).
#
# A method's own body -- method_declaration's "body" field, typed "block".
# Optional in the grammar (an abstract/interface method has none), so this
# simply doesn't match those.
(method_declaration body: (block) @brace.function)

# Every other brace-carrying construct: control-flow bodies share one name
# (brace.control), matching cpp/javascript's own grouping. Java's switch
# is a single "switch_expression" node covering both switch-statement and
# switch-expression usage; its own body is "switch_block", not a "block".
# No "brace.namespace" here -- Java's package declaration isn't
# brace-delimited at all, unlike cpp's namespace.
(if_statement consequence: (block) @brace.control)
(while_statement body: (block) @brace.control)
(for_statement body: (block) @brace.control)
(switch_expression body: (switch_block) @brace.control)
(catch_clause body: (block) @brace.control)
(class_declaration body: (class_body) @brace.class)

# control.parens: if/while/switch's own condition is one node spanning
# exactly "(...)" ("parenthesized_expression"), the same shape cpp's
# condition_clause and javascript's own parenthesized_expression already
# satisfy.
(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)
(switch_expression condition: (parenthesized_expression) @control.parens)

# paired-delimiter-captures follow-up: a for-loop's own "(init; condition;
# update)" has no single spanning node here either (Java's own init/update
# fields are each "multiple: true", allowing several comma-separated
# expressions -- confirmed live that the paired capture still finds the
# OUTER "(" ")" regardless of how many init/update expressions sit between
# them). A catch clause's own parameter is an unnamed
# "catch_formal_parameter" child with no wrapping parens field, the same
# shape javascript's catch has (unlike cpp's own named "parameters" field)
# -- captured the same paired way.
(for_statement "(" @control.parens.open ")" @control.parens.close)
(catch_clause "(" @control.parens.open ")" @control.parens.close)

# collapse-simple follow-up: same "<name>.simple" marker convention, one
# per brace-carrying construct above (except brace.class -- collapse-simple
# is a statement-block concept, not a type-body one, matching cpp/
# javascript's own scope). Java's switch_block's "exactly one child" means
# exactly one switch_block_statement_group (case group).
(method_declaration body: (block . (_) .) @brace.function.simple)
(if_statement consequence: (block . (_) .) @brace.control.simple)
(while_statement body: (block . (_) .) @brace.control.simple)
(for_statement body: (block . (_) .) @brace.control.simple)
(switch_expression body: (switch_block . (_) .) @brace.control.simple)
(catch_clause body: (block . (_) .) @brace.control.simple)
