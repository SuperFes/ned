#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; Elixir binds by pattern matching: a function head, a clause's left side
#; (`fn`, `case`, `receive`, ...), the left of `=` and of a `for`/`with`
#; generator's `<-`. A pattern binds every name inside it at any depth, so
#; each one is captured whole (`.pattern`) and the engine binds the
#; identifiers within, minus what a pattern reads rather than binds: a pinned
#; `^x`, a default after `\\`, a binary segment's type after `::`, and call
#; targets. A function body sees nothing bound outside it.

#; Scopes: a function clause (head and body), every `->` clause, a do-block,
#; and the comprehensions whose generators bind for their body.
((call
   target: (identifier) @_def) @local.scope
 (:any-of? @_def "def" "defp" "defmacro" "defmacrop" "defguard" "defguardp")
 (:set! local.scope-inherits "false"))

((call
   target: (identifier) @_comprehension) @local.scope
 (:any-of? @_comprehension "for" "with"))

[
  (stab_clause)
  (do_block)
] @local.scope

#; Function heads, with or without a guard.
((call
   target: (identifier) @_def
   (arguments
     (call
       (arguments) @local.definition.parameter.pattern)))
 (:any-of? @_def "def" "defp" "defmacro" "defmacrop" "defguard" "defguardp"))

((call
   target: (identifier) @_def
   (arguments
     (binary_operator
       left: (call
         (arguments) @local.definition.parameter.pattern)
       operator: "when")))
 (:any-of? @_def "def" "defp" "defmacro" "defmacrop" "defguard" "defguardp"))

#; Clause patterns.
(stab_clause
  left: (arguments) @local.definition.parameter.pattern)

(stab_clause
  left: (binary_operator
    left: (arguments) @local.definition.parameter.pattern
    operator: "when"))

#; Matches and generators.
(binary_operator
  left: (_) @local.definition.var.pattern
  operator: "=")

(binary_operator
  left: (_) @local.definition.var.pattern
  operator: "<-")

#; The right side is evaluated before the pattern binds: `x = x + 1` reads
#; the x already bound.
(binary_operator
  operator: ["=" "<-"]
  right: (_) @local.initializer) @local.declaration

#; What a pattern reads rather than binds.
(unary_operator
  operator: "^"
  operand: (_) @local.pattern.exclude)

(binary_operator
  operator: "\\\\"
  right: (_) @local.pattern.exclude)

(binary_operator
  operator: "::"
  right: (_) @local.pattern.exclude)

((identifier) @local.pattern.exclude
 (:eq? @local.pattern.exclude "_"))

(identifier) @local.reference

#; Identifiers that name no variable: function and macro names, a remote
#; call's function, a `&name/arity` capture and module attributes.
(call
  target: (identifier) @local.skip)

(dot
  right: (identifier) @local.skip)

(unary_operator
  operator: "&"
  operand: (binary_operator
    left: (identifier) @local.skip
    operator: "/"))

(unary_operator
  operator: "@"
  operand: (identifier) @local.skip)
