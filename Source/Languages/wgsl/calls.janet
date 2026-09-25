#; change-signature: call sites (ned's own query -- see cpp/calls.janet). A
#; call and a type constructor share one node; only a call's name matches a
#; function's.

(type_constructor_or_function_call_expression
  (type_declaration . (identifier) @call.callee .)
  (argument_list_expression) @call.arguments) @call.definition
