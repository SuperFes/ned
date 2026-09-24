#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). V has no default
#; values, so a parameter can be reordered or dropped, not added.

(function_declaration
  name: (identifier) @signature.name
  signature: (signature
    parameters: (parameter_list) @signature.parameters)) @signature.definition

(parameter_declaration
  name: (identifier) @parameter.name) @parameter

(parameter_declaration
  variadic: _) @parameter.variadic
