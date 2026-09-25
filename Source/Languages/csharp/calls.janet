#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f(b: 2)` names its argument. A member call's object supplies an
#; extension method's `this` parameter.

(invocation_expression
  function: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

(invocation_expression
  function: (generic_name (identifier) @call.callee)
  arguments: (argument_list) @call.arguments) @call.definition

(invocation_expression
  function: (member_access_expression
    name: (identifier) @call.callee) @call.receiver
  arguments: (argument_list) @call.arguments) @call.definition

(object_creation_expression
  type: [(identifier) @call.callee
         (generic_name (identifier) @call.callee)
         (qualified_name name: (identifier) @call.callee)]
  arguments: (argument_list) @call.arguments) @call.definition

(argument
  name: (identifier)) @argument.named

#; `: base(...)` and `: this(...)` call a constructor of the class this one
#; extends, or of this one. The first base in the list is the class.
(constructor_initializer
  "base" @call.callee.base
  (argument_list) @call.arguments) @call.definition

(constructor_initializer
  "this" @call.callee.class
  (argument_list) @call.arguments) @call.definition

(class_declaration
  name: (identifier) @call.class.name
  (base_list
    .
    [(identifier) @call.base
     (generic_name (identifier) @call.base)
     (qualified_name name: (identifier) @call.base)])) @call.class

(class_declaration
  name: (identifier) @call.class.name) @call.class
(struct_declaration
  name: (identifier) @call.class.name) @call.class
(record_declaration
  name: (identifier) @call.class.name) @call.class
