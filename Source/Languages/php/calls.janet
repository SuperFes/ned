#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; Every call is captured; Editor/ChangeSignature.h filters by callee name.
#; A named argument (`f(b: 2)`) or a spread one (`f(...$xs)`) marks its call
#; as not rewritable by position.

(function_call_expression
  function: (name) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(function_call_expression
  function: (qualified_name (name) @call.callee)
  arguments: (arguments) @call.arguments) @call.definition

(member_call_expression
  name: (name) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(nullsafe_member_call_expression
  name: (name) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(scoped_call_expression
  name: (name) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

#; A constructor reached through its class: `Widget::__construct(...)`, or
#; `parent::__construct(...)` in a class that extends Widget.
((scoped_call_expression
   scope: (name) @call.callee
   name: (name) @_name
   arguments: (arguments) @call.arguments) @call.definition
 (:eq? @_name "__construct"))

((scoped_call_expression
   scope: (relative_scope) @_scope @call.callee.base
   name: (name) @_name
   arguments: (arguments) @call.arguments) @call.definition
 (:any-of? @_scope "parent" "Parent" "PARENT")
 (:eq? @_name "__construct"))

(class_declaration
  (base_clause
    [(name) @call.base
     (qualified_name (name) @call.base)])) @call.class

(object_creation_expression
  (name) @call.callee
  (arguments) @call.arguments) @call.definition

(object_creation_expression
  (qualified_name (name) @call.callee)
  (arguments) @call.arguments) @call.definition

(argument
  name: (_)) @argument.named

(argument
  (variadic_unpacking)) @argument.spread
