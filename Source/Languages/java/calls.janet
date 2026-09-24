#; change-signature: call sites (ned's own query -- see cpp/calls.janet).

(method_invocation
  name: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

(object_creation_expression
  type: [(type_identifier) @call.callee
         (generic_type (type_identifier) @call.callee)
         (scoped_type_identifier (type_identifier) @call.callee .)]
  arguments: (argument_list) @call.arguments) @call.definition
