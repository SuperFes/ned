#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures).
#;
#; A method's first parameter is its receiver: `obj.m(a)` supplies `self`,
#; `Cls.m(obj, a)` passes it; a classmethod's `cls` comes from either; a
#; staticmethod has none. `__init__` is called as `Widget(...)`, which
#; supplies `self` every time. A bare `*` or `/` separator names nothing, so
#; a change to a list holding one declines.

((function_definition
   name: (identifier) @signature.name
   parameters: (parameters) @signature.parameters) @signature.definition
 (:not-eq? @signature.name "__init__"))

((class_definition
   name: (identifier) @signature.callee
   body: (block
     (function_definition
       name: (identifier) @signature.name
       parameters: (parameters) @signature.parameters) @signature.definition))
 (:eq? @signature.name "__init__"))

(parameters
  (identifier) @parameter.name @parameter)

(default_parameter
  name: (identifier) @parameter.name
  value: (_) @parameter.default) @parameter

(typed_parameter
  (identifier) @parameter.name) @parameter

(typed_default_parameter
  name: (identifier) @parameter.name
  value: (_) @parameter.default) @parameter

[(list_splat_pattern) (dictionary_splat_pattern)] @parameter.variadic

(class_definition
  body: (block
    (function_definition
      parameters: (parameters
        .
        (identifier) @parameter.receiver))))

((class_definition
   body: (block
     (decorated_definition
       (decorator (identifier) @_decorator)
       definition: (function_definition
         parameters: (parameters
           .
           (identifier) @parameter.receiver)))))
 (:not-any-of? @_decorator "classmethod" "staticmethod"))

((class_definition
   body: (block
     (decorated_definition
       (decorator (identifier) @_decorator)
       definition: (function_definition
         parameters: (parameters
           .
           (identifier) @parameter.receiver.any)))))
 (:eq? @_decorator "classmethod"))

((class_definition
   body: (block
     (function_definition
       name: (identifier) @_init
       parameters: (parameters
         .
         (identifier) @parameter.receiver.always))))
 (:eq? @_init "__init__"))
