(groovy_import . import: (qualified_name) @import.module) @import.statement
(juxt_function_call
  function: (identifier) @_callee
  args: (argument_list (map_item key: (identifier) @_key value: (string (string_content) @import.target)))
  (:eq? @_callee "apply")
  (:eq? @_key "from")) @import.statement

#; The file's own package, which a move rewrites.
(groovy_package (qualified_name) @import.package) @import.statement
