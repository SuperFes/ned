; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention and for what the delimiter imprint contributes without a
; capture. typescript-indents.scm's own rules plus JSX's -- the tsx dialect is
; the one whose parser knows `jsx_element` and friends, and a query naming a
; node type the parser does not know fails to compile, so the two dialects
; cannot share one file however similar they look.
;
; `return (` + JSX is the idiom @aligned exists for here: it degrades to one
; plain level when the opener ends its line (the JSX case) and aligns under
; the first operand when it does not (`if (a &&`), so one capture serves
; both.
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
