#; Symbol-kind query. tree-sitter-fish ships none. Only top-level definitions
#; are named: a `set` inside a function is an everyday local.

(function_definition
  name: (_) @name) @definition.function

#; `set`'s name is its first argument that isn't a flag. A quantifier can't
#; say "the flags, then the name" (it runs greedily over every word), so each
#; count of flags up to four is spelled out; a flag that erases, queries,
#; lists or shows means the command defines nothing.

((program
   (command
     name: (word) @_cmd
     .
     argument: (word) @name) @definition.variable)
 (:eq? @_cmd "set")
 (:not-match? @name "^-"))

((program
   (command
     name: (word) @_cmd
     .
     argument: (word) @_flag0
     .
     argument: (word) @name) @definition.variable)
 (:eq? @_cmd "set")
 (:match? @_flag0 "^-")
 (:not-match? @_flag0 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:not-match? @name "^-"))

((program
   (command
     name: (word) @_cmd
     .
     argument: (word) @_flag0
     .
     argument: (word) @_flag1
     .
     argument: (word) @name) @definition.variable)
 (:eq? @_cmd "set")
 (:match? @_flag0 "^-")
 (:not-match? @_flag0 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:match? @_flag1 "^-")
 (:not-match? @_flag1 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:not-match? @name "^-"))

((program
   (command
     name: (word) @_cmd
     .
     argument: (word) @_flag0
     .
     argument: (word) @_flag1
     .
     argument: (word) @_flag2
     .
     argument: (word) @name) @definition.variable)
 (:eq? @_cmd "set")
 (:match? @_flag0 "^-")
 (:not-match? @_flag0 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:match? @_flag1 "^-")
 (:not-match? @_flag1 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:match? @_flag2 "^-")
 (:not-match? @_flag2 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:not-match? @name "^-"))

((program
   (command
     name: (word) @_cmd
     .
     argument: (word) @_flag0
     .
     argument: (word) @_flag1
     .
     argument: (word) @_flag2
     .
     argument: (word) @_flag3
     .
     argument: (word) @name) @definition.variable)
 (:eq? @_cmd "set")
 (:match? @_flag0 "^-")
 (:not-match? @_flag0 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:match? @_flag1 "^-")
 (:not-match? @_flag1 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:match? @_flag2 "^-")
 (:not-match? @_flag2 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:match? @_flag3 "^-")
 (:not-match? @_flag3 "^-([a-zA-Z]*[eqnS]|-(erase|query|names|show)$)")
 (:not-match? @name "^-"))

#; `abbr` and `alias` the same way; fish makes an alias a function.
((program
   (command
     name: (word) @_cmd
     .
     argument: (word) @name) @definition.function)
 (:any-of? @_cmd "abbr" "alias")
 (:not-match? @name "^-"))

((program
   (command
     name: (word) @_cmd
     .
     argument: (word) @_flag0
     .
     argument: (word) @name) @definition.function)
 (:any-of? @_cmd "abbr" "alias")
 (:match? @_flag0 "^-")
 (:not-match? @_flag0 "^-([a-zA-Z]*[eqlrs]|-(erase|query|list|rename|show)$)")
 (:not-match? @name "^-"))

((program
   (command
     name: (word) @_cmd
     .
     argument: (word) @_flag0
     .
     argument: (word) @_flag1
     .
     argument: (word) @name) @definition.function)
 (:any-of? @_cmd "abbr" "alias")
 (:match? @_flag0 "^-")
 (:not-match? @_flag0 "^-([a-zA-Z]*[eqlrs]|-(erase|query|list|rename|show)$)")
 (:match? @_flag1 "^-")
 (:not-match? @_flag1 "^-([a-zA-Z]*[eqlrs]|-(erase|query|list|rename|show)$)")
 (:not-match? @name "^-"))
