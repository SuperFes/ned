#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Patterns are
#; matched by their text. A type annotation's arrows type the parameters in
#; order.

(value_declaration
  (function_declaration_left
    .
    (lower_case_identifier) @signature.name) @signature.parameters.rest) @signature.definition

(type_annotation
  name: (lower_case_identifier) @signature.name
  typeExpression: (type_expression) @signature.parameters.chain) @signature.type.curried

(type_expression (arrow) @parameter.skip)
