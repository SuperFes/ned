# Kotlin's coding conventions (https://kotlinlang.org/docs/coding-conventions.html),
# the official style. Capture names are format.janet's; keys are scoped to
# kotlin automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
#
# Indentation (4 spaces) comes from IndentDefaults.
{:break {# Opening braces end the line that begins the construct...
         "brace.function"  {:placement :same-line}
         "brace.class"     {:placement :same-line}
         "brace.control"   {:placement :same-line}
         # ...and `else`/`catch`/`finally` follow the closing brace.
         "control.keyword" {:before false}}
 # `if (x)`: a space after the keyword, none inside the parens.
 :space {"control.parens" {:before true :within false}}}
