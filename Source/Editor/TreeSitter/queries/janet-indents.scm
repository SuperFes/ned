; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Checked against tree-sitter-janet-simple's own
; node-types.json.
;
; real-per-form-lisp-indent follow-up: an ordinary call list (par_tup_lit)
; gets "aligned" -- Editor/Indent.h's engine aligns its continuation lines to
; the first argument's own column when one follows the opener on the same
; line (e.g. "(foo bar\n     baz)"), falling back to plain bracket-depth
; when the opener is alone on its own line, same as c-indents.scm's own
; argument_list. A short table of special forms whose BODY conventionally
; indents a fixed 2 columns past the form's own column instead (Emacs'
; lisp-indent-function convention -- "(let [x 1]\n  body)") gets
; "indent.body" on top of that via the SAME `.`-anchor + #any-of? idiom
; janet-imports.scm's own import-statement matching already uses; a node
; captured both ways (the common case for every special form below) is
; resolved as indent.body, see Indent.cpp. Array/table/struct literals
; (sqr_tup_lit/par_arr_lit/sqr_arr_lit/tbl_lit/struct_lit) aren't call forms
; and stay plain bracket-depth "@indent", unchanged.
(par_tup_lit
  .
  (sym_lit) @_head
  (#any-of? @_head
    "let" "fn" "do" "when" "when-not" "unless" "if" "if-not" "if-let" "when-let"
    "loop" "for" "each" "seq" "generate" "while" "repeat"
    "defn" "defn-" "def" "def-" "var" "defmacro" "defmacro-"
    "with" "with-dyns" "with-syms" "match" "case" "try" "default" "comment")) @indent.body

(par_tup_lit) @aligned
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
