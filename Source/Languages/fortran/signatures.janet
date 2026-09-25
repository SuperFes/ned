#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Dummy arguments
#; are names only; their types are declared in the body, which a change
#; leaves as written.

(subroutine
  (subroutine_statement
    name: (name) @signature.name
    parameters: (parameters) @signature.parameters)) @signature.definition

(function
  (function_statement
    name: (name) @signature.name
    parameters: (parameters) @signature.parameters)) @signature.definition

(parameters
  (identifier) @parameter.name @parameter)
