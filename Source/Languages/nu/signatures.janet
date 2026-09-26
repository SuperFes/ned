(decl_def (cmd_identifier) @signature.name (parameter_bracks) @signature.parameters) @signature.definition

(parameter (identifier) @parameter.name) @parameter
(parameter (identifier) @parameter.name (param_value (_) @parameter.default)) @parameter
(parameter (param_long_flag (long_flag_identifier) @parameter.name)) @parameter.keyword
(parameter (param_rest)) @parameter.variadic
