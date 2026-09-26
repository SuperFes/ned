# jsonnetfmt: `if (x) then` keeps its parens, with a space before them and
# none inside. Capture names are format.janet's; keys are scoped to jsonnet
# automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
{:space {"control.parens" {:before true :within false}}}
