#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; Arguments are the call's own children after its paren (`.open`).
#; `f (name: 1)` is named.

(method_call_expression
  (member_access_expression
    (identifier) @call.callee .)
  "(" @call.arguments.open) @call.definition

(object_creation_expression
  (type
    (symbol
      (identifier) @call.callee .))
  "(" @call.arguments.open) @call.definition

(argument
  (identifier) @argument.name
  ":") @argument.named
