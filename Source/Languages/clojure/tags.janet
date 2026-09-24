#; Symbol-kind query. tree-sitter-clojure ships none, and a Lisp tree carries
#; no definition nodes: a form defines by its head symbol's text, with the
#; name as the second element.

((list_lit
   .
   (sym_lit name: (sym_name) @_head)
   .
   (sym_lit name: (sym_name) @name)) @definition.module
 (:eq? @_head "ns"))

((list_lit
   .
   (sym_lit name: (sym_name) @_head)
   .
   (sym_lit name: (sym_name) @name)) @definition.function
 (:any-of? @_head "defn" "defn-" "defmacro" "defmulti"))

((list_lit
   .
   (sym_lit name: (sym_name) @_head)
   .
   (sym_lit name: (sym_name) @name)) @definition.variable
 (:any-of? @_head "def" "defonce"))

((list_lit
   .
   (sym_lit name: (sym_name) @_head)
   .
   (sym_lit name: (sym_name) @name)) @definition.interface
 (:eq? @_head "defprotocol"))

((list_lit
   .
   (sym_lit name: (sym_name) @_head)
   .
   (sym_lit name: (sym_name) @name)) @definition.class
 (:any-of? @_head "defrecord" "deftype"))
