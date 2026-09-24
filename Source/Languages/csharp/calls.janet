#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f(b: 2)` names its argument. A member call's object supplies an
#; extension method's `this` parameter.

(invocation_expression
  function: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

(invocation_expression
  function: (generic_name (identifier) @call.callee)
  arguments: (argument_list) @call.arguments) @call.definition

(invocation_expression
  function: (member_access_expression
    name: (identifier) @call.callee) @call.receiver
  arguments: (argument_list) @call.arguments) @call.definition

(object_creation_expression
  type: [(identifier) @call.callee
         (generic_name (identifier) @call.callee)
         (qualified_name name: (identifier) @call.callee)]
  arguments: (argument_list) @call.arguments) @call.definition

(argument
  name: (identifier)) @argument.named
