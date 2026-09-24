# ned-authored. The imprint already indents bracketed bodies and
# struct/sig/begin/do ... end; what it can't see is a keyword-introduced
# body. Each such construct's first line is its header and everything after
# sits one level in (@indent.headed). Match arms and `in` stay at their
# construct's own level because match_expression and let_expression are
# deliberately not captured.
[(let_binding)
 (type_binding)
 (external)
 (value_specification)
 (match_case)
 (then_clause)
 (else_clause)
 (application_expression)] @indent.headed

# The scrutinee may continue onto following lines; `with` and the arms return
# to the construct's own level.
(match_expression "with" @indent.end) @indent.headed
(try_expression "with" @indent.end) @indent.headed
