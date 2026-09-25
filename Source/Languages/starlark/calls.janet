#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; Starlark has no methods of its own: `module.f(...)` is a plain call to f.

(call
  function: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

(call
  function: (attribute attribute: (identifier) @call.callee)
  arguments: (argument_list) @call.arguments) @call.definition

(keyword_argument
  name: (identifier) @argument.name) @argument.named

[(list_splat) (dictionary_splat)] @argument.spread
