; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Deliberately simple bracket-depth indentation, NOT
; real Lisp per-form argument alignment (Emacs' lisp-indent-function/
; clojure-mode's own semantic indent rules, which special-case individual
; forms like "defn"/"let"/"if" to align differently) -- that needs per-form
; special-casing this generic level-counting engine has no concept of, a
; separate, larger follow-up if ever pursued. This still beats "no support
; at all" for the common case of a form's own body/blank continuation line.
; Checked against tree-sitter-janet-simple's own node-types.json.
(par_tup_lit) @indent
(sqr_tup_lit) @indent
(par_arr_lit) @indent
(sqr_arr_lit) @indent
(tbl_lit) @indent
(struct_lit) @indent

(par_tup_lit ")" @dedent)
(sqr_tup_lit "]" @dedent)
(par_arr_lit ")" @dedent)
(sqr_arr_lit "]" @dedent)
(tbl_lit "}" @dedent)
(struct_lit "}" @dedent)
