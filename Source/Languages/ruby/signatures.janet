#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). `self` is
#; implicit, never a parameter. A `k:` parameter is passed by name; a
#; splat, double splat or `&block` makes the list variadic. `initialize` is
#; called as `Widget.new(...)`. A definition written without parens is
#; skipped: its list can't be rewritten in place.

((method
   name: (identifier) @signature.name
   parameters: (method_parameters) @signature.parameters) @signature.definition
 (:not-eq? @signature.name "initialize"))

(singleton_method
  name: (identifier) @signature.name
  parameters: (method_parameters) @signature.parameters) @signature.definition

((class
   name: (constant) @signature.callee
   body: (body_statement
     (method
       name: (identifier) @signature.name
       parameters: (method_parameters) @signature.parameters) @signature.definition))
 (:eq? @signature.name "initialize"))

(method_parameters
  (identifier) @parameter.name @parameter)

(optional_parameter
  name: (identifier) @parameter.name
  value: (_) @parameter.default) @parameter

(keyword_parameter
  name: (identifier) @parameter.name) @parameter.keyword

(keyword_parameter
  name: (identifier)
  value: (_) @parameter.default) @parameter.keyword

[(splat_parameter) (hash_splat_parameter) (block_parameter)] @parameter.variadic
