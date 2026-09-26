(create_function
  (object_reference (identifier) @signature.name .)
  (function_arguments) @signature.parameters) @signature.definition

(function_argument (identifier) @parameter.name) @parameter
(function_argument (identifier) @parameter.name (literal) @parameter.default .) @parameter
