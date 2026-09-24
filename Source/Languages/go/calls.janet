#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f(xs...)` spreads.

(call_expression
  function: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

(call_expression
  function: (selector_expression
    field: (field_identifier) @call.callee)
  arguments: (argument_list) @call.arguments) @call.definition

(variadic_argument) @argument.spread
