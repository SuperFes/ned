(function_call_expression function: (function) @call.callee "(" @call.arguments.open) @call.definition
(method_call_expression invocant: (_) @call.receiver method: (method) @call.callee "(" @call.arguments.open) @call.definition

# Without parens the list can't be rewritten in place; found, so it's declined.
(ambiguous_function_call_expression function: (function) @call.callee arguments: (_) @call.arguments) @call.definition

(list_expression) @argument.group
