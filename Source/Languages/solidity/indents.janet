# ned-authored. Every delimited body indents from the grammar's delimiters
# (Editor/ImprintIndent.h); what's here is the part they can't say.
#
# Continuation lines -- see c-indents.scm.
[(variable_declaration_statement) (state_variable_declaration) (assignment_expression)
 (augmented_assignment_expression) (binary_expression) (ternary_expression) (member_expression)
 (call_expression) (return_statement)] @indent.continuation
