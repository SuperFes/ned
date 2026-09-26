#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; Only what nothing outside can name binds here: a process's or
#; subprogram's variables and a `for` loop's parameter. An architecture's
#; signals are reachable by external name (`<<signal .tb.dut.total>>`) and a
#; subprogram's parameters by named association at a call (`f(n => 1)`), so
#; both stay unbound and decline. A record field (`r.count`) and a formal
#; (`n =>`) are no variables. Names are case-insensitive.

(process_statement) @local.scope
(subprogram_definition) @local.scope
(loop_statement) @local.scope

((process_head (variable_declaration (identifier_list (identifier) @local.definition.var)))
 (:set! local.case-insensitive "true"))
((subprogram_head (variable_declaration (identifier_list (identifier) @local.definition.var)))
 (:set! local.case-insensitive "true"))
((for_loop (parameter_specification . (identifier) @local.definition.var))
 (:set! local.case-insensitive "true"))

((identifier) @local.reference
 (:set! local.case-insensitive "true"))

(selection (identifier) @local.skip)
(association_element . (name (identifier) @local.skip) . (_))
