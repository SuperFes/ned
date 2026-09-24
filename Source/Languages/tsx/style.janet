# Prettier (https://prettier.io/docs/en/rationale.html), the de facto style.
# Capture names are format.janet's; keys are scoped to tsx automatically.
# format.janet or ned/set-format-* override it, and
# (ned/set-format-builtin-style false) turns this file off.
#
# Indentation (2 spaces) comes from IndentDefaults. Prettier also wraps at
# its print width and picks a quote style, both configurable in a project's
# .prettierrc, which ned does not read -- so neither is applied here.
{:break {# 1TBS: braces open on the line, `} else {` shares one.
         "brace.function"  {:placement :same-line}
         "brace.class"     {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "brace.interface" {:placement :same-line}
         "control.keyword" {:before false}}
 :space {"control.parens" {:before true :within false}}
 # Prettier keeps at most one blank line.
 :blank {"def.toplevel" {:max-before 1}
         "def.method"   {:max-before 1}}}
