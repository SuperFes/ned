(func_def name: (_) @signature.name (param_list) @signature.parameters) @signature.definition
(func_def name: (_) @signature.name "(" @signature.parameters.open . ")") @signature.definition

(param_list (identifier) @parameter.name @parameter)
