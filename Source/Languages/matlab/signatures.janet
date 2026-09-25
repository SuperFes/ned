#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures).

(function_definition
  name: (identifier) @signature.name
  (function_arguments) @signature.parameters) @signature.definition

(function_arguments
  arguments: (identifier) @parameter.name @parameter)

((function_arguments
   arguments: (identifier) @parameter.variadic)
 (:eq? @parameter.variadic "varargin"))
