# ned-authored. Every delimited body indents from the grammar's delimiters
# (Editor/ImprintIndent.h); what's here is the part they can't say.
#
# Continuation lines -- see c-indents.scm. Every statement is a pipeline,
# so one written across lines (`Get-Item |` then `Where-Object`) is exactly
# a continuation.
[(pipeline) (assignment_expression)] @indent.continuation

# A `${...}` variable name is spelled with whatever its braces hold, line
# breaks and indentation included.
(braced_variable) @indent.ignore
