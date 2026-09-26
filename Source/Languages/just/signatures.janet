# A recipe without parameters has no list to add one to.
(recipe (recipe_header name: (identifier) @signature.name (parameters) @signature.parameters.rest)) @signature.definition

(parameter name: (identifier) @parameter.name) @parameter
(parameter name: (identifier) @parameter.name default: (value) @parameter.default) @parameter
(variadic_parameter) @parameter.variadic
