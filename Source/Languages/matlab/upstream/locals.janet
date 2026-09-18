# References

(identifier) @reference

# Definitions

# ned: upstream quantifies a multi-pattern group (`("," (identifier))*`),
# outside QueryMatcher's measured scope; every argument identifier is a
# parameter either way.
(function_definition
  name: (identifier) @definition.function) @scope
(function_definition
  (function_arguments
    (identifier) @definition.parameter))

(assignment left: (identifier) @definition.var)
(multioutput_variable (identifier) @definition.var)

(iterator . (identifier) @definition.var)
(lambda (arguments (identifier) @definition.parameter))
(global_operator (identifier) @definition.var)
(persistent_operator (identifier) @definition.var)
(catch_clause (identifier) @definition)
