#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A definition's own signature is a call_expression too, and isn't one.
#; `f(k=3)` names its argument, `f(xs...)` spreads.

((call_expression
   (identifier) @call.callee
   (argument_list) @call.arguments) @call.definition
 (:not-has-parent? @call.definition signature))

((call_expression
   (field_expression
     (identifier) @call.callee
     .)
   (argument_list) @call.arguments) @call.definition
 (:not-has-parent? @call.definition signature))

(argument_list
  (assignment
    .
    (identifier) @argument.name) @argument.named)

(argument_list
  (splat_expression) @argument.spread)
