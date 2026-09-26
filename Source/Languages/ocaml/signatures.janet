#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A pattern is
#; matched by its text. `~x` is passed by its label, and so is an optional
#; `?x`, which a call may leave out -- as good as a default.

(let_binding
  pattern: (value_name) @signature.name
  .
  (parameter)) @signature.definition @signature.parameters.rest

(parameter pattern: (value_pattern) @parameter.name) @parameter
(parameter (typed_pattern pattern: (value_pattern) @parameter.name)) @parameter

(parameter "~" pattern: (_) @parameter.name) @parameter.keyword
(parameter (label_name) @parameter.name) @parameter.keyword
(parameter "?" pattern: (_) @parameter.name @parameter.default) @parameter.keyword
