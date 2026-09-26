# The name is the first argument and the parameters are the rest.
(function_def (function_command (argument_list . (argument) @signature.name) @signature.parameters.rest)) @signature.definition
(macro_def (macro_command (argument_list . (argument) @signature.name) @signature.parameters.rest)) @signature.definition

(argument_list . (argument) (argument) @parameter.name @parameter)
