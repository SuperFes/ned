#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A labelled
#; parameter is passed by its label, so the label is its name here.

(function
  name: (identifier) @signature.name
  parameters: (function_parameters) @signature.parameters) @signature.definition

(function_parameter
  !label
  name: (identifier) @parameter.name) @parameter

(function_parameter
  label: (label) @parameter.name) @parameter
