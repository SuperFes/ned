#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `obj:m(...)` supplies `self`. A call with a string or table argument and
#; no parens (`f "x"`, `f {}`) is declined.

(function_call
  name: [(identifier) @call.callee
         (dot_index_expression field: (identifier) @call.callee)]
  arguments: (arguments) @call.arguments) @call.definition

(function_call
  name: (method_index_expression
    method: (identifier) @call.callee) @call.receiver
  arguments: (arguments) @call.arguments) @call.definition
