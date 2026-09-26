#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; A target is a scope. `LET` and a `FOR` variable bind in it and `SET`
#; writes one; an `ARG` is the target's interface (`+target --NAME=x`, a
#; build's `--NAME`), so it binds at file level and declines. A `RUN`'s
#; `$name` is read through its injected shell (`:injected-locals`).

(target) @local.scope

(let_command (variable) @local.definition.var)
(for_command (variable) @local.definition.var)
((arg_command (variable) @local.definition.parameter)
 (:set! definition.parameter.scope "parent"))

(set_command (variable) @local.reference)
(expansion (variable) @local.reference)
