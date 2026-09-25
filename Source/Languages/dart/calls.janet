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

#; `: super(...)` and `: this(...)` call the unnamed constructor of the class
#; this one extends, or of this one.
(initializer_list_entry
  (super) @call.callee.base
  .
  (arguments) @call.arguments) @call.definition

(redirection
  (this) @call.callee.class
  .
  (arguments) @call.arguments) @call.definition

(class_definition
  name: (identifier) @call.class.name
  superclass: (superclass
    .
    (type_identifier) @call.base)) @call.class

(class_definition
  name: (identifier) @call.class.name) @call.class
