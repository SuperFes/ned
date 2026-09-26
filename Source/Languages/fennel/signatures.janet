(fn name: (symbol) @signature.name (parameters) @signature.parameters) @signature.definition
(lambda name: (symbol) @signature.name (parameters) @signature.parameters) @signature.definition

(parameters (binding (symbol) @parameter.name) @parameter)
((parameters (binding (symbol) @parameter.variadic)) (:eq? @parameter.variadic "..."))
