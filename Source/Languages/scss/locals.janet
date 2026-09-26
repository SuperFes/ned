#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; A mixin or function, and a style or at-rule block, is a scope. Flow
#; control (`@if`, `@each`, `@for`, `@while`) isn't: an assignment inside one
#; writes the enclosing variable, so a declaration binds a new one only where
#; none is visible (`local.assignment`) and otherwise joins it, and a loop
#; variable binds in the enclosing scope. A top-level variable is a module
#; member other files `@use`, so it stays file-level and declines. A
#; `!global` assignment writes that member, and a keyword argument's name
#; (`$size: 2px`) is the callee's parameter; both are passed over.

(mixin_statement) @local.scope
(function_statement) @local.scope
(rule_set (block) @local.scope)
(media_statement (block) @local.scope)
(supports_statement (block) @local.scope)
(at_rule (block) @local.scope)

(parameter . (variable) @local.definition.parameter)

((declaration . (property_name) @local.definition.var)
 (:match? @local.definition.var "^[$]")
 (:set! local.assignment "true"))

((each_statement . (variable) @local.definition.var)
 (:set! local.assignment "true"))
((for_statement . (variable) @local.definition.var)
 (:set! local.assignment "true"))

(variable) @local.reference
((declaration . (property_name) @local.reference)
 (:match? @local.reference "^[$]"))

(declaration . (property_name) @local.skip (global))
(argument . (variable) @local.skip . (_))
