# load()'s first argument is a label naming a .bzl file.
(call
  function: (identifier) @_load
  arguments: (argument_list . (string) @import.target)
  (:eq? @_load "load")) @import.statement
