# ned-authored. The imprint indents braced bodies and argument lists; this
# adds continuation lines, as c/indents.janet does for the same shapes.
[(variable_statement) (assignment_statement) (return_statement)] @indent.continuation
# A broken `if (`/`while (` condition keeps its operators at its own level.
(binary_expression) @indent.continuation
(parenthesized_expression (binary_expression) @indent.suppress)
