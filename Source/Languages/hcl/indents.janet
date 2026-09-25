# ned-authored. HCL spells its braces and brackets as named rules
# (block_start "{", object_end "}", ...), which the imprint -- reading
# anonymous delimiter tokens -- does not see, so the containers and their
# closers are named here.
[(block)
 (object)
 (tuple)
 (for_tuple_expr)
 (for_object_expr)] @indent.headed
[(block_end)
 (object_end)
 (tuple_end)] @dedent

# Continuation lines -- see c-indents.scm.
[(attribute) (binary_operation) (conditional)] @indent.continuation
