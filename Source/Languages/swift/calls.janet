#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; Swift arguments are positional even when labelled, and a label travels
#; with its argument. A trailing closure sits outside the parens, so a call
#; that has one reads as short an argument and is declined.

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

#; `super.init(...)` and `self.init(...)` call an initializer of the class
#; this one inherits from (its first listed type), or of this one.
((call_expression
   (navigation_expression
     (super_expression) @call.callee.base
     (navigation_suffix
       (simple_identifier) @_init))
   (call_suffix
     (value_arguments) @call.arguments)) @call.definition
 (:eq? @_init "init"))

((call_expression
   (navigation_expression
     (self_expression) @call.callee.class
     (navigation_suffix
       (simple_identifier) @_init))
   (call_suffix
     (value_arguments) @call.arguments)) @call.definition
 (:eq? @_init "init"))

(class_declaration
  name: (type_identifier) @call.class.name
  .
  (inheritance_specifier
    (user_type
      (type_identifier) @call.base
      .))) @call.class

(class_declaration
  name: (type_identifier) @call.class.name) @call.class

(class_declaration
  name: (user_type
    (type_identifier) @call.class.name
    .)) @call.class
