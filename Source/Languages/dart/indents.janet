# ned-authored. The imprint indents every brace body; a case label carries no
# delimiters, so its statements sit one level in by query (dart format).
[(switch_statement_case) (switch_statement_default)] @indent.headed

# Continuation lines -- see c-indents.scm; dart format's are two levels
# (IndentStyle::continuation). A method chain has no node of its own (its
# selectors hang off the statement), so the statement carries it.
[(initialized_variable_definition) (assignment_expression) (expression_statement) (return_statement)
 (additive_expression) (multiplicative_expression) (relational_expression) (equality_expression)
 (logical_and_expression) (logical_or_expression) (if_null_expression)
 (conditional_expression)] @indent.continuation
# Cascades (`..a = b`) are one level.
(expression_statement (cascade_section) (:set! indent.levels "1")) @indent.continuation
(initialized_variable_definition (cascade_section) (:set! indent.levels "1")) @indent.continuation
