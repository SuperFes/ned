#; Symbol-kind query, ned-authored: 6cdh/tree-sitter-scheme ships
#; highlights only. Scheme has one syntax, the list: a definition is a
#; list whose head is a define form and whose second element -- or the
#; head of its second element, for the (define (f x) ...) shorthand -- is
#; the name.
((list
   .
   (symbol) @_form
   .
   (symbol) @name)
  (:match? @_form "^(define|define-syntax|define-record-type|define-values|define-macro)$")) @definition.var

((list
   .
   (symbol) @_form
   .
   (list
     .
     (symbol) @name))
  (:match? @_form "^(define|define-syntax)$")) @definition.function
