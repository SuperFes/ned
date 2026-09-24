#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A member call's callee is the last name of the `type` the grammar reads
#; it as. `f(a: 1)` names its argument.

(call_expression
  (identifier) @call.callee
  (named_arguments) @call.arguments) @call.definition

(call_expression
  (type
    (identifier) @call.callee
    .)
  (named_arguments) @call.arguments) @call.definition

(named_argument
  (identifier) @argument.name
  (expression)) @argument.named
