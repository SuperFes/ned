# Org keywords are case-insensitive; upper and lower case are the spellings
# in use. A remote #+SETUPFILE URL is no file (@import.link).
(directive
  name: (expr) @_name
  value: (value . (expr) @import.link)
  (:any-of? @_name "INCLUDE" "include" "SETUPFILE" "setupfile")) @import.statement
