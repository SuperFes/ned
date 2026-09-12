; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention and for what the delimiter imprint contributes without a
; capture. Checked against tree-sitter-javascript's own node-types.json.
; arguments/formal_parameters (@aligned-paren-column-alignment follow-up) get
; "aligned" -- see c-indents.scm's own comment on parameter_list/
; argument_list for why.
(arguments) @aligned
(formal_parameters) @aligned

; lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
; STATEMENT/DECLARATION body an enclosing @aligned container's column
; alignment must not reach through -- alignment is a continuation-line rule
; ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
; ("setTimeout(() => {") is not a continuation of the argument list at
; all. Contributes no indent level of its own (the imprint's container for
; the same node does that); it only degrades an OUTER @aligned container
; back to plain level counting. Deliberately never applied to a data
; literal -- a multi-line initializer/object/array argument aligning its own
; body relative to the call's alignment column is existing, intentional
; behavior. See Editor/Indent.h.
(statement_block) @align.barrier
(class_body) @align.barrier
(switch_body) @align.barrier

(parenthesized_expression) @aligned

; jsx-indent follow-up: JSX nests by matched tags rather than by brackets --
; the same shape html-indents.scm handles, and the reason the delimiter
; imprint contributes nothing for an ELEMENT (a tag pair is not a bracket
; pair; see Docs/ParsingEngine.md). Every one of these node types exists in
; the javascript, typescript and tsx grammars alike (checked in each
; grammar.json, not assumed -- a query naming a node type the grammar lacks
; fails to compile, which would take the whole mode's indentation with it).
;
; The imprint does cover jsx_opening_element (`<...>`) and jsx_expression
; (`{...}`), which genuinely are brackets, and that carries a subtlety worth
; knowing: because the opening tag is a NAMED node that resolution stops at,
; its being a container is what lets the walk's own self-exclusion fire on
; an element's own line instead of counting the element as one level deeper
; than itself. HTML needs a hardcoded promotion in Indent.cpp for exactly
; this; JSX gets it from the imprint. The oracle held byte for byte when
; the explicit capture came out.
(jsx_element) @indent
(jsx_self_closing_element) @indent

(jsx_element (jsx_closing_element) @dedent)
(jsx_self_closing_element "/>" @dedent)
