# ned-authored. Every delimited body indents from the grammar's delimiters
# (Editor/ImprintIndent.h); what's here is the part they can't say.
#
# Continuation lines -- see c-indents.scm.
[(declaration) (assignment) (binary_op) (ternary_op) (return) (dotted_identifier)
 (function_call)] @indent.continuation
