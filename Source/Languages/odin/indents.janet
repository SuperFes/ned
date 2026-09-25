# ned-authored. Every delimited body indents from the grammar's delimiters
# (Editor/ImprintIndent.h); what's here is the part they can't say.
#
# Continuation lines -- see c-indents.scm.
[(var_declaration) (variable_declaration) (assignment_statement) (binary_expression)
 (ternary_expression) (member_expression) (call_expression) (return_statement)] @indent.continuation
