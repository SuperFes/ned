#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures).

(function_definition
  name: (name) @signature.name
  parameters: (parameters) @signature.parameters) @signature.definition

(parameters
  (identifier) @parameter.name @parameter)

(typed_parameter
  (identifier) @parameter.name) @parameter

(default_parameter
  (identifier) @parameter.name
  value: (_) @parameter.default) @parameter

(typed_default_parameter
  (identifier) @parameter.name
  value: (_) @parameter.default) @parameter
