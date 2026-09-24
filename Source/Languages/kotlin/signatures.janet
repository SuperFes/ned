#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A function
#; parameter's `= default` is its sibling in this grammar, which Mode's
#; signatures closure attaches. A primary constructor is named after its class, as `Widget(...)`
#; calls it.

(function_declaration
  (simple_identifier) @signature.name
  (function_value_parameters) @signature.parameters) @signature.definition

(class_declaration
  (type_identifier) @signature.name
  (primary_constructor) @signature.parameters) @signature.definition

(parameter
  (simple_identifier) @parameter.name) @parameter

(class_parameter
  (simple_identifier) @parameter.name) @parameter

#; A class parameter holds its own default.
(class_parameter
  (simple_identifier)
  "="
  (_) @parameter.default) @parameter
