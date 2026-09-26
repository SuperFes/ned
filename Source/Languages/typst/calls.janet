# A `#let f(x) = ...` names its parameters with a call; it isn't one.
((call item: (ident) @call.callee (group) @call.arguments) @call.definition
 (:not-has-parent? @call.definition let))
(let (_) . (call item: (ident) @call.callee (group) @call.arguments) @call.definition)

(group (tagged field: (ident) @argument.name) @argument.named)
(group (elude) @argument.spread)
