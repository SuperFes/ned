#; Local-binding query, ned-authored -- see c/locals.janet's header for the
#; three rules every locals query here follows.
#;
#; R scopes by function only: an assignment anywhere in a function body binds
#; for the whole body, and a `for` variable belongs to the enclosing
#; function. nvim-treesitter's query marks a call's named arguments
#; (`f(x = 1)`) as definitions; those name the callee's parameters, not a
#; variable here, so they are skipped along with `$`/`@` member names and
#; `pkg::` qualifiers. `<<-` assigns into an enclosing scope, so it binds
#; nothing locally.

(function_definition) @local.scope

(parameter
  name: (identifier) @local.definition.parameter)

(binary_operator
  lhs: (identifier) @local.definition.var
  operator: ["<-" "="])

(binary_operator
  operator: "->"
  rhs: (identifier) @local.definition.var)

(for_statement
  variable: (identifier) @local.definition.var)

(identifier) @local.reference

(argument
  name: (identifier) @local.skip)

(extract_operator
  rhs: (identifier) @local.skip)

(namespace_operator
  (identifier) @local.skip)
