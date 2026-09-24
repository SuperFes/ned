#; change-signature: call sites (ned's own query -- see cpp/calls.janet).
#; `Widget.new(...)` reaches `initialize`. `k: 1` names a keyword argument;
#; `*xs`, `**opts` and `&blk` spread. A call written without parens is
#; declined.

((call
   receiver: (_)? @_receiver
   method: (identifier) @call.callee
   arguments: (argument_list) @call.arguments) @call.definition
 (:not-eq? @call.callee "new"))

((call
   receiver: (constant) @call.callee
   method: (identifier) @_new
   arguments: (argument_list) @call.arguments) @call.definition
 (:eq? @_new "new"))

((call
   receiver: (scope_resolution name: (constant) @call.callee)
   method: (identifier) @_new
   arguments: (argument_list) @call.arguments) @call.definition
 (:eq? @_new "new"))

(pair
  key: (hash_key_symbol) @argument.name) @argument.named

[(splat_argument) (hash_splat_argument) (block_argument)] @argument.spread
