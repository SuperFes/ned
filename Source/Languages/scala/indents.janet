# ned-authored. Braces come from the imprint. Scala 3's indentation syntax
# is an indented_block/indented_cases opened by a zero-width scanner token,
# which the imprint reads as an introducer rather than a body.
#
# A definition or match whose body is indented has its own first line as
# the header, so the definition itself is the container: a brace block that
# opens partway along the body's first line then closes at that line's level.
(function_definition body: (indented_block)) @indent.headed
(val_definition value: (indented_block)) @indent.headed
(var_definition value: (indented_block)) @indent.headed
(given_definition body: (indented_block)) @indent.headed
(match_expression body: (indented_cases)) @indent.headed

# A case's body may start on the line after its `=>`.
(case_clause) @indent.headed

# A class/object/enum body in Scala 3's colon form (`object A:`); the
# braced form gets the same answer from the imprint.
[(template_body) (enum_body) (with_template_body)] @indent

# Where one construct has several indented parts (if/else, try/catch/
# finally) the part itself is the container.
(if_expression (indented_block) @indent)
(try_expression (indented_block) @indent)
(catch_clause [(indented_block) (indented_cases)] @indent)
(finally_clause (indented_block) @indent)
(while_expression (indented_block) @indent)
(for_expression (indented_block) @indent)
# Enumerators written on their own lines under a bare `for` (not in parens or
# braces, which the imprint already indents).
(for_expression "for" . (enumerators) @indent)
(colon_argument [(indented_block) (indented_cases)] @indent)
