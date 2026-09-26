# Only a sub with a signature has a parameter list to rewrite; one that
# unpacks @_ has none.
(subroutine_declaration_statement
  name: (bareword) @signature.name
  (signature) @signature.parameters) @signature.definition

(signature (mandatory_parameter) @parameter.name @parameter)
(signature (optional_parameter (scalar) @parameter.name default: (_) @parameter.default) @parameter)
(signature (slurpy_parameter) @parameter.variadic)

((signature . (mandatory_parameter) @parameter.receiver.any)
 (:any-of? @parameter.receiver.any "$self" "$class"))
