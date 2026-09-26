# The Ruby Style Guide (https://rubystyle.guide/) as RuboCop's defaults
# enforce it: one blank line between method definitions
# (Layout/EmptyLineBetweenDefs) and never two in a row (Layout/EmptyLines).
# Capture names are format.janet's; keys are scoped to ruby automatically.
# format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
{:space {"control.parens" {:before true :within false}}
 :blank {"def.toplevel" {:min-before 1 :max-before 1}
         "def.method"   {:min-before 1 :max-before 1}}}
