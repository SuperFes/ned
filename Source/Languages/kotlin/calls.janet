#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A trailing lambda sits outside the parens, so a call that has one reads
#; as short an argument and is declined. `f(b = 2)` is named, `f(*xs)` spread.

(call_expression
  (simple_identifier) @call.callee
  (call_suffix
    (value_arguments) @call.arguments)) @call.definition

(call_expression
  (navigation_expression
    (navigation_suffix
      (simple_identifier) @call.callee))
  (call_suffix
    (value_arguments) @call.arguments)) @call.definition

(value_argument
  (simple_identifier)
  "=") @argument.named

(value_argument
  "*") @argument.spread
