# ned-authored. The imprint indents braced blocks; this adds continuation
# lines. awk lets a line break after `&&`, `||` and `,` and inside
# parentheses, so a wrapped condition or assignment sits a continuation
# step past its first line (see c/indents.janet).
[(binary_exp) (ternary_exp) (assignment_exp)] @indent.continuation

# An unbraced control-statement body on its own line sits one level in (see
# c/indents.janet). awk's bodies carry no field: each is the statement after
# the condition, or the last one after a for header.
(if_statement condition: (_) . (_) @indent.branch (:not-match? @indent.branch "^[{#]"))
(else_clause (_) @indent.branch (:not-match? @indent.branch "^[{#]"))
(while_statement condition: (_) . (_) @indent.branch (:not-match? @indent.branch "^[{#]"))
(for_statement (_) @indent.branch . (:not-match? @indent.branch "^[{#]"))
(for_in_statement (_) @indent.branch . (:not-match? @indent.branch "^[{#]"))
(do_while_statement . (_) @indent.branch (:not-match? @indent.branch "^[{#]"))
