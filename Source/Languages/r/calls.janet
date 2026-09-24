#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f(b = 2)` names its argument; `...` passed on spreads.

(call
  function: (identifier) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(call
  function: (namespace_operator
    rhs: (identifier) @call.callee)
  arguments: (arguments) @call.arguments) @call.definition

(argument
  name: (_) @argument.name) @argument.named

(argument
  value: (dots)) @argument.spread
