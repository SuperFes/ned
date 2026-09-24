#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; Arguments are the call's own children after its paren (`.open`). A
#; `f({a: 1})` call names every argument inside one brace group, which is
#; declined rather than read as one positional argument.

(call_expression
  function: (expression
    (identifier) @call.callee)
  "(" @call.arguments.open) @call.definition

(call_expression
  function: (expression
    (member_expression
      property: (identifier) @call.callee))
  "(" @call.arguments.open) @call.definition

(call_argument
  "{") @argument.spread
