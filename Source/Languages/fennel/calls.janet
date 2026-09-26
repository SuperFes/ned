(list . (symbol) @call.callee) @call.definition @call.arguments.rest

# `(obj:m a)` passes `obj` first.
(list . (multi_symbol_method (symbol) @call.callee .)) @call.definition @call.arguments.rest @call.receiver.first
