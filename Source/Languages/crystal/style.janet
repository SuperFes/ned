# `crystal tool format`, Crystal's canonical formatter: a run of blank lines
# collapses to one. Capture names are format.janet's; keys are scoped to
# crystal automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}
         "def.method"   {:max-before 1}}}
