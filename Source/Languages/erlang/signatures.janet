#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Each clause of a
#; function is its own definition; its patterns are matched by their text.
#; A `-spec` types the parameters of the function of its name and arity.

(function_clause
  name: (atom) @signature.name
  args: (expr_args) @signature.parameters) @signature.definition

(spec
  fun: (atom) @signature.name
  sigs: (type_sig args: (expr_args) @signature.parameters)) @signature.type
