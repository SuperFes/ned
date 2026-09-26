#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; An application lists its arguments after the function; one with fewer
#; than the function takes applies it partially.

(exp_apply
  .
  (exp_name [(variable) @call.callee
             (qualified_variable (variable) @call.callee)])) @call.definition @call.arguments.rest @call.curried
