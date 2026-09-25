#; change-signature: call sites (ned's own query -- see cpp/calls.janet).

(method_invocation
  name: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

(object_creation_expression
  type: [(type_identifier) @call.callee
         (generic_type (type_identifier) @call.callee)
         (scoped_type_identifier (type_identifier) @call.callee .)]
  arguments: (argument_list) @call.arguments) @call.definition

#; `super(...)` and `this(...)` call a constructor of the class this one
#; extends, or of this one.
(explicit_constructor_invocation
  constructor: (super) @call.callee.base
  arguments: (argument_list) @call.arguments) @call.definition

(explicit_constructor_invocation
  constructor: (this) @call.callee.class
  arguments: (argument_list) @call.arguments) @call.definition

(class_declaration
  name: (identifier) @call.class.name
  superclass: (superclass
    [(type_identifier) @call.base
     (generic_type (type_identifier) @call.base)
     (scoped_type_identifier (type_identifier) @call.base .)])) @call.class

(class_declaration
  name: (identifier) @call.class.name) @call.class
(enum_declaration
  name: (identifier) @call.class.name) @call.class
(record_declaration
  name: (identifier) @call.class.name) @call.class
