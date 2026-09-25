(extends_statement (string) @import.target) @import.statement
(call
  (identifier) @_callee
  arguments: (arguments . (string) @import.target)
  (:any-of? @_callee "preload" "load")) @import.statement
