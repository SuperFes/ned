; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Checked against tree-sitter-javascript's own
; node-types.json. arguments/formal_parameters (@aligned-paren-column-
; alignment follow-up) get "aligned" instead of "indent" -- see
; c-indents.scm's own comment on parameter_list/argument_list for why.
(statement_block) @indent
(object) @indent
(object_pattern) @indent
(array) @indent
(array_pattern) @indent
(arguments) @aligned
(formal_parameters) @aligned
(class_body) @indent
(switch_body) @indent

; lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
; STATEMENT/DECLARATION body an enclosing @aligned container's column
; alignment must not reach through -- alignment is a continuation-line rule
; ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
; ("setTimeout(() => {") is not a continuation of the argument list at
; all. Contributes no indent level of its own (the same node's @indent
; capture above still does that); it only degrades an OUTER @aligned
; container back to plain level counting. Deliberately never applied to a
; data literal -- a multi-line initializer/object/array argument aligning
; its own body relative to the call's alignment column is existing,
; intentional behavior. See Editor/Indent.h.
(statement_block) @align.barrier
(class_body) @align.barrier
(switch_body) @align.barrier

(statement_block "}" @dedent)
(object "}" @dedent)
(object_pattern "}" @dedent)
(array "]" @dedent)
(array_pattern "]" @dedent)
(arguments ")" @dedent)
(formal_parameters ")" @dedent)
(class_body "}" @dedent)
(switch_body "}" @dedent)
