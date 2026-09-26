# Mixins only: a @function's call arguments are CSS values, where `1px solid`
# is one argument written as two, so its calls can't be counted.
(mixin_statement name: (identifier) @signature.name (parameters) @signature.parameters) @signature.definition

(parameter (variable) @parameter.name) @parameter
(parameter (variable) @parameter.name default: (_) @parameter.default) @parameter
