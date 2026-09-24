#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A class's
#; `constructor` is called as `new Widget(...)`. A destructuring parameter
#; has no one name, so a change to it declines.

(function_declaration
  name: (identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

(generator_function_declaration
  name: (identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

((method_definition
   name: (property_identifier) @signature.name
   parameters: (formal_parameters) @signature.parameters) @signature.definition
 (:not-eq? @signature.name "constructor"))

((class_declaration
   name: (identifier) @signature.callee
   body: (class_body
     (method_definition
       name: (property_identifier) @signature.name
       parameters: (formal_parameters) @signature.parameters) @signature.definition))
 (:eq? @signature.name "constructor"))

(variable_declarator
  name: (identifier) @signature.name
  value: [(arrow_function parameters: (formal_parameters) @signature.parameters)
          (function_expression parameters: (formal_parameters) @signature.parameters)]) @signature.definition

(formal_parameters
  (identifier) @parameter.name @parameter)

(assignment_pattern
  left: (identifier) @parameter.name
  right: (_) @parameter.default) @parameter

(rest_pattern) @parameter.variadic
