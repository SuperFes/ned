# buildifier: exactly one blank line around every def, and `if (x):` keeps
# its parens with a space before them and none inside. Capture names are
# format.janet's; keys are scoped to starlark automatically. format.janet or
# ned/set-format-* override it, and (ned/set-format-builtin-style false)
# turns this file off.
{:space {"control.parens" {:before true :within false}}
 :blank {"def.toplevel" {:min-before 1 :max-before 1}}}
