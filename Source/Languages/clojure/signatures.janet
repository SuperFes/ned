# Each arity of a multi-arity function is its own signature.
((list_lit . (sym_lit) @_def . (sym_lit) @signature.name . (vec_lit) @signature.parameters) @signature.definition
 (:any-of? @_def "defn" "defn-" "defmacro"))
((list_lit . (sym_lit) @_def . (sym_lit) @signature.name . (str_lit) . (vec_lit) @signature.parameters) @signature.definition
 (:any-of? @_def "defn" "defn-" "defmacro"))
((list_lit . (sym_lit) @_def . (sym_lit) @signature.name . (map_lit) . (vec_lit) @signature.parameters) @signature.definition
 (:any-of? @_def "defn" "defn-" "defmacro"))
((list_lit . (sym_lit) @_def . (sym_lit) @signature.name . (str_lit) . (map_lit) . (vec_lit) @signature.parameters) @signature.definition
 (:any-of? @_def "defn" "defn-" "defmacro"))
((list_lit . (sym_lit) @_def . (sym_lit) @signature.name . (list_lit . (vec_lit) @signature.parameters) @signature.definition)
 (:any-of? @_def "defn" "defn-" "defmacro"))
((list_lit . (sym_lit) @_def . (sym_lit) @signature.name . (str_lit) . (list_lit . (vec_lit) @signature.parameters) @signature.definition)
 (:any-of? @_def "defn" "defn-" "defmacro"))
((list_lit . (sym_lit) @_def . (sym_lit) @signature.name . (map_lit) . (list_lit . (vec_lit) @signature.parameters) @signature.definition)
 (:any-of? @_def "defn" "defn-" "defmacro"))
((list_lit . (sym_lit) @_def . (sym_lit) @signature.name . (str_lit) . (map_lit) . (list_lit . (vec_lit) @signature.parameters) @signature.definition)
 (:any-of? @_def "defn" "defn-" "defmacro"))

(vec_lit (sym_lit) @parameter.name @parameter)
((vec_lit (sym_lit) @parameter.skip . (sym_lit) @parameter.variadic) (:eq? @parameter.skip "&"))
