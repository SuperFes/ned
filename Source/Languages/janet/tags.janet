#; Symbol-kind query. janet-simple ships none, and a Lisp tree carries no
#; definition nodes: a form defines by its head symbol's text, with the name
#; as the second element.

((par_tup_lit
   .
   (sym_lit) @_head
   .
   (sym_lit) @name) @definition.function
 (:any-of? @_head "defn" "defn-" "defmacro" "defmacro-" "varfn"))

#; `def` and `var` are Janet's everyday local binding too, so only a
#; module-level one names a symbol.
((source
   (par_tup_lit
     .
     (sym_lit) @_head
     .
     (sym_lit) @name) @definition.constant)
 (:any-of? @_head "def" "def-" "defglobal"))

((source
   (par_tup_lit
     .
     (sym_lit) @_head
     .
     (sym_lit) @name) @definition.variable)
 (:any-of? @_head "var" "var-" "varglobal"))
