#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures).

(function_definition
  function: (identifier) @signature.name
  parameters: (parameter_list) @signature.parameters) @signature.definition

(parameter
  name: (identifier) @parameter.name) @parameter

(parameter
  name: (identifier)
  value: (_) @parameter.default) @parameter
