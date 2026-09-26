#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; An application lists its arguments after the function; one with fewer
#; than the function takes (`x |> f a` among them) applies it partially.
#; `~x:1` and `?x` pass by label.

(application_expression
  function: (value_path (value_name) @call.callee)) @call.definition @call.arguments.rest @call.curried

(labeled_argument (label_name) @argument.name) @argument.named
