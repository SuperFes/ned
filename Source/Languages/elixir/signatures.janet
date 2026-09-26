#; change-signature: definitions and their parameters (ned's own query --
#; see php/signatures.janet for the @parameter captures). Each `def` clause
#; is its own definition; a pattern parameter is matched by its text, and
#; `b \\ 1` is a parameter with a default. A `@spec` types the parameters.

((call
  target: (identifier) @_def
  (arguments
    [(call target: (identifier) @signature.name (arguments) @signature.parameters) @signature.definition
     (binary_operator
       left: (call target: (identifier) @signature.name (arguments) @signature.parameters) @signature.definition
       operator: "when")]))
  (:any-of? @_def "def" "defp" "defmacro" "defmacrop"))

(binary_operator
  left: (identifier) @parameter.name
  operator: "\\\\"
  right: (_) @parameter.default) @parameter

((unary_operator
  operator: "@"
  operand: (call
    target: (identifier) @_spec
    (arguments
      [(binary_operator
         left: (call target: (identifier) @signature.name (arguments) @signature.parameters)
         operator: "::") @signature.type
       (binary_operator
         left: (binary_operator
           left: (call target: (identifier) @signature.name (arguments) @signature.parameters)
           operator: "::") @signature.type
         operator: "when")])))
  (:eq? @_spec "spec"))
