# smart-indentation follow-up. See c-indents.scm's own header comment for the
# general convention and for what the delimiter imprint contributes without a
# capture -- checked against tree-sitter/tree-sitter-java's own
# src/node-types.json/grammar.js directly, same discipline every other
# *-indents.scm in this project holds to. "formal_parameters" (a method's
# own parameter list), "argument_list" (a call's own arguments) and
# "annotation_argument_list" (an annotation's own "(...)") get @aligned,
# same reasoning c-indents.scm's own parameter_list/argument_list do.
(formal_parameters) @aligned
(argument_list) @aligned
(annotation_argument_list) @aligned

# lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
# STATEMENT/DECLARATION body an enclosing @aligned container's column
# alignment must not reach through -- alignment is a continuation-line rule
# ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
# ("submit(new Runnable() {") is not a continuation of the argument list at
# all. Contributes no indent level of its own (the imprint's container for
# the same node does that); it only degrades an OUTER @aligned container
# back to plain level counting. Deliberately never applied to a
# data literal -- a multi-line initializer/object/array argument aligning
# its own body relative to the call's alignment column is existing,
# intentional behavior. See Editor/Indent.h.
(block) @align.barrier
(constructor_body) @align.barrier
(class_body) @align.barrier
(interface_body) @align.barrier
(enum_body) @align.barrier
(annotation_type_body) @align.barrier
(module_body) @align.barrier
(switch_block) @align.barrier


# Continuation lines -- see c-indents.scm. Java's convention (IntelliJ's
# default, Oracle's and Google's styles alike) is two levels for every
# wrapped line, a call's or declaration's own arguments on a line of their
# own included (IndentStyle::continuation).
[(ternary_expression) (assignment_expression) (variable_declarator)
 (return_statement) (throw_statement) (method_invocation) (field_access)
 (argument_list) (formal_parameters)] @indent.continuation
# A broken `if (`/`while (` condition keeps its operators at its own level.
(binary_expression) @indent.continuation
(parenthesized_expression (binary_expression) @indent.suppress)
