#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f a b` is `(apply (apply f a) b)`: the innermost application names the
#; function and every one around it adds an argument.

(apply
  function: [(variable) @call.callee
             (qualified (variable) @call.callee)]) @call.definition @call.arguments.chain
