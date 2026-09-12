; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention -- checked against tree-sitter/tree-sitter-java's own
; src/node-types.json/grammar.js directly, same discipline every other
; *-indents.scm in this project holds to. Mirrors the node selection the
; deleted java-folds.scm made (`git log` carries its per-construct body-node
; reasoning; folding itself comes from the imprint now). "formal_parameters" (a method's own parameter list),
; "argument_list" (a call's own arguments) and "annotation_argument_list"
; (an annotation's own "(...)") get @aligned rather than @indent, same
; reasoning c-indents.scm's own parameter_list/argument_list do.
(class_body) @indent
(interface_body) @indent
(enum_body) @indent
(annotation_type_body) @indent
(module_body) @indent
(block) @indent
(constructor_body) @indent
(switch_block) @indent
(array_initializer) @indent
(element_value_array_initializer) @indent
(formal_parameters) @aligned
(argument_list) @aligned
(annotation_argument_list) @aligned

; lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
; STATEMENT/DECLARATION body an enclosing @aligned container's column
; alignment must not reach through -- alignment is a continuation-line rule
; ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
; ("submit(new Runnable() {") is not a continuation of the argument list at
; all. Contributes no indent level of its own (the same node's @indent
; capture above still does that); it only degrades an OUTER @aligned
; container back to plain level counting. Deliberately never applied to a
; data literal -- a multi-line initializer/object/array argument aligning
; its own body relative to the call's alignment column is existing,
; intentional behavior. See Editor/Indent.h.
(block) @align.barrier
(constructor_body) @align.barrier
(class_body) @align.barrier
(interface_body) @align.barrier
(enum_body) @align.barrier
(annotation_type_body) @align.barrier
(module_body) @align.barrier
(switch_block) @align.barrier

(class_body "}" @dedent)
(interface_body "}" @dedent)
(enum_body "}" @dedent)
(annotation_type_body "}" @dedent)
(module_body "}" @dedent)
(block "}" @dedent)
(constructor_body "}" @dedent)
(switch_block "}" @dedent)
(array_initializer "}" @dedent)
(element_value_array_initializer "}" @dedent)
(formal_parameters ")" @dedent)
(argument_list ")" @dedent)
(annotation_argument_list ")" @dedent)
