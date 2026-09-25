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

#; A constructor reached through its class: `super().__init__(...)` in a
#; class whose first base is Widget, or `Widget.__init__(self, ...)`.
((call
   function: (attribute
     object: (call
       function: (identifier) @_super) @call.callee.base
     attribute: (identifier) @_name) @call.receiver
   arguments: (argument_list) @call.arguments) @call.definition
 (:eq? @_super "super")
 (:eq? @_name "__init__"))

((call
   function: (attribute
     object: (identifier) @call.callee
     attribute: (identifier) @_name) @call.receiver.explicit
   arguments: (argument_list) @call.arguments) @call.definition
 (:eq? @_name "__init__"))

(class_definition
  superclasses: (argument_list
    .
    [(identifier) @call.base
     (attribute attribute: (identifier) @call.base)])) @call.class

(keyword_argument
  name: (identifier) @argument.name) @argument.named

[(list_splat) (dictionary_splat)] @argument.spread
