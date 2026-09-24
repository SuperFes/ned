#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). `obj.add(3)` is
#; `add(obj, 3)`, so a routine's first parameter is a receiver the dot
#; supplies -- and has to stay first. `a, b: int` names two parameters in
#; one node; it's left undescribed, so a change to it declines.

(proc_declaration
  name: (identifier) @signature.name
  parameters: (parameter_declaration_list) @signature.parameters) @signature.definition

(func_declaration
  name: (identifier) @signature.name
  parameters: (parameter_declaration_list) @signature.parameters) @signature.definition

(method_declaration
  name: (identifier) @signature.name
  parameters: (parameter_declaration_list) @signature.parameters) @signature.definition

(parameter_declaration
  (symbol_declaration_list
    .
    (symbol_declaration
      name: (identifier) @parameter.name)
    .)) @parameter

(parameter_declaration
  (symbol_declaration_list
    .
    (symbol_declaration)
    .)
  value: (_) @parameter.default) @parameter

(parameter_declaration_list
  .
  (parameter_declaration) @parameter.receiver)
