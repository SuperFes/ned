(call_expression
  function: [(value_identifier) @call.callee
             (value_identifier_path (value_identifier) @call.callee .)]
  arguments: (arguments) @call.arguments) @call.definition

(labeled_argument label: (value_identifier) @argument.name) @argument.named
