#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). `self` is a
#; receiver: `x.m(a)` supplies it, `T::m(x, a)` passes it.

(function_item
  name: (identifier) @signature.name
  parameters: (parameters) @signature.parameters) @signature.definition

(function_signature_item
  name: (identifier) @signature.name
  parameters: (parameters) @signature.parameters) @signature.definition

(parameter
  pattern: (identifier) @parameter.name) @parameter

(parameter
  pattern: (mut_pattern (identifier) @parameter.name)) @parameter

(self_parameter
  (self) @parameter.name) @parameter.receiver

(variadic_parameter) @parameter.variadic
