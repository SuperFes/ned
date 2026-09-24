#; change-signature: call sites (ned's own query -- see cpp/calls.janet).

(call
  (identifier) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(attribute_call
  (identifier) @call.callee
  arguments: (arguments) @call.arguments) @call.definition
