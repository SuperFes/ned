#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A function's
#; parameters are its own children with no list node around them, so the
#; list is named by its opening paren (`.open`); `returns (...)` is a node of
#; its own and never confused with it.

(function_definition
  name: (identifier) @signature.name
  "(" @signature.parameters.open) @signature.definition

(modifier_definition
  name: (identifier) @signature.name
  "(" @signature.parameters.open) @signature.definition

#; A constructor is called as its contract: `new Vault(...)`, or a base
#; constructor's arguments in `is Vault(...)` and `constructor() Vault(...)`.
(contract_declaration
  name: (identifier) @signature.callee
  body: (contract_body
    (constructor_definition
      "constructor" @signature.name
      .
      "(" @signature.parameters.open) @signature.definition))

(parameter
  name: (identifier) @parameter.name) @parameter
