#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; Jsonnet has no receivers: `obj.f(x)` passes exactly its arguments.

(functioncall
  . (id) @call.callee
  (args) @call.arguments) @call.definition

(functioncall
  (fieldaccess last: (id) @call.callee)
  (args) @call.arguments) @call.definition

(named_argument
  . (id) @argument.name) @argument.named
