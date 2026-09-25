# ned-authored. The imprint indents every brace body. `rescript format` keeps
# a switch's (and a catch's) arms at the switch's own level with each arm's
# body one level in, puts a variant's constructors one level under its `type`
# line, and indents a binding's or function's body that starts on the line
# after its `=`/`=>`.
[(switch_expression) (try_expression)] @indent.suppress
[(switch_match) (let_binding) (function)] @indent.headed
(type_binding (variant_type)) @indent.headed

# An arm whose body is a block opened on the arm's own line indents the
# block's contents two levels past the `|` and its `}` one.
(switch_match
  (sequence_expression
    .
    (expression_statement (block))
    .)) @indent.stacked

# Continuation lines -- see c-indents.scm.
[(binary_expression) (pipe_expression)] @indent.continuation
