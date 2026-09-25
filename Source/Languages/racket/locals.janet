#; ned's delta over the vendored upstream/locals.janet.

#; A binding's value is evaluated before its name binds: `(let ([x (add1 x)])
#; ...)` reads the enclosing x.
((list
   .
   (symbol) @_let
   .
   (list
     (list
       .
       (symbol)
       .
       (_) @local.initializer) @local.declaration))
 (:match? @_let "^(let|let\\*|for|for\\*)$"))
