# ned-authored. Every delimited body indents from the grammar's delimiters
# (Editor/ImprintIndent.h); what's here is the part they can't say.
#
# Continuation lines -- see c-indents.scm.
[(local_declaration) (assignment_expression) (arithmetic_expression) (multiplicative_expression)
 (relational_expression) (equality_expression) (logical_and_expression) (logical_or_expression)
 (null_coalescing_expression) (ternary_expression) (member_access_expression)
 (method_call_expression) (return_statement)] @indent.continuation
