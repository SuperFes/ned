#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). `a, b: int` names
#; two parameters in one node; it's left undescribed, so a change to it
#; declines (go/signatures.janet does the same).

(procedure_declaration
  (identifier) @signature.name
  (procedure
    (parameters) @signature.parameters)) @signature.definition

(parameter
  .
  (identifier) @parameter.name
  .
  (type)) @parameter

(default_parameter
  .
  (identifier) @parameter.name
  (_) @parameter.default .) @parameter
