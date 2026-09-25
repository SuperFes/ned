#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; A plain `let` binds its names after all of its values, `let*` one by
#; one, `letrec` before any; a define inside a body is local to that body,
#; one at top level is the file's.

((list . (symbol) @_form) @local.scope
 (:any-of? @_form "define" "lambda" "let" "let*" "letrec" "letrec*" "let-values" "let*-values" "do"
  "case-lambda" "define-syntax" "let-syntax" "letrec-syntax"))

#; (define (f a b . rest) ...)
((list
   . (symbol) @_form
   . (list . (symbol) (symbol) @local.definition.parameter))
 (:eq? @_form "define"))

#; (define x ...) inside a body
((list . (symbol) @_form . (symbol) @local.definition.var)
 (:eq? @_form "define"))

#; (lambda (a b) ...) and (lambda args ...)
((list . (symbol) @_form . (list (symbol) @local.definition.parameter))
 (:eq? @_form "lambda"))
((list . (symbol) @_form . (symbol) @local.definition.parameter)
 (:eq? @_form "lambda"))

#; (let ((x v) ...) ...), named let's (let loop ((x v)) ...), and do's
#; (do ((i 0 (+ i 1))) ...).
((list . (symbol) @_form . (list (list . (symbol) @local.definition.var)))
 (:any-of? @_form "let" "let*" "letrec" "letrec*" "do"))
((list . (symbol) @_form . (symbol) @local.definition.function . (list (list . (symbol) @local.definition.var)))
 (:eq? @_form "let"))

#; A plain let's names bind after its last value.
((list
   . (symbol) @_form
   . (list (list . (symbol) . (_) @local.initializer) .) @local.declaration)
 (:eq? @_form "let"))
((list
   . (symbol) @_form
   . (symbol)
   . (list (list . (symbol) . (_) @local.initializer) .) @local.declaration)
 (:eq? @_form "let"))

#; let* binds each name after its own value.
((list
   . (symbol) @_form
   . (list (list . (symbol) . (_) @local.initializer) @local.declaration))
 (:eq? @_form "let*"))

(symbol) @local.reference

(quote (symbol) @local.skip)
