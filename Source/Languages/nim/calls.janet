#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `obj.add(3)` supplies the first parameter; `f(a = 1)` names its
#; argument. A command call (`add obj, 3`) has no parens and is declined.

(call
  function: (identifier) @call.callee
  (argument_list) @call.arguments) @call.definition

(call
  function: (dot_expression
    right: (identifier) @call.callee) @call.receiver
  (argument_list) @call.arguments) @call.definition

(argument_list
  (equal_expression
    left: (identifier) @argument.name) @argument.named)
