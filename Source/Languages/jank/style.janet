# Clojure's layout, which jank shares -- cljfmt's defaults
# (remove-consecutive-blank-lines?): a run of blank lines between top-level
# forms collapses to one. Capture names are format.janet's; keys are scoped
# to jank automatically. format.janet or ned/set-format-* override it,
# and (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
