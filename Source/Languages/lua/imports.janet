(function_call
  name: (identifier) @_callee
  arguments: (arguments . (string content: (string_content) @import.module))
  (:eq? @_callee "require")) @import.statement
(function_call
  name: (identifier) @_callee
  arguments: (arguments . (string content: (string_content) @import.target))
  (:any-of? @_callee "dofile" "loadfile")) @import.statement
