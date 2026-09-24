#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `obj.m(...)` supplies a method's `self`; `Cls.m(...)` -- a capitalized
#; object -- supplies only a classmethod's `cls`. `f(b=2)` names its
#; argument, `f(*xs)`/`f(**kw)` spread.

(call
  function: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

((call
   function: (attribute
     object: (_) @_object
     attribute: (identifier) @call.callee) @call.receiver
   arguments: (argument_list) @call.arguments) @call.definition
 (:not-match? @_object "^[A-Z]"))

((call
   function: (attribute
     object: (identifier) @_object
     attribute: (identifier) @call.callee) @call.receiver.type
   arguments: (argument_list) @call.arguments) @call.definition
 (:match? @_object "^[A-Z]"))

(keyword_argument) @argument.named

[(list_splat) (dictionary_splat)] @argument.spread
