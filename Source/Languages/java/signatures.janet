#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A constructor is
#; named after its class, as `new Widget(...)` spells it.

(method_declaration
  name: (identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

(constructor_declaration
  name: (identifier) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

(formal_parameter
  name: (identifier) @parameter.name) @parameter

(spread_parameter) @parameter.variadic
