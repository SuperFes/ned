(list . (symbol) @_head (string) @import.target (:eq? @_head "require")) @import.statement
(list
  . (symbol) @_head
  (list . (symbol) @_file . (string) @import.target (:eq? @_file "file"))
  (:eq? @_head "require")) @import.statement
