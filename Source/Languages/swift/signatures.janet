#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). A function's
#; parameters are its own children with no list node around them, so the
#; list is named by the paren after the name (`.open`). A default sits after
#; the parameter as a sibling, which Mode's signatures closure attaches.
#; Every argument is written with its parameter's label -- the external name,
#; else the name, none for `_` -- so a new parameter's default is too.

(function_declaration
  name: (simple_identifier) @signature.name
  .
  "(" @signature.parameters.open) @signature.definition

(function_declaration
  name: (simple_identifier) @signature.name
  .
  (type_parameters)
  .
  "(" @signature.parameters.open) @signature.definition

#; An initializer is called as its type: `Box(w: 1, h: 2)`.
(class_declaration
  name: (type_identifier) @signature.callee
  body: (class_body
    (init_declaration
      "init" @signature.name
      .
      "(" @signature.parameters.open) @signature.definition))

(class_declaration
  name: (user_type
    (type_identifier) @signature.callee
    .)
  body: (class_body
    (init_declaration
      "init" @signature.name
      .
      "(" @signature.parameters.open) @signature.definition))

((parameter
   external_name: (simple_identifier) @parameter.label
   name: (simple_identifier) @parameter.name) @parameter
 (:not-eq? @parameter.label "_"))

(parameter
  external_name: (simple_identifier)
  name: (simple_identifier) @parameter.name) @parameter

(parameter
  .
  name: (simple_identifier) @parameter.label @parameter.name) @parameter
