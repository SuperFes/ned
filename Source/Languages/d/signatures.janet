#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A default sits
#; after `=` inside its parameter.

(function_declaration
  (identifier) @signature.name
  .
  (parameters) @signature.parameters) @signature.definition

(parameter
  (identifier) @parameter.name) @parameter

(parameter
  (identifier)
  "="
  (_) @parameter.default) @parameter

((parameters
   "...") @parameter.variadic)
