#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A constructor is
#; named after its class, as `new Widget(...)` spells it; an extension
#; method's `this` parameter is supplied by the object it's called on.

(method_declaration
  name: (identifier) @signature.name
  parameters: (parameter_list) @signature.parameters) @signature.definition

(constructor_declaration
  name: (identifier) @signature.name
  parameters: (parameter_list) @signature.parameters) @signature.definition

(local_function_statement
  name: (identifier) @signature.name
  parameters: (parameter_list) @signature.parameters) @signature.definition

(parameter
  name: (identifier) @parameter.name) @parameter

(parameter
  name: (identifier)
  "="
  (_) @parameter.default) @parameter

((parameter
   (modifier) @_this) @parameter.receiver
 (:eq? @_this "this"))

#; `params int[] rest` is no parameter node at all: the type and name sit
#; in the list itself.
(parameter_list
  type: (_) @parameter.variadic)
