#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; Arguments are the call's own children after its paren (`.open`). A named
#; argument (`f(x = 1)`) is written as two siblings around `=`, so a call
#; with one declines. `x->f(a)` passes `x` as well, so it isn't matched at
#; all rather than read one argument short.

((call_expression
   function: (identifier) @call.callee
   "(" @call.arguments.open) @call.definition
 (:not-has-parent? @call.definition selector_call_expression))

((call_expression
   function: (member_expression
     (identifier) @call.callee .)
   "(" @call.arguments.open) @call.definition
 (:not-has-parent? @call.definition selector_call_expression))

(call_expression
  argument: (_) @argument.spread
  .
  "=")

(call_expression
  "="
  .
  argument: (_) @argument.spread)
