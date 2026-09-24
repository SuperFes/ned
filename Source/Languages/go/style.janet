# gofmt (https://pkg.go.dev/cmd/gofmt), Go's canonical and only style: it
# has no options, so nothing here is a preference. Capture names are
# format.janet's; keys are scoped to go automatically. format.janet or
# ned/set-format-* still override it, and (ned/set-format-builtin-style
# false) turns this file off.
#
# Indentation (tabs) comes from IndentDefaults. gofmt also aligns struct
# fields and trailing comments and drops a condition's redundant parens;
# the native formatter does neither, so gofmt itself (gopls formatting, or
# ned/set-format-command) is what makes a file fully canonical.
{:break {# Opening braces never leave their line -- ASI would end the
         # statement before them.
         "brace.function"  {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "brace.class"     {:placement :same-line}
         "brace.interface" {:placement :same-line}}
 # `if (x) {` keeps its space before the parens and none inside them.
 :space {"control.parens" {:before true :within false}}
 # gofmt collapses a run of blank lines to one.
 :blank {"def.toplevel" {:max-before 1}}}
