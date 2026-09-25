#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A method's
#; receiver is its own list, never an argument. `x, y int` names two
#; parameters in one node; it's left undescribed, so a change to it declines.

(function_declaration
  name: (identifier) @signature.name
  parameters: (parameter_list) @signature.parameters) @signature.definition

(method_declaration
  name: (field_identifier) @signature.name
  parameters: (parameter_list) @signature.parameters) @signature.definition

(parameter_declaration
  .
  name: (identifier) @parameter.name
  .
  type: (_)) @parameter

#; Names sharing a type (`a, b int`) are a parameter each; the type is theirs.
((parameter_declaration
   type: (_) @parameter.skip) @parameter.group
 (:match? @parameter.group ","))
((parameter_declaration
   name: (identifier) @parameter.name @parameter) @_grouped
 (:match? @_grouped ","))
(variadic_parameter_declaration) @parameter.variadic
