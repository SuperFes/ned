(procedure name: (simple_word) @signature.name arguments: (arguments) @signature.parameters) @signature.definition

(argument name: (simple_word) @parameter.name) @parameter
(argument name: (simple_word) @parameter.name default: (_) @parameter.default) @parameter
((arguments (argument name: (simple_word) @_args) @parameter.variadic .) (:eq? @_args "args"))
