# `just --fmt`: a run of blank lines between recipes collapses to one, and
# adjacent recipes stay adjacent. Capture names are format.janet's; keys are
# scoped to just automatically. format.janet or ned/set-format-* override it,
# and (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
