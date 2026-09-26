#; change-signature: a `val`'s arrows type its implementation's parameters
#; in order (ned's own query -- see ocaml/signatures.janet). One that
#; doesn't spell them out (a synonym) is opaque.

(value_specification
  (value_name) @signature.name
  (function_type) @signature.parameters.chain) @signature.type.curried

(value_specification
  (value_name) @signature.name
  [(type_constructor_path) (constructed_type) (type_variable) (parenthesized_type)] @signature.parameters.opaque) @signature.type.curried
