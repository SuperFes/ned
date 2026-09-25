# ned-authored, erlfmt layout. Every body -- a function clause, a case/
# receive arm, a fun, a catch clause -- is a clause_body that starts at its
# `->` on the header's own row, so its lines sit one level in.
(clause_body) @indent

# Keyword-bracketed expressions: arms one level in, `end` (and the catch/
# after that split a try or receive) back at the expression's own level.
[(case_expr) (if_expr) (try_expr) (maybe_expr) (block_expr)] @indent.headed
(receive_expr (receive_after) @indent.end) @indent.headed
(receive_expr) @indent.headed
(_ "end" @dedent)
(try_expr "catch" @dedent)
[(try_after) (receive_after)] @dedent

# Continuation lines -- see c-indents.scm.
[(match_expr) (binary_op_expr)] @indent.continuation
