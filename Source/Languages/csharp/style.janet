# The .NET coding conventions (https://learn.microsoft.com/dotnet/csharp/fundamentals/coding-style/coding-conventions),
# as Microsoft's own .editorconfig spells them: csharp_new_line_before_open_brace
# = all, csharp_new_line_before_else/catch/finally = true. Capture names are
# format.janet's; keys are scoped to csharp automatically. format.janet or
# ned/set-format-* override it, and (ned/set-format-builtin-style false)
# turns this file off.
#
# Indentation (4 spaces) comes from IndentDefaults.
{:break {# Every opening brace on its own line (Allman)...
         "brace.function"  {:placement :next-line}
         "brace.class"     {:placement :next-line}
         "brace.interface" {:placement :next-line}
         "brace.namespace" {:placement :next-line}
         "brace.control"   {:placement :next-line}
         # ...and else/catch/finally start their own line too.
         "control.keyword" {:before true}}
 # csharp_space_after_keywords_in_control_flow_statements = true,
 # csharp_space_between_parentheses = false
 :space {"control.parens" {:before true :within false}}}
