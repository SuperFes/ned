#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Only a curried
#; definition's first parameter list is changed -- the one `f(a)(b)`'s
#; inner call passes. A class's parameters are its constructor's, called as
#; `Widget(...)` or `new Widget(...)`.

(function_definition
  name: (identifier) @signature.name
  .
  parameters: (parameters) @signature.parameters) @signature.definition

(function_definition
  name: (identifier) @signature.name
  .
  type_parameters: (_)
  .
  parameters: (parameters) @signature.parameters) @signature.definition

(function_declaration
  name: (identifier) @signature.name
  .
  parameters: (parameters) @signature.parameters) @signature.definition

(class_definition
  name: (identifier) @signature.name
  class_parameters: (class_parameters) @signature.parameters) @signature.definition

[(parameter name: (identifier) @parameter.name)
 (class_parameter name: (identifier) @parameter.name)] @parameter

[(parameter name: (identifier) default_value: (_) @parameter.default)
 (class_parameter name: (identifier) default_value: (_) @parameter.default)] @parameter

((parameter
   type: (repeated_parameter_type)) @parameter.variadic)
