# fnlfmt, Fennel's canonical formatter: a run of blank lines between
# top-level forms collapses to one. It also adds one after a multi-line form,
# which no fixed rule here expresses. Capture names are format.janet's; keys
# are scoped to fennel automatically. format.janet or ned/set-format-*
# override it, and (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
