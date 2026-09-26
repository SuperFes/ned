# shfmt's defaults, the de facto canonical shell formatter: a function's `{`
# stays on its `f()` line (`-fn` is the opt-out), and a run of blank lines
# collapses to one. Capture names are format.janet's; keys are scoped to bash
# automatically. format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
{:break {"brace.function" {:placement :same-line}}
 :blank {"def.toplevel" {:max-before 1}}}
