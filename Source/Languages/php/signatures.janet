#; change-signature: definitions and their parameters (ned's own query --
#; see cpp/signatures.janet for the capture convention). Parameters are
#; described here rather than by a declarator walk: @parameter is the whole
#; parameter, @parameter.name its variable, @parameter.default its default,
#; @parameter.variadic a `...$rest`.

(function_definition
  name: (name) @signature.name
  parameters: (formal_parameters) @signature.parameters) @signature.definition

((method_declaration
   name: (name) @signature.name
   parameters: (formal_parameters) @signature.parameters) @signature.definition
 (:not-eq? @signature.name "__construct"))

#; A constructor is called as `new Widget(...)`: its callers spell the class.
((class_declaration
   name: (name) @signature.callee
   body: (declaration_list
     (method_declaration
       name: (name) @signature.name
       parameters: (formal_parameters) @signature.parameters) @signature.definition))
 (:eq? @signature.name "__construct"))

(simple_parameter
  name: (variable_name) @parameter.name
  default_value: (_) @parameter.default) @parameter

(simple_parameter
  name: (variable_name) @parameter.name
  !default_value) @parameter

(property_promotion_parameter
  name: (variable_name) @parameter.name
  default_value: (_) @parameter.default) @parameter

(property_promotion_parameter
  name: (variable_name) @parameter.name
  !default_value) @parameter

(variadic_parameter) @parameter.variadic
