# fish_indent, fish's own formatter: at most one blank line between
# functions, none added. Capture names are format.janet's; keys are scoped
# to fish automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
