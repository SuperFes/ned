#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; A function is a scope: its parameters bind in it, and `set` (like a
#; `foreach` variable) binds a local only where no enclosing binding is
#; visible (`local.assignment`) -- a function sees its caller's variables,
#; so a name the file already sets stays the file's and declines. A `set`
#; with PARENT_SCOPE or CACHE writes outside the function. A macro is no
#; scope at all: its `set` writes the caller's variables.
#;
#; CMake names a variable by a bare word as often as by `${x}` --
#; `list(APPEND total ...)` and `if(total)` read and write it, while
#; `message(total)` prints a string -- so any bare word spelled like a
#; variable is uncertain (`local.uncertain`), which declines a rename rather
#; than guess either way.

(function_def) @local.scope

(function_command
  (argument_list
    (argument)
    .
    (argument (unquoted_argument) @local.definition.parameter)))

((normal_command
   (identifier) @_set
   (argument_list . (argument (unquoted_argument) @local.definition.var) .))
 (:any-of? @_set "set" "SET" "Set")
 (:set! local.assignment "true"))
((normal_command
   (identifier) @_set
   (argument_list . (argument (unquoted_argument) @local.definition.var) (argument) @_last .))
 (:any-of? @_set "set" "SET" "Set")
 (:not-any-of? @_last "PARENT_SCOPE" "CACHE")
 (:set! local.assignment "true"))
((normal_command
   (identifier) @_set
   (argument_list . (argument (unquoted_argument) @local.definition.var) (argument) @_last .))
 (:any-of? @_set "set" "SET" "Set")
 (:eq? @_last "PARENT_SCOPE")
 (:set! definition.var.scope "parent"))
((normal_command
   (identifier) @_set
   (argument_list . (argument (unquoted_argument) @local.definition.var) (argument (unquoted_argument) @_cache)))
 (:any-of? @_set "set" "SET" "Set")
 (:eq? @_cache "CACHE")
 (:set! definition.var.scope "parent"))
((foreach_command (argument_list . (argument (unquoted_argument) @local.definition.var)))
 (:set! local.assignment "true"))

(variable_ref (normal_var (variable) @local.reference))

((argument (unquoted_argument) @local.reference)
 (:set! local.uncertain "true"))

#; A binding's own word is no doubtful use of it.
((normal_command
   (identifier) @_set
   (argument_list . (argument (unquoted_argument) @local.skip)))
 (:any-of? @_set "set" "SET" "Set"))
(foreach_command (argument_list . (argument (unquoted_argument) @local.skip)))
(function_command (argument_list (argument (unquoted_argument) @local.skip)))
