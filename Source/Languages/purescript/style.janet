# purs-tidy, PureScript's canonical formatter: at most one blank line between
# declarations. It adds one between declarations of different kinds, which no
# fixed rule here expresses. Capture names are format.janet's; keys are scoped
# to purescript automatically. format.janet or ned/set-format-* override it,
# and (ned/set-format-builtin-style false) turns this file off.
{:blank {"def.toplevel" {:max-before 1}}}
