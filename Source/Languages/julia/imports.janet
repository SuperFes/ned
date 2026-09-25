(call_expression
  (identifier) @_callee
  (argument_list . (string_literal (content) @import.target))
  (:eq? @_callee "include")) @import.statement
