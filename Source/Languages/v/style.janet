# `v fmt`, V's canonical formatter: opening braces end the line that begins
# the construct -- also what V's grammar requires, which is why
# :braces-on-header-line refuses any other placement. Capture names are
# format.janet's; keys are scoped to v automatically. format.janet or
# ned/set-format-* override it, and (ned/set-format-builtin-style false)
# turns this file off.
{:break {"brace.function" {:placement :same-line}
         "brace.control"  {:placement :same-line}}}
