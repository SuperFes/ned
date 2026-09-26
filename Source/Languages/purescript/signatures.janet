#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Each equation of
#; a function is its own definition, its patterns matched by their text. A
#; type signature's arrows type the parameters in order, after any
#; `forall` and `C a =>` constraint; one that doesn't spell them out (a
#; synonym) is opaque.

(function
  name: (variable) @signature.name
  patterns: (patterns) @signature.parameters.rest) @signature.definition

((signature
  name: (variable) @signature.name
  (type_infix (type_operator) @_arrow) @signature.parameters.chain) @signature.type.curried
  (:eq? @_arrow "->"))

((signature
  name: (variable) @signature.name
  (type_infix
    (type_operator) @_constraint
    (type_infix (type_operator) @_arrow) @signature.parameters.chain)) @signature.type.curried
  (:eq? @_constraint "=>")
  (:eq? @_arrow "->"))

(signature
  name: (variable) @signature.name
  [(type_name) (type_apply) (type_infix)] @signature.parameters.opaque) @signature.type.curried

(signature (type_infix [(forall) (type_operator)] @parameter.skip))
(type_infix (type_infix [(forall) (type_operator)] @parameter.skip))
