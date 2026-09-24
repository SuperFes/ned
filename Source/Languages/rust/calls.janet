#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A method call's object supplies `self`; a path call passes it.

(call_expression
  function: (identifier) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(call_expression
  function: (scoped_identifier
    name: (identifier) @call.callee)
  arguments: (arguments) @call.arguments) @call.definition

(call_expression
  function: (field_expression
    field: (field_identifier) @call.callee) @call.receiver
  arguments: (arguments) @call.arguments) @call.definition

(call_expression
  function: (generic_function
    function: (field_expression
      field: (field_identifier) @call.callee) @call.receiver)
  arguments: (arguments) @call.arguments) @call.definition

(call_expression
  function: (generic_function
    function: [(identifier) @call.callee (scoped_identifier name: (identifier) @call.callee)])
  arguments: (arguments) @call.arguments) @call.definition
