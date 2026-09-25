#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A spec and its
#; body are both signatures. A group sharing one type (`A, B : Integer`) is
#; one entry with no single name, which a change can drop but never match.

(procedure_specification
  name: (identifier) @signature.name
  (formal_part) @signature.parameters) @signature.definition

(function_specification
  name: (identifier) @signature.name
  (formal_part) @signature.parameters) @signature.definition

(parameter_specification
  .
  (identifier) @parameter.name
  .
  subtype_mark: (_)) @parameter

(parameter_specification
  .
  (identifier) @parameter.name
  .
  (non_empty_mode)) @parameter

(parameter_specification
  .
  (identifier) @parameter.name
  (expression) @parameter.default) @parameter

(parameter_specification
  (identifier)
  .
  (identifier)) @parameter
