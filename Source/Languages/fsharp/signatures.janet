#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A pattern is
#; matched by its text, a typed one by its name.

(function_or_value_defn
  (function_declaration_left
    (identifier) @signature.name
    (argument_patterns) @signature.parameters.rest)) @signature.definition

(argument_patterns
  (paren_pattern
    (typed_pattern
      .
      (identifier_pattern (long_identifier_or_op (identifier) @parameter.name)))) @parameter)
