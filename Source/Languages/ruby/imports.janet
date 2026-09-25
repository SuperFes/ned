(call
  method: (identifier) @_method
  arguments: (argument_list . (string (string_content) @import.target))
  (:any-of? @_method "require" "require_relative" "load")) @import.statement
