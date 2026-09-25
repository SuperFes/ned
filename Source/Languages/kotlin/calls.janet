#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A trailing lambda sits outside the parens, so a call that has one reads
#; as short an argument and is declined. `f(b = 2)` is named, `f(*xs)` spread.

(call_expression
  (simple_identifier) @call.callee
  (call_suffix
    (value_arguments) @call.arguments)) @call.definition

(call_expression
  (navigation_expression
    (navigation_suffix
      (simple_identifier) @call.callee))
  (call_suffix
    (value_arguments) @call.arguments)) @call.definition

(value_argument
  (simple_identifier)
  "=") @argument.named

(value_argument
  "*") @argument.spread

#; `class B : A(x)` calls A's constructor; a secondary constructor's
#; `: super(...)` and `: this(...)` call the base's or this class's.
(constructor_invocation
  (user_type
    (type_identifier) @call.callee
    .)
  (value_arguments) @call.arguments) @call.definition

(constructor_delegation_call
  "super" @call.callee.base
  (value_arguments) @call.arguments) @call.definition

(constructor_delegation_call
  "this" @call.callee.class
  (value_arguments) @call.arguments) @call.definition

(class_declaration
  (type_identifier) @call.class.name
  (delegation_specifier
    (constructor_invocation
      (user_type
        (type_identifier) @call.base
        .)))) @call.class

(class_declaration
  (type_identifier) @call.class.name
  .
  (delegation_specifier
    (user_type
      (type_identifier) @call.base
      .))) @call.class

(class_declaration
  (type_identifier) @call.class.name) @call.class
