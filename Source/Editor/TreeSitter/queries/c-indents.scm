; smart-indentation follow-up. Hand-written, ned-local capture-name convention
; for Editor/Indent.h's generic tree-walk engine -- "indent"/"dedent" borrowed
; from nvim-treesitter/Helix as capture NAMES only; "aligned", "indent.body",
; "align.barrier" and "indent.suppress" are ned's own. No upstream indents.scm
; exists for C to vendor.
;
; What is NOT here, and why. Every delimited body -- compound_statement,
; field_declaration_list, initializer_list, parameter_list, argument_list,
; and a dozen more the old query never named -- is an indent container with
; its closer as a dedent, from the delimiter imprint (Editor/ImprintIndent.h,
; Editor/ImprintTables.h) rather than from a capture. The imprint reads that
; off the grammar for every bundled language at once; restating it here was
; measured to change nothing over the oracle corpus and the grammar's own
; example files, so it is not restated. What remains in an indents.scm is
; the part structure cannot state: how a container aligns (@aligned,
; @align.barrier, @indent.body), which containers a language's layout
; convention says do NOT indent (@indent.suppress -- see cpp-indents.scm),
; and the shapes the imprint cannot read at all -- keyword-delimited bodies
; (bash, fish), tag pairs (html, xml, jsx) and clause headers (python's
; else/elif). A query capture on a node the imprint also reports stands
; alongside it; the walk counts a node once however many sources name it.
;
; parameter_list/argument_list (@aligned-paren-column-alignment follow-up)
; get "aligned": a wrapped declaration/call's continuation lines
; conventionally line up under the first parameter/argument's own column
; (e.g. "foo(a,\n    b)"), not one flat indent level deeper. The engine falls
; back to plain level counting when the opener is alone on its own line
; (nothing to align to), so this loses nothing for that shape.
(parameter_list) @aligned
(argument_list) @aligned

; lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
; STATEMENT/DECLARATION body an enclosing @aligned container's column
; alignment must not reach through -- alignment is a continuation-line rule
; ("foo(a,\n    b)", and nothing more). C itself has no block-bodied
; argument expression the way C++/JS/Go do (this capture is what keeps
; cpp-indents.scm's own lambda case right, and is carried here purely so the
; two queries stay the divergence-free pair their headers already claim),
; but a statement body reached from inside an @aligned container is the same
; situation wherever it arises. Contributes no indent level of its own (the
; imprint's container for the same node does that); it only degrades an
; OUTER @aligned container back to plain level counting. Deliberately never
; applied to a data literal -- a multi-line initializer-list argument
; aligning its own body relative to the call's alignment column is existing,
; intentional behavior. See Editor/Indent.h.
(compound_statement) @align.barrier
(field_declaration_list) @align.barrier
