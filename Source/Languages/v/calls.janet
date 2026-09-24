#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f(a: 1)` fills a struct parameter's field, not a parameter;
#; `f(...xs)` spreads.

(call_expression
  name: (reference_expression (identifier) @call.callee)
  arguments: (argument_list) @call.arguments) @call.definition

(call_expression
  name: (selector_expression
    field: (reference_expression (identifier) @call.callee))
  arguments: (argument_list) @call.arguments) @call.definition

(argument
  (keyed_element)) @argument.named

(argument
  (spread_expression)) @argument.spread
