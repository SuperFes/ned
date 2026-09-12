# smart-indentation follow-up. See c-indents.scm's own header comment for the
# general convention and for what the delimiter imprint contributes without a
# capture -- checked against tree-sitter/tree-sitter-c-sharp's own
# src/node-types.json/grammar.js directly, same discipline every other
# *-indents.scm in this project holds to. "parameter_list"/"argument_list"
# (a method's own parameters/a call's own arguments) and their bracketed
# siblings "bracketed_parameter_list"/"bracketed_argument_list" (an
# indexer's "this[...]" and an element access's "arr[...]") all get @aligned,
# same reasoning c-indents.scm's own parameter_list/argument_list do.
(parameter_list) @aligned
(argument_list) @aligned
(bracketed_parameter_list) @aligned
(bracketed_argument_list) @aligned

# lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
# STATEMENT/DECLARATION body an enclosing @aligned container's column
# alignment must not reach through -- alignment is a continuation-line rule
# ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
# ("Task.Run(() => {") is not a continuation of the argument list at
# all. Contributes no indent level of its own (the imprint's container for
# the same node does that); it only degrades an OUTER @aligned container
# back to plain level counting. Deliberately never applied to a
# data literal -- a multi-line initializer/object/array argument aligning
# its own body relative to the call's alignment column is existing,
# intentional behavior. See Editor/Indent.h.
(block) @align.barrier
(declaration_list) @align.barrier
(switch_body) @align.barrier
(accessor_list) @align.barrier

