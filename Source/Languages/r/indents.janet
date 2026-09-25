# ned-authored. Every delimited body indents from the grammar's delimiters
# (Editor/ImprintIndent.h); what's here is the part they can't say.
#
# Continuation lines -- see c-indents.scm. `<-`, arithmetic, `&&` and a
# pipe (`%>%`, `|>`) are all a binary_operator.
(binary_operator) @indent.continuation
