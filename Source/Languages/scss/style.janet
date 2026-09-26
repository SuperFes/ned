# Prettier: braces on the header's line, `} @else {`, and at most one blank
# line in a row.
{:break {"brace.function"  {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "control.keyword" {:before false}}
 :blank {"def.toplevel" {:max-before 1}}}
