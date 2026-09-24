#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f(b = 2)` names its argument, `f(xs: _*)` spreads.

(call_expression
  function: (identifier) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(call_expression
  function: (field_expression
    field: (identifier) @call.callee)
  arguments: (arguments) @call.arguments) @call.definition

(instance_expression
  (type_identifier) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(arguments
  (assignment_expression
    left: (identifier) @argument.name) @argument.named)

(arguments
  (ascription_expression
    (wildcard)) @argument.spread)
