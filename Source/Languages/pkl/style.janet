# `pkl format`: a class's `{` stays on its header's line, and a run of blank
# lines between module members or class members collapses to one. Capture
# names are format.janet's; keys are scoped to pkl automatically. format.janet
# or ned/set-format-* override it, and (ned/set-format-builtin-style false)
# turns this file off.
{:break {"brace.class" {:placement :same-line}}
 :blank {"def.toplevel" {:max-before 1}
         "def.method"   {:max-before 1}}}
