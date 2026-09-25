#; change-signature: definitions and their parameters (ned's own query --
#; see python/signatures.janet, whose parameter rules Starlark shares).

(function_definition
  name: (identifier) @signature.name
  parameters: (parameters) @signature.parameters) @signature.definition

(parameters
  (identifier) @parameter.name @parameter)

(default_parameter
  name: (identifier) @parameter.name
  value: (_) @parameter.default) @parameter

(typed_parameter
  (identifier) @parameter.name) @parameter

(typed_default_parameter
  name: (identifier) @parameter.name
  value: (_) @parameter.default) @parameter

[(list_splat_pattern) (dictionary_splat_pattern)] @parameter.variadic

#; Parameters after a bare `*` are passed only by name.
(keyword_separator) @parameter.keyword.marker
