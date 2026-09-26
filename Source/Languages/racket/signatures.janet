((list . (symbol) @_def . (list . (symbol) @signature.name) @signature.parameters.rest) @signature.definition
 (:any-of? @_def "define" "define*" "define/public" "define/private" "define/override"))

((list . (symbol) @_def . (list . (symbol) (symbol) @parameter.name @parameter))
 (:any-of? @_def "define" "define*" "define/public" "define/private" "define/override"))
((list . (symbol) @_def . (list . (symbol) (list . (symbol) @parameter.name . (_) @parameter.default .) @parameter))
 (:any-of? @_def "define" "define*" "define/public" "define/private" "define/override"))
((list . (symbol) @_def . (list . (symbol) (dot) @parameter.skip . (symbol) @parameter.variadic))
 (:any-of? @_def "define" "define*" "define/public" "define/private" "define/override"))
