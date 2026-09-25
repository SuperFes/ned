#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Optional
#; parameters sit in a `[...]` or `{...}` group, read through as the list's
#; own; those in `{...}` are passed by name. A default is the parameter's
#; sibling after `=`. A constructor is named after its class.

(function_signature
  name: (identifier) @signature.name
  (formal_parameter_list) @signature.parameters) @signature.definition

#; A named constructor (`Box.square(...)`) is named by its last part.
(constructor_signature
  name: (identifier) @signature.name
  .
  parameters: (formal_parameter_list) @signature.parameters) @signature.definition

(formal_parameter
  name: (identifier) @parameter.name) @parameter

(formal_parameter
  (constructor_param
    (identifier) @parameter.name)) @parameter

(optional_formal_parameters) @parameter.group

(optional_formal_parameters
  "{"
  (formal_parameter) @parameter.keyword)
