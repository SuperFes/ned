;; Local-binding query -- see c-locals.scm's header for the three rules every
;; locals query here follows.
;;
;; Fish is bash's situation with different spelling: "is this binding local?"
;; is not a structural question, because a bare `set x 1` modifies whatever
;; enclosing scope already holds `x` and only creates a new one when nothing
;; does. So bash-locals.scm's resolution applies verbatim -- only an
;; explicitly function-scoped declaration binds (`set -l`, `set --local`,
;; `set -f`, `set --function`), and every other `set` target is captured as a
;; REFERENCE rather than a definition. The consequences are the same pair:
;;
;;   * `set -l count 0` really is function-local, so rename-symbol rewrites it
;;     along with every `$count` expansion inside that function and nothing
;;     else. Its later `set count (math $count + 1)` reassignment is captured
;;     as a reference, which is what keeps the rename from rewriting the
;;     expansions and leaving the assignment behind.
;;   * `set -g total 0` binds nothing here, so a reference to it resolves to
;;     no binding and rename-symbol declines rather than renaming a global
;;     across one function's worth of the file.
;;
;; A `set -l` target is deliberately captured as BOTH a definition and a
;; reference (the reference pattern below matches any flagged `set`, this
;; one's flag included). That is not a bug to tidy up: LocalScopes.cpp sorts
;; and uniques the occurrence list, so the duplicate collapses, and keeping
;; the reference pattern flag-agnostic is what stops a new fish scope flag
;; from silently falling out of the reference set.
;;
;; Two deliberate misses, both of which degrade to "declines" rather than to
;; a wrong rename: a scope flag that isn't adjacent to its own target
;; (`set -l -x count 0` -- the pattern anchors the name to the flag before
;; it), and `read`'s own `-l` binding form, which has no `set` command node
;; to hang off.

;; Scopes. A fish function is the only thing that introduces one -- an if/
;; for/while/begin block shares the function's variables. The whole
;; function_definition is the scope, not its body, so the `--argument-names`
;; parameters below sit inside it (rule 1).
(function_definition) @local.scope

;; Parameters. `function f --argument-names a b` -- fish consumes every
;; remaining option word as an argument name, so an unanchored pair whose
;; first half is the flag captures exactly the right set, in order.
((function_definition
   option: (word) @_argnames
   option: (word) @local.definition.parameter)
 (#any-of? @_argnames "-a" "--argument-names"))

;; The one real binding form: an explicitly function-scoped `set`. The flag
;; pattern accepts clustered short flags (`set -lx count 0`) as well as the
;; long spellings.
((command
   name: (word) @_set
   (word) @_flag
   .
   (word) @local.definition.var)
 (#eq? @_set "set")
 (#match? @_flag "^(-[a-zA-Z]*[lf][a-zA-Z]*|--local|--function)$"))

;; The for-loop variable. Confirmed against fish 4.9.2 rather than assumed
;; from C/Python precedent: fish's loop variable is FUNCTION-scoped, not
;; loop-scoped -- it survives the loop and overwrites an existing same-named
;; local -- so a for_statement is deliberately not a scope here. Capturing
;; one would split a single function-local binding in two and rename only
;; the half inside the loop.
(for_statement
  variable: (variable_name) @local.definition.var)

;; References -- every expansion (`$name`, `"$prefix$item"`, `$list[1]`),
;; plus a `set` target, which reads as a use of whatever binding already
;; exists. See this file's header for why the target is a reference even
;; when the very same node is also captured as a definition above.
(variable_name) @local.reference
((command
   name: (word) @_set
   .
   (word) @local.reference)
 (#eq? @_set "set")
 (#not-match? @local.reference "^-"))
((command
   name: (word) @_set
   (word) @_flag
   .
   (word) @local.reference)
 (#eq? @_set "set")
 (#match? @_flag "^-"))
