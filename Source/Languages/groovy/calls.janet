#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f(a: 1)` passes a map, not a parameter by name; `f(*xs)` spreads.

(function_call
  function: (identifier) @call.callee
  args: (argument_list) @call.arguments) @call.definition

(function_call
  function: (dotted_identifier
    (identifier) @call.callee
    .)
  args: (argument_list) @call.arguments) @call.definition

(map_item) @argument.named

(argument_list
  (access_op) @argument.spread)
