(list_lit
  . (sym_lit) @_head
  . (str_lit) @import.target
  (:any-of? @_head "load" "LOAD")) @import.statement
