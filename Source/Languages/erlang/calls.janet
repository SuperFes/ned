#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `m:f(...)` is a `remote` around the call itself. An export names the
#; function by its arity (@call.arity); `fun f/2` passes it on to be called
#; elsewhere (@call.arity.value).

(call
  expr: (atom) @call.callee
  args: (expr_args) @call.arguments) @call.definition

[(internal_fun fun: (atom) @call.callee arity: (arity value: (_) @call.arity.value))
 (external_fun fun: (atom) @call.callee arity: (arity value: (_) @call.arity.value))] @call.definition

(export_attribute
  funs: (fa fun: (atom) @call.callee arity: (arity value: (_) @call.arity)) @call.definition)
