# odinfmt's default (1TBS): opening braces end the line that begins the
# construct. Capture names are format.janet's; keys are scoped to odin
# automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
{:break {"brace.function" {:placement :same-line}
         "brace.control"  {:placement :same-line}}}
