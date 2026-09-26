# erlfmt, Erlang's canonical formatter: a run of blank lines between forms
# collapses to one, and none is added. Capture names are format.janet's; keys
# are scoped to erlang automatically. format.janet or ned/set-format-*
# override it, and (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
