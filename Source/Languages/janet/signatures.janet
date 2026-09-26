((par_tup_lit . (sym_lit) @_def . (sym_lit) @signature.name . (sqr_tup_lit) @signature.parameters) @signature.definition
 (:any-of? @_def "defn" "defn-" "defmacro" "defmacro-"))
((par_tup_lit . (sym_lit) @_def . (sym_lit) @signature.name . (str_lit) . (sqr_tup_lit) @signature.parameters) @signature.definition
 (:any-of? @_def "defn" "defn-" "defmacro" "defmacro-"))
((par_tup_lit . (sym_lit) @_def . (sym_lit) @signature.name . (long_str_lit) . (sqr_tup_lit) @signature.parameters) @signature.definition
 (:any-of? @_def "defn" "defn-" "defmacro" "defmacro-"))

(sqr_tup_lit (sym_lit) @parameter.name @parameter)
((sqr_tup_lit (sym_lit) @parameter.skip) (:any-of? @parameter.skip "&opt" "&keys" "&named"))
((sqr_tup_lit (sym_lit) @parameter.skip . (sym_lit) @parameter.variadic) (:eq? @parameter.skip "&"))
