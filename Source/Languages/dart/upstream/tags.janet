#; ned: two upstream @reference.* clauses were dropped -- they quantify
#; bare tokens, outside QueryMatcher's measured scope, and the symbol gutter
#; reads definitions only.

(class_definition
  name: (identifier) @name) @definition.class

(method_signature
  (function_signature)) @definition.method

(type_alias
  (type_identifier) @name) @definition.type

(method_signature
(getter_signature
  name: (identifier) @name)) @definition.method

(method_signature
(setter_signature
  name: (identifier) @name)) @definition.method 

(method_signature
  (function_signature
  name: (identifier) @name)) @definition.method

(method_signature
  (factory_constructor_signature
    (identifier) @name)) @definition.method

(method_signature
  (constructor_signature
  name: (identifier) @name)) @definition.method

(method_signature
  (operator_signature)) @definition.method

(method_signature) @definition.method

(mixin_declaration
  (mixin)
  (identifier) @name) @definition.mixin

(extension_declaration
  name: (identifier) @name) @definition.extension


(new_expression
  (type_identifier) @name) @reference.class

(enum_declaration
  name: (identifier) @name) @definition.enum

(function_signature
  name: (identifier) @name) @definition.function 


(assignment_expression
  left: (assignable_expression 
		  (identifier)
		  (unconditional_assignable_selector
			"."
			(identifier) @name))) @reference.call

(assignment_expression
  left: (assignable_expression 
		  (identifier)
		  (conditional_assignable_selector
			"?."
			(identifier) @name))) @reference.call


