# rescript format: every brace on its header's line, `} else {`, and at most
# one blank line between declarations.
{:break {"brace.function"  {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "brace.namespace" {:placement :same-line}
         "control.keyword" {:before false}}
 :blank {"def.toplevel" {:max-before 1}}}
