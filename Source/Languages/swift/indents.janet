# ned-authored. The imprint indents every brace body, if/else chains included.
# A switch's case labels sit at the switch's own level (Swift's convention,
# swift-format's and SwiftLint's default) and each case body one level in.
(switch_statement) @indent.suppress
(switch_entry) @indent.headed

# Continuation lines -- see c-indents.scm.
[(additive_expression) (multiplicative_expression) (comparison_expression) (equality_expression)
 (conjunction_expression) (disjunction_expression) (nil_coalescing_expression) (ternary_expression)
 (infix_expression) (navigation_expression) (call_expression) (assignment)
 (control_transfer_statement)] @indent.continuation
# From its `=` on: an attribute on the line above isn't a continuation.
(property_declaration "=" @indent.begin) @indent.continuation
