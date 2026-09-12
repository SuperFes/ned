; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention and for what the delimiter imprint contributes without a
; capture -- checked against tree-sitter/tree-sitter-go's own
; grammar.js/node-types.json directly, not assumed. Worth knowing: the
; imprint also covers import_spec_list and var_spec_list (`import (\n\t"fmt"`),
; which the hand-written query never had -- measured over the grammar's own
; examples, every such line was flat under the query and indented in the
; file. "parameter_list" is Go's own node for a function/method's parameters
; AND its multi-value return type AND a method's receiver -- all three get
; @aligned for the same reason c-indents.scm's own parameter_list/
; argument_list do (a wrapped continuation line lines up under the first
; entry's own column, falling back to a plain indent when the opener is
; alone on its line).
(parameter_list) @aligned
(argument_list) @aligned

; lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
; STATEMENT/DECLARATION body an enclosing @aligned container's column
; alignment must not reach through -- alignment is a continuation-line rule
; ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
; ("go doStuff(func() {") is not a continuation of the argument list at
; all. Contributes no indent level of its own (the imprint's container for
; the same node does that); it only degrades an OUTER @aligned container
; back to plain level counting. Deliberately never applied to a
; data literal -- a multi-line initializer/object/array argument aligning
; its own body relative to the call's alignment column is existing,
; intentional behavior. See Editor/Indent.h.
(block) @align.barrier
(field_declaration_list) @align.barrier
(interface_type) @align.barrier
(expression_switch_statement) @align.barrier
(type_switch_statement) @align.barrier
(select_statement) @align.barrier

