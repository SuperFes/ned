(call
  function: (identifier) @_callee
  arguments: (arguments . (argument value: (string content: (string_content) @import.target)))
  (:any-of? @_callee "source" "sys.source")) @import.statement
