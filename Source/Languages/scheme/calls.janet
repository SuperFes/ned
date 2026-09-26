(list . (symbol) @call.callee) @call.definition @call.arguments.rest

# A definition's own header names its parameters; it isn't a call.
((list . (symbol) @_def . (list) @call.exclude)
 (:any-of? @_def "define" "define*"))
