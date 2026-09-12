; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention and for what the delimiter imprint contributes without a
; capture. Kotlin is the language the imprint's INSTANCE test exists for: a
; hidden "_block" rule inlines its braces into the parent, and function_body/
; control_structure_body/secondary_constructor are `{ ... }` OR an unbraced
; form -- the old query guarded that by hand with an explicit "{" child, and
; Editor/ImprintBracket.h's DelimitersOf now asks each instance whether it
; carries its brackets. The same "{" guard is kept below on the barriers,
; which are still declared here. The imprint also covers primary_constructor
; (`class Logger(\n  val level: Int`), which the hand-written query lacked.
; "function_value_parameters" (a function's own parameter list),
; "value_arguments" (a call's own arguments) and "indexing_suffix" (an
; "arr[i]" subscript) get @aligned, same reasoning c-indents.scm's own
; parameter_list/argument_list do.
(function_value_parameters) @aligned
(value_arguments) @aligned
(indexing_suffix) @aligned

; lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
; STATEMENT/DECLARATION body an enclosing @aligned container's column
; alignment must not reach through -- alignment is a continuation-line rule
; ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
; ("runCatching(block = {") is not a continuation of the argument list at
; all. Contributes no indent level of its own (the imprint's container for
; the same node does that); it only degrades an OUTER @aligned container
; back to plain level counting. Deliberately never applied to a
; data literal -- a multi-line struct/array/collection-literal argument
; aligning its own body relative to the call's alignment column is existing,
; intentional behavior. See Editor/Indent.h.
(lambda_literal) @align.barrier
(class_body) @align.barrier
(enum_class_body) @align.barrier
(when_expression) @align.barrier
(anonymous_initializer) @align.barrier
(catch_block) @align.barrier
(finally_block) @align.barrier
(function_body "{") @align.barrier
(control_structure_body "{") @align.barrier
(secondary_constructor "{") @align.barrier

