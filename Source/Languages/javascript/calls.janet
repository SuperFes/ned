#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `f(...xs)` spreads.

(call_expression
  function: (identifier) @call.callee
  arguments: (arguments) @call.arguments) @call.definition

(call_expression
  function: (member_expression
    property: (property_identifier) @call.callee)
  arguments: (arguments) @call.arguments) @call.definition

(new_expression
  constructor: [(identifier) @call.callee
                (member_expression property: (property_identifier) @call.callee)]
  arguments: (arguments) @call.arguments) @call.definition

(spread_element) @argument.spread

#; `super(...)` calls the constructor of the class this one extends.
(call_expression
  function: (super) @call.callee.base
  arguments: (arguments) @call.arguments) @call.definition

(class_declaration
  (class_heritage
    [(identifier) @call.base
     (member_expression property: (property_identifier) @call.base)])) @call.class
(class
  (class_heritage
    [(identifier) @call.base
     (member_expression property: (property_identifier) @call.base)])) @call.class
