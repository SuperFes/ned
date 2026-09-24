#; tree-sitter-python lists `(attribute attribute: (identifier) @property)`
#; after its method-call pattern, so under later-pattern-wins a called
#; method would read as a property. Restated here, after it.
(call
  function: (attribute attribute: (identifier) @function.method))
