#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Each equation of
#; a function is its own definition, its patterns matched by their text. A
#; type signature's arrows type the parameters in order; one that doesn't
#; spell them out (a synonym, or `f, g :: ...`) is opaque.

(function
  name: (variable) @signature.name
  patterns: (patterns) @signature.parameters.rest) @signature.definition

(signature
  name: (variable) @signature.name
  type: [(function) @signature.parameters.chain
         (context type: (function) @signature.parameters.chain)
         (forall type: [(function) @signature.parameters.chain
                        (context type: (function) @signature.parameters.chain)])]) @signature.type.curried

(signature
  name: (variable) @signature.name
  type: (_) @signature.parameters.opaque) @signature.type.curried

(signature
  names: (binding_list (variable) @signature.name)
  type: (_) @signature.parameters.opaque) @signature.type.curried
