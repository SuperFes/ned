#; change-signature: call sites (ned's own query -- see cpp/calls.janet).

(call_expression
  function: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

(call_expression
  function: (field_expression
    field: (field_identifier) @call.callee)
  arguments: (argument_list) @call.arguments) @call.definition
