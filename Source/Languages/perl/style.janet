# perltidy's defaults: braces on the header's line, `elsif`/`else` on a line
# of their own, a space between a keyword and its parens, and at most one
# blank line in a row. Space inside the parens is left alone: perltidy
# decides it by what they hold.
{:break {"brace.function"  {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "control.keyword" {:before true}}
 :space {"control.parens" {:before true}}
 :blank {"def.toplevel" {:max-before 1}}}
