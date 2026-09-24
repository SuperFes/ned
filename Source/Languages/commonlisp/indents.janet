# ned-authored, common-lisp-indent-function's layout. A call's arguments line
# up under its first argument (under the head when none shares its line); a
# definition or binding form's body sits two columns past its own paren.
# defun/defmacro/defmethod/lambda are their own node, parens included.
(defun) @indent.body
(loop_macro) @aligned.args
(list_lit
  .
  (sym_lit) @_head
  (:any-of? @_head
    "let" "let*" "flet" "labels" "macrolet" "symbol-macrolet" "lambda" "when" "unless" "progn" "prog1" "prog2"
    "block"
    "restart-case" "unwind-protect"
   
    "with-standard-io-syntax"
    "defpackage")) @indent.body

# Distinguished arguments before the body (Emacs lisp-indent-specform).
(list_lit
  .
  (sym_lit) @_head
  (:any-of? @_head "dolist" "dotimes" "case" "ecase" "ccase" "typecase" "etypecase" "ctypecase" "handler-case" "handler-bind" "with-open-file" "with-open-stream" "with-input-from-string" "with-output-to-string" "eval-when" "catch" "defstruct")
  (:set! indent.specials "1")) @indent.body
(list_lit
  .
  (sym_lit) @_head
  (:any-of? @_head "multiple-value-bind" "destructuring-bind" "with-slots" "with-accessors" "do" "do*" "defclass" "define-condition")
  (:set! indent.specials "2")) @indent.body

(list_lit) @aligned.args
(vec_lit) @aligned
