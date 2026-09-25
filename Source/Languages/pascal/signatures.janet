#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A group sharing
#; one type (`A, B: Integer`) is one entry with no single name, which a
#; change can drop but never match.

(declProc
  name: (identifier) @signature.name
  args: (declArgs) @signature.parameters) @signature.definition

(declArg
  .
  name: (identifier) @parameter.name
  .
  type: (_)) @parameter

(declArg
  .
  name: (identifier) @parameter.name
  .
  type: (_)
  defaultValue: (defaultValue . (_) . (_) @parameter.default)) @parameter

#; Names sharing a type (`A, B: Integer`) are a parameter each.
((declArg name: (identifier) . name: (identifier)) @parameter.group)
((declArg [(kVar) (kConst) (kOut) (kConstref) (type) (defaultValue)] @parameter.skip) @_grouped
 (:match? @_grouped ","))
((declArg name: (identifier) @parameter.name @parameter) @_grouped
 (:match? @_grouped ","))
