(tf_call (simple_identifier) @call.callee . (list_of_arguments_parent) @call.arguments) @call.definition

(list_of_arguments_parent "." . (_) @argument.name @argument.named)

# A call binding its arguments by name, `f(.a(1))`, parses as a let expression.
(let_expression (simple_identifier) @call.callee (let_list_of_arguments) @call.arguments) @call.definition
(let_list_of_arguments (simple_identifier) @argument.name . (let_actual_arg) @argument.named)
