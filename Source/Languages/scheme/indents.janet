# ned-authored, Emacs scheme-mode's layout. A call's arguments line up under
# its first argument (under the head when none shares its line); a binding
# or body form's body sits two columns past its own paren.
(list
  .
  (symbol) @_head
  (:any-of? @_head
    "define" "define-syntax" "define-record-type" "define-values" "define-library" "lambda" "case-lambda"
    "let" "let*" "letrec" "letrec*" "let-syntax" "letrec-syntax" "named-lambda"
    "begin" "when" "unless" "delay" "delay-force"
    "call-with-values" "call-with-input-file"
    "call-with-output-file" "with-input-from-file" "with-output-to-file" "dynamic-wind" "library")) @indent.body

# Distinguished arguments before the body (Emacs lisp-indent-specform).
(list
  .
  (symbol) @_head
  (:any-of? @_head "let-values" "let*-values" "with-syntax" "case" "parameterize" "guard" "syntax-rules")
  (:set! indent.specials "1")) @indent.body
(list
  .
  (symbol) @_head
  (:any-of? @_head "do" "syntax-case" "receive")
  (:set! indent.specials "2")) @indent.body

(list) @aligned.args
(vector) @aligned
