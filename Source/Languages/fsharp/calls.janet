#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f a b` is `(application_expression (application_expression f a) b)`:
#; the innermost application names the function and every one around it
#; adds an argument. Application and infix operators share a precedence in
#; the grammar, so `1 + f a` reads as `(1 + f) a`: the function is then the
#; infix expression's last operand, still right before its arguments.

(application_expression
  .
  (long_identifier_or_op
    [(identifier) @call.callee
     (long_identifier (identifier) @call.callee .)])) @call.definition @call.arguments.chain

(application_expression
  .
  (infix_expression
    (infix_op)
    .
    (long_identifier_or_op
      [(identifier) @call.callee
       (long_identifier (identifier) @call.callee .)]) .)) @call.definition @call.arguments.chain
