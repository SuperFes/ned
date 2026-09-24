#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). JavaScript's
#; shapes plus TypeScript's own: typed parameters, `b?` optional ones,
#; interface and overload signatures, and the `this:` annotation, which no
#; call ever passes.

(function_declaration
  name: (identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

(function_signature
  name: (identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

(generator_function_declaration
  name: (identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

((method_definition
   name: (property_identifier) @signature.name
   parameters: (formal_parameters) @signature.parameters) @signature.definition
 (:not-eq? @signature.name "constructor"))

(method_signature
  name: (property_identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

(abstract_method_signature
  name: (property_identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

((class_declaration
   name: (type_identifier) @signature.callee
   body: (class_body
     (method_definition
       name: (property_identifier) @signature.name
       parameters: (formal_parameters) @signature.parameters) @signature.definition))
 (:eq? @signature.name "constructor"))

(variable_declarator
  name: (identifier) @signature.name
  value: [(arrow_function parameters: (formal_parameters) @signature.parameters)
          (function_expression parameters: (formal_parameters) @signature.parameters)]) @signature.definition

(required_parameter
  pattern: (identifier) @parameter.name) @parameter

(required_parameter
  pattern: (identifier)
  value: (_) @parameter.default) @parameter

(optional_parameter
  pattern: (identifier) @parameter.name) @parameter

(optional_parameter
  pattern: (identifier)
  value: (_) @parameter.default) @parameter

(required_parameter
  pattern: (rest_pattern)) @parameter.variadic

(required_parameter
  pattern: (this) @parameter.name) @parameter.receiver.always
