#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; A proc is the only scope. Its parameters bind in it, and `set` (like a
#; loop's or `catch`'s variable) binds a local only where no enclosing
#; binding is visible (`local.assignment`), so a proc's `global top`, whose
#; name binds at file level, makes every later `top` in it the global's --
#; which declines. `upvar` and `variable` link a name to another scope the
#; same way.
#;
#; Tcl names a variable by a bare word as often as by `$x`: `incr total`
#; writes it, while `puts total` prints a string. The commands known to take
#; a variable name first read it as a reference; any other bare word spelled
#; like a variable is uncertain (`local.uncertain`), which declines a rename
#; rather than guess either way.

(procedure) @local.scope

(procedure arguments: (arguments (argument name: (simple_word) @local.definition.parameter)))
(procedure arguments: (arguments (simple_word) @local.definition.parameter))

((set (id) @local.definition.var)
 (:set! local.assignment "true"))
((foreach (arguments (simple_word) @local.definition.var))
 (:set! local.assignment "true"))
((foreach (arguments (argument name: (simple_word) @local.definition.var)))
 (:set! local.assignment "true"))
((catch (_) . (simple_word) @local.definition.var .)
 (:set! local.assignment "true"))

((global (simple_word) @local.definition.var)
 (:set! definition.var.scope "parent"))
((command
   name: (simple_word) @_link
   arguments: (word_list (simple_word) @local.definition.var))
 (:any-of? @_link "upvar" "variable")
 (:set! definition.var.scope "parent"))

(variable_substitution (id) @local.reference)
(set (id) @local.reference)

((command
   name: (simple_word) @_writes
   arguments: (word_list . (simple_word) @local.reference))
 (:any-of? @_writes "incr" "append" "lappend" "lset" "unset"))
((command
   name: (simple_word) @_unset
   arguments: (word_list (simple_word) @local.reference))
 (:eq? @_unset "unset"))

((command
   name: (simple_word) @_other
   arguments: (word_list (simple_word) @local.reference))
 (:not-any-of? @_other "incr" "append" "lappend" "lset" "unset" "upvar" "variable")
 (:set! local.uncertain "true"))
((regexp (simple_word) @local.reference)
 (:set! local.uncertain "true"))
