#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `call s(...)` and a function reference, which reads like an array's
#; subscript; only a name some signature has is ever rewritten.

(subroutine_call
  subroutine: (identifier) @call.callee
  (argument_list) @call.arguments) @call.definition

(call_expression
  . (identifier) @call.callee
  (argument_list) @call.arguments) @call.definition

(keyword_argument
  name: (identifier) @argument.name) @argument.named
