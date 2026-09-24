#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `Widget.new(...)` reaches `initialize`; `f(b: 2)` names its argument.

((call
   method: (identifier) @call.callee
   arguments: (argument_list) @call.arguments) @call.definition
 (:not-eq? @call.callee "new"))

((call
   receiver: (constant) @call.callee
   method: (identifier) @_new
   arguments: (argument_list) @call.arguments) @call.definition
 (:eq? @_new "new"))

(named_expr
  name: (identifier) @argument.name) @argument.named

[(splat) (double_splat) (block_argument)] @argument.spread
