# spork/fmt, Janet's canonical formatter: at most two blank lines between
# top-level forms, and none added. Capture names are format.janet's; keys are
# scoped to janet automatically. format.janet or ned/set-format-* override
# it, and (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 2}}}
