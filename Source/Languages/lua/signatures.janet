#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A `M:m(...)`
#; definition's `self` is implicit; one written `M.m(self, ...)` has it as a
#; receiver, which `obj:m(...)` supplies.

(function_declaration
  name: [(identifier) @signature.name
         (dot_index_expression field: (identifier) @signature.name)
         (method_index_expression method: (identifier) @signature.name)]
  parameters: (parameters) @signature.parameters) @signature.definition

(variable_declaration
  (assignment_statement
    (variable_list name: (identifier) @signature.name)
    (expression_list value: (function_definition parameters: (parameters) @signature.parameters)))) @signature.definition

(parameters
  name: (identifier) @parameter.name @parameter)

((parameters
   .
   name: (identifier) @parameter.receiver)
 (:eq? @parameter.receiver "self"))

(parameters
  (vararg_expression) @parameter.variadic)
