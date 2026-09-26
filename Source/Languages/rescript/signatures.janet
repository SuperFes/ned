(let_binding
  pattern: (value_identifier) @signature.name
  body: (function parameters: (formal_parameters) @signature.parameters)) @signature.definition

(parameter . (value_identifier) @parameter.name) @parameter
(parameter (labeled_parameter (value_identifier) @parameter.name)) @parameter.keyword
(parameter (labeled_parameter (value_identifier) @parameter.name default_value: (_) @parameter.default)) @parameter.keyword
