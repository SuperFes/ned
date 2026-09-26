# gleam format: every brace on its header's line, and at most one blank line
# between definitions.
{:break {"brace.function" {:placement :same-line}
         "brace.class"    {:placement :same-line}
         "brace.control"  {:placement :same-line}}
 :blank {"def.toplevel" {:max-before 1}}}
