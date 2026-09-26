#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; A call lists its arguments after the function; one with fewer than the
#; function takes (`x |> f a` among them) applies it partially.

(function_call_expr
  target: (value_expr (value_qid (lower_case_identifier) @call.callee))) @call.definition @call.arguments.rest @call.curried
