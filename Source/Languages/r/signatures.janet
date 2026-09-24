#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A function is
#; named by what it's assigned to.

(binary_operator
  lhs: (identifier) @signature.name
  rhs: (function_definition
    parameters: (parameters) @signature.parameters)) @signature.definition

(parameter
  name: (identifier) @parameter.name) @parameter

(parameter
  name: (identifier)
  default: (_) @parameter.default) @parameter

(parameter
  name: (dots)) @parameter.variadic
