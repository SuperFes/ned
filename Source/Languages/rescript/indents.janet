# ned-authored. The imprint indents every brace body. `rescript format` keeps
# a switch's (and a catch's) arms at the switch's own level with each arm's
# body one level in, puts a variant's constructors one level under its `type`
# line, and indents a binding's or function's body that starts on the line
# after its `=`/`=>`.
[(switch_expression) (try_expression)] @indent.suppress
[(switch_match) (let_binding) (function)] @indent.headed
(type_binding (variant_type)) @indent.headed
