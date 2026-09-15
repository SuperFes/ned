# configurable-formatter-rules follow-up: Python is the edge case flagged
# back in the cpp/javascript/java rollouts -- verified live against
# tree-sitter-python's own node-types.json (not assumed) that its grammar
# has NO brace-delimited bodies anywhere: function_definition, if_statement,
# while_statement, for_statement, class_definition, try_statement's
# except/finally clauses, with_statement and match_statement's case_clause
# ALL use a bare "block" field with no wrapping delimiter tokens at all --
# indentation alone marks a body's extent. `brace.function`/`brace.control`/
# `brace.class`, the paired .open/.close mechanism, and `:collapse-empty`/
# `:collapse-simple` all fundamentally do not apply: ComputeBracePlacementEdits
# (FormatBracePlacement.cpp) hardcodes `text[capture.startByte]`/
# `text[capture.endByte - 1]` as single literal delimiter characters to
# reposition or splice -- there is no such character for a Python "block",
# so no capture of that shape belongs in this file at all.
#
# The one construct that DOES fit the existing template: if/while's own
# condition is an "expression" field, not a required parenthesized one --
# but the grammar still allows a user to write one anyway ("if (x):"), which
# parses as a real parenthesized_expression node. Verified live with a
# throwaway QueryMatcher probe that this only matches when parens are
# actually present in the source ("if x:" produces zero matches, "if (x):"
# produces exactly one) -- so :space rules on this capture are a real,
# narrow lever ("if a project's style guide permits/forbids redundant
# condition parens, keep the spacing inside them consistent") rather than
# a no-op. match_statement's own subject and case_clause's guard have the
# exact same "expression, not required-parenthesized" shape and are left
# uncaptured for now -- a match subject is routinely a tuple already
# wrapped for unrelated reasons, muddying what "redundant" even means there.
(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)
