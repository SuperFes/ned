;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows.
;;
;; Bash is the one bundled language where "is this binding local?" is not a
;; structural question at all: `x=1` inside a function creates a GLOBAL
;; unless the name was declared with `local`/`declare`/`typeset` first. So a
;; plain assignment is deliberately captured as a reference, not a
;; definition, and only a declaration_command binds. The consequences are
;; both correct and conservative:
;;
;;   * `local count="$1"` really is function-local, so rename-symbol rewrites
;;     it and every expansion of it inside that function, and nothing else.
;;   * a plain `TOTAL=0` binds nothing here, so a reference to it resolves to
;;     no binding and rename-symbol declines rather than renaming a global in
;;     one function's worth of the file and leaving the rest behind.
;;
;; A positional parameter (`$1`, `$2`) is a variable_name in this grammar,
;; but it is never a definition and never renameable, so it simply never
;; resolves to a binding.

;; Scopes. A bash function is the only thing that introduces one -- a
;; compound_statement, a loop body and a subshell all share the caller's
;; variables.
(function_definition) @local.scope

;; The only real binding form: `local x`, `local x=1`, `declare x=1`,
;; `typeset x`, `readonly x=1`.
(declaration_command
  (variable_name) @local.definition.var)
(declaration_command
  (variable_assignment
    name: (variable_name) @local.definition.var))

;; The for-loop variable, which really is bound by the loop.
(for_statement
  variable: (variable_name) @local.definition.var)

;; References -- an expansion (`$name`, `${name}`, `${name[@]}`), an
;; arithmetic use, and the target of a plain assignment, which reads as a
;; use of whatever binding already exists. See this file's header.
(variable_name) @local.reference
