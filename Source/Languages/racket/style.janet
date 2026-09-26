# raco fmt's default --max-blank-lines 1: a run of blank lines between
# top-level forms collapses to one, and none is added. Capture names are
# format.janet's; keys are scoped to racket automatically. format.janet or
# ned/set-format-* override it, and (ned/set-format-builtin-style false)
# turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
