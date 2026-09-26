# A rule's braces aren't a function's, class's or control statement's, so no
# brace capture here would mean what its name says.
(stylesheet [(rule_set) (media_statement) (keyframes_statement) (supports_statement) (scope_statement)
             (at_rule)] @def.toplevel)
(stylesheet . [(rule_set) (media_statement) (keyframes_statement) (supports_statement) (scope_statement)
               (at_rule)] @def.toplevel.first)
