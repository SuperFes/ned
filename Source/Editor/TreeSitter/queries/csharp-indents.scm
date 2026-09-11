; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention -- checked against tree-sitter/tree-sitter-c-sharp's own
; src/node-types.json/grammar.js directly, same discipline every other
; *-indents.scm in this project holds to. Mirrors csharp-folds.scm's own node
; selection (see that file's header comment for the declaration_list/
; switch_body/switch_expression/accessor_list/initializer_expression
; reasoning). "parameter_list"/"argument_list" (a method's own parameters/a
; call's own arguments) and their bracketed siblings "bracketed_parameter_
; list"/"bracketed_argument_list" (an indexer's "this[...]" and an element
; access's "arr[...]") all get @aligned rather than @indent, same reasoning
; c-indents.scm's own parameter_list/argument_list do.
(declaration_list) @indent
(enum_member_declaration_list) @indent
(block) @indent
(switch_body) @indent
(switch_expression) @indent
(accessor_list) @indent
(initializer_expression) @indent
(parameter_list) @aligned
(argument_list) @aligned
(bracketed_parameter_list) @aligned
(bracketed_argument_list) @aligned

; lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
; STATEMENT/DECLARATION body an enclosing @aligned container's column
; alignment must not reach through -- alignment is a continuation-line rule
; ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
; ("Task.Run(() => {") is not a continuation of the argument list at
; all. Contributes no indent level of its own (the same node's @indent
; capture above still does that); it only degrades an OUTER @aligned
; container back to plain level counting. Deliberately never applied to a
; data literal -- a multi-line initializer/object/array argument aligning
; its own body relative to the call's alignment column is existing,
; intentional behavior. See Editor/Indent.h.
(block) @align.barrier
(declaration_list) @align.barrier
(switch_body) @align.barrier
(accessor_list) @align.barrier

(declaration_list "}" @dedent)
(enum_member_declaration_list "}" @dedent)
(block "}" @dedent)
(switch_body "}" @dedent)
(switch_expression "}" @dedent)
(accessor_list "}" @dedent)
(initializer_expression "}" @dedent)
(parameter_list ")" @dedent)
(argument_list ")" @dedent)
(bracketed_parameter_list "]" @dedent)
(bracketed_argument_list "]" @dedent)
