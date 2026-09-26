# PEP 8 (https://peps.python.org/pep-0008/), as black enforces it: two
# blank lines around a top-level function or class, one around a method.
# Capture names are format.janet's; keys are scoped to python automatically.
# format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
#
# Indentation (4 spaces) comes from IndentDefaults. black also drops a
# condition's redundant parens and wraps long lines; the native formatter
# does neither, so black itself (ned/set-format-command) is what makes a
# file fully canonical.
{:space {"control.parens" {:before true :within false}}
 :blank {"def.toplevel" {:min-before 2 :max-before 2}
         "def.method"   {:min-before 1 :max-before 1}}}
