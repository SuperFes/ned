# Java's code conventions -- Oracle's and Google's agree here: K&R braces
# (the opening brace ends the line that begins the construct), `} else {`
# on one line, and `if (x)` with a space after the keyword and none inside
# the parens. Capture names are format.janet's; keys are scoped to java
# automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
#
# Indentation (4 spaces) comes from IndentDefaults.
{:break {"brace.function"  {:placement :same-line}
         "brace.class"     {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "control.keyword" {:before false}}
 :space {"control.parens" {:before true :within false}}}
