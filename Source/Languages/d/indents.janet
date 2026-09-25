# ned-authored. Every delimited body indents from the grammar's delimiters
# (Editor/ImprintIndent.h); what's here is the part they can't say.
#
# Continuation lines -- see c-indents.scm.
[(variable_declaration) (auto_declaration) (assignment_expression) (binary_expression)
 (property_expression) (call_expression) (return_statement)] @indent.continuation
