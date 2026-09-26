((list . (symbol) @_def . (list . (symbol) @signature.name) @signature.parameters.rest) @signature.definition
 (:any-of? @_def "define" "define*"))

((list . (symbol) @_def . (list . (symbol) (symbol) @parameter.name @parameter))
 (:any-of? @_def "define" "define*"))
