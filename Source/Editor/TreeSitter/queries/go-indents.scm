; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention -- checked against tree-sitter/tree-sitter-go's own
; grammar.js/node-types.json directly, same discipline rust-indents.scm's own
; header comment establishes, not assumed. Mirrors go-folds.scm's own node
; selection (see that file's header comment for why
; expression_switch_statement/type_switch_statement/select_statement/
; interface_type are each captured directly rather than via a child block --
; Go's switch/select/interface bodies have no separate wrapping node the way
; a func/if/for body's "block" does). "parameter_list" is Go's own node for a
; function/method's parameters AND its multi-value return type AND a method's
; receiver -- all three get @aligned for the same reason c-indents.scm's own
; parameter_list/argument_list do (a wrapped continuation line lines up under
; the first entry's own column, falling back to a plain indent when the
; opener is alone on its line).
(block) @indent
(field_declaration_list) @indent
(interface_type) @indent
(expression_switch_statement) @indent
(type_switch_statement) @indent
(select_statement) @indent
(literal_value) @indent
(parameter_list) @aligned
(argument_list) @aligned

; lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
; STATEMENT/DECLARATION body an enclosing @aligned container's column
; alignment must not reach through -- alignment is a continuation-line rule
; ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
; ("go doStuff(func() {") is not a continuation of the argument list at
; all. Contributes no indent level of its own (the same node's @indent
; capture above still does that); it only degrades an OUTER @aligned
; container back to plain level counting. Deliberately never applied to a
; data literal -- a multi-line initializer/object/array argument aligning
; its own body relative to the call's alignment column is existing,
; intentional behavior. See Editor/Indent.h.
(block) @align.barrier
(field_declaration_list) @align.barrier
(interface_type) @align.barrier
(expression_switch_statement) @align.barrier
(type_switch_statement) @align.barrier
(select_statement) @align.barrier

(block "}" @dedent)
(field_declaration_list "}" @dedent)
(interface_type "}" @dedent)
(expression_switch_statement "}" @dedent)
(type_switch_statement "}" @dedent)
(select_statement "}" @dedent)
(literal_value "}" @dedent)
(parameter_list ")" @dedent)
(argument_list ")" @dedent)
