#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `x |> f(y)` is `f(x, y)`: the piped value fills the first parameter.

(function_call
  function: [(identifier) @call.callee
             (field_access field: (label) @call.callee)]
  arguments: (arguments) @call.arguments) @call.definition

(binary_expression
  operator: "|>"
  right: (function_call
    function: [(identifier) @call.callee
               (field_access field: (label) @call.callee)]
    arguments: (arguments) @call.arguments) @call.definition) @call.receiver.first

(argument
  label: (label) @argument.name) @argument.named
