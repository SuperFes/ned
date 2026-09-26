#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A definition's own head and a `@spec`'s read as calls and aren't. `x |>
#; f(y)` is `f(x, y)`, and `&f/2` passes the function on to be called
#; elsewhere (@call.arity.value).

(call
  target: [(identifier) @call.callee
           (dot right: (identifier) @call.callee)]
  (arguments) @call.arguments) @call.definition

(binary_operator
  operator: "|>"
  right: (call
    target: [(identifier) @call.callee
             (dot right: (identifier) @call.callee)]
    (arguments) @call.arguments) @call.definition) @call.receiver.first

(unary_operator
  operator: "&"
  operand: (binary_operator
    left: [(identifier) @call.callee
           (call target: (dot right: (identifier) @call.callee))]
    operator: "/"
    right: (integer) @call.arity.value)) @call.definition

((call
  target: (identifier) @_def
  (arguments
    [(call) @call.exclude
     (binary_operator left: (call) @call.exclude operator: "when")]))
  (:any-of? @_def "def" "defp" "defmacro" "defmacrop"))

((unary_operator
  operator: "@"
  operand: (call
    target: (identifier) @_spec
    (arguments
      [(binary_operator left: (call) @call.exclude operator: "::")
       (binary_operator
         left: (binary_operator left: (call) @call.exclude operator: "::")
         operator: "when")])))
  (:eq? @_spec "spec"))
