; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why. tree-sitter-python wraps every indented suite
; (function/class/if/for/while/with/try body) as a single "block" node
; regardless of which statement it belongs to, so this one capture covers
; all of them.
(block) @fold
