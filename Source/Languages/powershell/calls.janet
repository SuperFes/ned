# A function is called as a command, `F 1 2`; a method with parens.
(command (command_name) @call.callee (command_elements) @call.arguments.rest) @call.definition
(invokation_expression (member_name (simple_name) @call.callee) (argument_list) @call.arguments) @call.definition

(command_argument_sep) @argument.skip
(command_parameter) @argument.name @argument.named
(argument_expression_list) @argument.group
