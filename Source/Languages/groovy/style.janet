# Groovy follows Java's code conventions (the Apache Groovy style guide
# covers idioms, not layout): K&R braces, `} else {` on one line, and
# `if (x)`. Capture names are format.janet's; keys are scoped to groovy
# automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
{:break {"brace.function"  {:placement :same-line}
         "brace.class"     {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "control.keyword" {:before false}}
 :space {"control.parens" {:before true :within false}}}
