#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). `initialize` is
#; called as `Widget.new(...)`.

((method_def
   name: (identifier) @signature.name
   params: (param_list) @signature.parameters) @signature.definition
 (:not-eq? @signature.name "initialize"))

((class_def
   name: (constant) @signature.callee
   (expressions
     (method_def
       name: (identifier) @signature.name
       params: (param_list) @signature.parameters) @signature.definition))
 (:eq? @signature.name "initialize"))

(param
  name: (identifier) @parameter.name) @parameter

(param
  name: (identifier)
  default: (_) @parameter.default) @parameter

[(splat_param) (double_splat_param) (block_param)] @parameter.variadic
