#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A call, an indexed component and a type conversion read alike; only a
#; name some signature has is ever rewritten.

(procedure_call_statement
  name: [(identifier) @call.callee
         (selected_component selector_name: (identifier) @call.callee)]
  (actual_parameter_part) @call.arguments) @call.definition

(function_call
  name: [(identifier) @call.callee
         (selected_component selector_name: (identifier) @call.callee)]
  (actual_parameter_part) @call.arguments) @call.definition

(parameter_association
  (component_choice_list . (identifier) @argument.name .)) @argument.named
