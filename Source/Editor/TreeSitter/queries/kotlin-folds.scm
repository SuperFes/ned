; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why (fwcd/tree-sitter-kotlin ships a highlights.scm
; and nothing else). Checked against the grammar's own src/node-types.json/
; grammar.js directly, and Kotlin needs one structural correction the other
; bracket languages don't: this grammar's brace-delimited block is a HIDDEN
; rule ("_block: seq('{', optional($.statements), '}')"), so the braces
; inline into whichever parent used it and the visible "statements" node
; covers only the content BETWEEN them -- folding "statements" would leave
; the closing brace inside the fold's own first hidden line. The parent is
; therefore what gets captured. Two of those parents are only sometimes
; braced -- a "function_body" may be "= expr" and a "control_structure_body"
; may be a single unbraced statement -- so both are matched with an explicit
; "{" child rather than unconditionally, which is what keeps a fold
; affordance off an expression-bodied function or a braceless if-branch.
; "when_expression"/"anonymous_initializer"/"catch_block"/"finally_block"/
; "secondary_constructor" are captured whole, keyword included, the same
; shape csharp-folds.scm's own switch_expression capture documents.
; "try_expression" is deliberately absent: its own braces inline the same
; way, but the node spans the catch/finally clauses too, so capturing it
; would fold a whole try/catch as one block; the try body itself has no
; node of its own to capture, and catch/finally each still fold separately.
(class_body) @fold
(enum_class_body) @fold
(lambda_literal) @fold
(when_expression) @fold
(anonymous_initializer) @fold
(catch_block) @fold
(finally_block) @fold
(function_body "{") @fold
(control_structure_body "{") @fold
(secondary_constructor "{") @fold
