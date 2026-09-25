#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). `local f(a) = ...`
#; and an object's `f(a):: ...` field.

(bind
  function: (id) @signature.name
  params: (params) @signature.parameters) @signature.definition

(field
  (fieldname (id) @signature.name)
  (params) @signature.parameters) @signature.definition

(param
  identifier: (id) @parameter.name) @parameter

(param
  identifier: (id) @parameter.name
  value: (_) @parameter.default) @parameter
