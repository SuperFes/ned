#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A method's
#; parameters are its own children with no list node around them, so the
#; list is named by the paren right after the name (`.open`) -- anchored,
#; since a `requires (...)` clause has parens of its own. A creation method
#; is named after its class, as `new Widget (...)` calls it.

(method_declaration
  (symbol
    (identifier) @signature.name .)
  .
  "(" @signature.parameters.open) @signature.definition

(method_declaration
  (symbol
    (identifier) @signature.name .)
  .
  (type_arguments)
  .
  "(" @signature.parameters.open) @signature.definition

(creation_method_declaration
  (symbol
    (identifier) @signature.name .)
  .
  "(" @signature.parameters.open) @signature.definition

(parameter
  (identifier) @parameter.name) @parameter

(parameter
  "="
  .
  (_) @parameter.default) @parameter

(parameter
  "...") @parameter.variadic

(parameter
  "params") @parameter.variadic
