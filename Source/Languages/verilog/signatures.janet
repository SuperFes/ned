# Functions only: a task is called as a statement, `t(3);`, which parses as
# an instantiation, so its calls can't be found.
(function_body_declaration (function_identifier) @signature.name (tf_port_list) @signature.parameters) @signature.definition
(function_body_declaration (function_identifier) @signature.name "(" @signature.parameters.open . ")") @signature.definition

(tf_port_list (tf_port_item1 (port_identifier) @parameter.name) @parameter)
(tf_port_list (tf_port_item1 (port_identifier) @parameter.name "=" (expression) @parameter.default) @parameter)
