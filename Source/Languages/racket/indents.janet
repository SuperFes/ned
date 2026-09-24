# ned-authored, racket-mode's layout: Scheme's, plus Racket's own definition,
# iteration and matching forms as body forms.
(list
  .
  (symbol) @_head
  (:any-of? @_head
    "define" "define-syntax" "define-syntax-rule" "define-values" "define/contract" "define-struct" "struct"
    "lambda" "λ" "case-lambda" "let" "let*" "letrec" "let-syntax"
    "begin" "begin0" "when" "unless" "match-define" "match-lambda"
    "syntax-parse" "module" "module+"
    "module*" "class" "class*"
    "unit" "dynamic-wind")) @indent.body

# Distinguished arguments before the body (Emacs lisp-indent-specform).
(list
  .
  (symbol) @_head
  (:any-of? @_head "let-values" "let*-values" "letrec-values" "with-syntax" "case" "match" "match*" "parameterize" "with-handlers" "syntax-rules" "for" "for*" "for/list" "for*/list" "for/vector" "for/hash" "for/and" "for/or" "for/sum" "for/first" "for/last")
  (:set! indent.specials "1")) @indent.body
(list
  .
  (symbol) @_head
  (:any-of? @_head "do" "syntax-case" "for/fold" "for*/fold")
  (:set! indent.specials "2")) @indent.body

(list) @aligned.args
(vector) @aligned
