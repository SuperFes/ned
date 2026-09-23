# change-signature follow-up: ned's own query, no upstream convention to
# vendor (same shape as tests.janet/signatures.janet). Deliberately NOT
# parameterized by callee name -- a query predicate only ever compares a
# capture's text to a literal string (:eq?/:any-of?, as tests.janet's own
# macro-name matching uses), so it cannot name an arbitrary target function.
# Every call expression in the buffer is captured; Editor/ChangeSignature.h
# filters by @call.callee's own text against the function being changed.
#
# Three callee shapes: a bare `foo(...)`, a qualified `Foo::bar(...)` (a
# namespace- or class-qualified free/static call), and a member call
# `obj.bar(...)`/`obj->bar(...)` (field_expression, both `.` and `->` alike --
# grammar.janet's own field_expression rule doesn't distinguish the operator
# in its node shape). A destructor call, a template-id callee
# (`foo<int>(...)`), and a call through a function pointer/lambda variable
# are all uncaptured -- change-signature never sees them as a candidate,
# which is the safe direction: an uncaptured call is one fewer site rewritten,
# never a wrong rewrite.

(call_expression
  function: (identifier) @call.callee
  arguments: (argument_list) @call.arguments) @call.definition

(call_expression
  function: (qualified_identifier name: (identifier) @call.callee)
  arguments: (argument_list) @call.arguments) @call.definition

(call_expression
  function: (field_expression field: (field_identifier) @call.callee)
  arguments: (argument_list) @call.arguments) @call.definition
