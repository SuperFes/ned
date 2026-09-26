# The Vala coding style (GNOME's, which valac and libgee follow): K&R braces
# and `} else {` on one line. Its space before every call's parenthesis
# (`foo ()`) is not a rule ned's formatter has. Capture names are
# format.janet's; keys are scoped to vala automatically. format.janet or
# ned/set-format-* override it, and (ned/set-format-builtin-style false)
# turns this file off.
{:break {"brace.function"  {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "control.keyword" {:before false}}}
