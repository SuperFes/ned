#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Only the
#; `function ... end` form; a parameter after `;` is keyword-only but reads
#; as positional here, so a call passing one by name is declined.

(function_definition
  (signature
    (call_expression
      (identifier) @signature.name
      (argument_list) @signature.parameters))) @signature.definition

(signature
  (call_expression
    (argument_list
      (identifier) @parameter.name @parameter)))

(signature
  (call_expression
    (argument_list
      (typed_expression
        .
        (identifier) @parameter.name) @parameter)))

(signature
  (call_expression
    (argument_list
      (assignment
        .
        (identifier) @parameter.name
        (operator)
        (_) @parameter.default .) @parameter)))

(signature
  (call_expression
    (argument_list
      (splat_expression) @parameter.variadic)))
