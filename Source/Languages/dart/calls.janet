#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A call is no node here: the callee is followed by a selector holding the
#; arguments. `f(b: 2)` names its argument.

((identifier) @call.callee
 .
 (selector
   (argument_part
     (arguments) @call.arguments)) @call.definition)

((selector
   (unconditional_assignable_selector
     (identifier) @call.callee)) @call.receiver
 .
 (selector
   (argument_part
     (arguments) @call.arguments)) @call.definition)

(new_expression
  (type_identifier) @call.callee
  (arguments) @call.arguments) @call.definition

(const_object_expression
  (type_identifier) @call.callee
  (arguments) @call.arguments) @call.definition

(named_argument
  (label
    (identifier) @argument.name)) @argument.named
