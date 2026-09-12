; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. typescript-indents.scm's own rules plus JSX's -- the tsx
; dialect is the one whose parser knows `jsx_element` and friends, and a query
; naming a node type the parser does not know fails to compile, so the two
; dialects cannot share one file however similar they look.
(statement_block) @indent
(object) @indent
(object_pattern) @indent
(object_type) @indent
(array) @indent
(array_pattern) @indent
(arguments) @indent
(formal_parameters) @indent
(type_parameters) @indent
(class_body) @indent
(interface_body) @indent
(enum_body) @indent
(switch_body) @indent

(statement_block "}" @dedent)
(object "}" @dedent)
(object_pattern "}" @dedent)
(object_type "}" @dedent)
(array "]" @dedent)
(array_pattern "]" @dedent)
(arguments ")" @dedent)
(formal_parameters ")" @dedent)
(type_parameters ">" @dedent)
(class_body "}" @dedent)
(interface_body "}" @dedent)
(enum_body "}" @dedent)
(switch_body "}" @dedent)

(parenthesized_expression) @aligned
(parenthesized_expression ")" @dedent)

; jsx-indent follow-up: JSX nests by matched tags rather than by brackets --
; the same shape html-indents.scm handles, and the reason the delimiter
; imprint contributes nothing here (a tag pair is not a bracket pair; see
; Docs/ParsingEngine.md). Every one of these node types exists in the
; javascript, typescript and tsx grammars alike (checked in each
; grammar.json, not assumed -- a query naming a node type the grammar lacks
; fails to compile, which would take the whole mode's indentation with it).
;
; "jsx_opening_element" is captured for two reasons at once: a multi-line
; attribute list indents its own attributes, and -- because the opening tag
; is a NAMED node that resolution stops at -- capturing it is what lets the
; walk's own self-exclusion fire on an element's own line instead of counting
; the element as one level deeper than itself. HTML needs a hardcoded
; promotion in Indent.cpp for exactly this; JSX gets it from the query.
(parenthesized_expression) @aligned
(parenthesized_expression ")" @dedent)

(jsx_element) @indent
(jsx_expression) @indent
(jsx_opening_element) @indent
(jsx_self_closing_element) @indent

(jsx_element (jsx_closing_element) @dedent)
(jsx_expression "}" @dedent)
(jsx_opening_element ">" @dedent)
(jsx_self_closing_element "/>" @dedent)
