(function_statement (function_name) @signature.name (function_parameter_declaration) @signature.parameters) @signature.definition
(function_statement
  (function_name) @signature.name
  (script_block (param_block "(" @signature.parameters.open))) @signature.definition
(class_method_definition (simple_name) @signature.name "(" @signature.parameters.open) @signature.definition

(parameter_list) @parameter.group
(script_parameter (variable) @parameter.name) @parameter
(script_parameter (variable) @parameter.name (script_parameter_default (_) @parameter.default)) @parameter
(class_method_parameter_list) @parameter.group
(class_method_parameter (variable) @parameter.name) @parameter
