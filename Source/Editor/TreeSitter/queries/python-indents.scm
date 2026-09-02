; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Checked against tree-sitter-python's own
; node-types.json (the same discipline python-imports.scm's own header
; comment establishes), not assumed.
;
; Unlike the brace-delimited languages, Python's grammar already scopes every
; indented suite (function/class/if/for/while/with/try body) as one "block"
; node whose own byte range exactly matches the indented lines -- the same
; single capture python-folds.scm already relies on. This is what makes
; end-of-block dedent free: once the ancestor walk climbs OUT of a block, it
; simply stops contributing, no closing-delimiter @dedent capture needed at
; all (there's no closing token to capture -- Python has none). Only a
; clause's own HEADER line -- elif/else/except/finally -- needs an explicit
; @dedent, since each is a real sibling node of the block it follows, not
; part of that block itself, and needs to align back to the clause chain's
; own opening level (the "if"/"try" line) rather than the preceding block's
; deeper one.
(block) @indent

(elif_clause) @dedent
(else_clause) @dedent
(except_clause) @dedent
(finally_clause) @dedent

; Multi-line continuation inside an open bracket -- same bracket-depth
; mechanism the C-family tier uses, just Python's own container node names.
; Unlike "block" above, these DO have a real closing delimiter, so (unlike
; block) they need an explicit @dedent too -- otherwise a line that's just
; the lone closing bracket would wrongly indent one level deeper instead of
; aligning with its own opening line (the C-family/JSON tier's own
; precedent).
(parenthesized_expression) @indent
(tuple) @indent
(list) @indent
(list_comprehension) @indent
(dictionary) @indent
(dictionary_comprehension) @indent
(set) @indent
(set_comprehension) @indent
(argument_list) @indent
(parameters) @indent

(parenthesized_expression ")" @dedent)
(tuple ")" @dedent)
(list "]" @dedent)
(list_comprehension "]" @dedent)
(dictionary "}" @dedent)
(dictionary_comprehension "}" @dedent)
(set "}" @dedent)
(set_comprehension "}" @dedent)
(argument_list ")" @dedent)
(parameters ")" @dedent)
