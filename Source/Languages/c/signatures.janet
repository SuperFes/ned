#; change-signature: definitions (ned's own query -- see cpp/signatures.janet
#; for the capture convention and why only definitions with a body are
#; captured). Parameters come from each one's `declarator:` field.

(function_definition
  declarator: (function_declarator
    declarator: (identifier) @signature.name
    parameters: (parameter_list) @signature.parameters)) @signature.definition

(function_definition
  declarator: (pointer_declarator
    declarator: (function_declarator
      declarator: (identifier) @signature.name
      parameters: (parameter_list) @signature.parameters))) @signature.definition

(function_definition
  declarator: (pointer_declarator
    declarator: (pointer_declarator
      declarator: (function_declarator
        declarator: (identifier) @signature.name
        parameters: (parameter_list) @signature.parameters)))) @signature.definition
