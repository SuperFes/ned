#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures).

(function_declaration
  name: (identifier) @signature.name
  parameters: (parameter_list) @signature.parameters) @signature.definition

(parameter
  (variable_identifier_declaration name: (identifier) @parameter.name)) @parameter
