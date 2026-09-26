[(subprogram_declaration
   [(function_specification function: (_) @signature.name (parameter_list_specification) @signature.parameters)
    (procedure_specification procedure: (_) @signature.name (parameter_list_specification) @signature.parameters)])
 (subprogram_definition
   [(function_specification function: (_) @signature.name (parameter_list_specification) @signature.parameters)
    (procedure_specification procedure: (_) @signature.name (parameter_list_specification) @signature.parameters)])]
  @signature.definition

(interface_list) @parameter.group
(interface_declaration (identifier_list . (identifier) @parameter.name .)) @parameter
(interface_declaration
  (identifier_list . (identifier) @parameter.name .)
  (simple_mode_indication (initialiser (_) @parameter.default .))) @parameter

# `a, b : integer` declares two parameters.
((interface_declaration (identifier_list (identifier) . (identifier)) @parameter.group) @parameter.group)
(interface_declaration (identifier_list (identifier) . (identifier)) (_) @parameter.skip .)
(identifier_list (identifier) @parameter.name @parameter)
