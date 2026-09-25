(list
  . (symbol) @_head
  . (string) @import.target
  (:any-of? @_head "load" "include" "include-ci")) @import.statement
