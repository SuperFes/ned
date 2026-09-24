#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; Swift arguments are positional even when labelled, and a label travels
#; with its argument. A trailing closure sits outside the parens, so a call
#; that has one reads as short an argument and is declined.

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
