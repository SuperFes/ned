; Hand-written for Ned's generic-code-folding feature -- see c-folds.scm's
; own header comment for why. Every collection form is a foldable block:
; in a lisp, the (list_lit ...) form IS the function/let/ns body, so folding
; collection literals covers everything brace-folding covers elsewhere.
; Shared by ClojureMode and JankMode, same as clojure.scm itself.
(list_lit) @fold
(vec_lit) @fold
(map_lit) @fold
(set_lit) @fold
(anon_fn_lit) @fold
(read_cond_lit) @fold
