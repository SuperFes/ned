#; change-signature: call sites (ned's own query -- see cpp/calls.janet).

(exprCall
  entity: (identifier) @call.callee
  args: (exprArgs) @call.arguments) @call.definition

(exprCall
  entity: (exprDot rhs: (identifier) @call.callee)
  args: (exprArgs) @call.arguments) @call.definition
