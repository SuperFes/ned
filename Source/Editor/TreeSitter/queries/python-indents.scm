; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention and for what the delimiter imprint contributes without a
; capture. Checked against tree-sitter-python's own node-types.json (the same
; discipline python-imports.scm's own header comment establishes), not
; assumed.
;
; Python's indentation works from the imprint alone: every suite is one
; "block" node whose byte range exactly matches the indented lines, and the
; imprint reports it as an indentation body with a header row above it
; (Editor/ImprintIndent.h) -- which is also what makes end-of-block dedent
; free: once the ancestor walk climbs OUT of a block, it simply stops
; contributing, no closing-delimiter @dedent needed (there's no closing
; token -- Python has none). Bracketed continuations (a multi-line call,
; list, dict, ...) are ordinary bracket bodies with their closer as a dedent.
;
; What structure cannot say is where a clause's own HEADER line goes:
; elif/else/except/finally are real sibling nodes of the block they follow,
; not part of that block, and need to align back to the clause chain's own
; opening level (the "if"/"try" line) rather than the preceding block's
; deeper one.
(elif_clause) @dedent
(else_clause) @dedent
(except_clause) @dedent
(finally_clause) @dedent
