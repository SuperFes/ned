# StyLua's defaults, the de facto canonical Lua formatter: a run of blank
# lines collapses to one. Capture names are format.janet's; keys are scoped
# to lua automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
