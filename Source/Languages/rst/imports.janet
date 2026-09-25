(directive
  name: (type) @_name
  body: (body (arguments) @import.target)
  (:any-of? @_name "include" "literalinclude")) @import.statement
