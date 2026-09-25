(normal_command
  (identifier) @_command
  (argument_list . (argument [(unquoted_argument) (quoted_argument (quoted_element))] @import.target))
  (:any-of? @_command "include" "add_subdirectory" "INCLUDE" "ADD_SUBDIRECTORY")) @import.statement
