# gawk's --pretty-print: a function's brace on its own line, every other
# brace on its header's line, `} else {` and `} while`, `if (x)`, and one
# blank line between top-level rules and functions.
{:break {"brace.function"  {:placement :next-line}
         "brace.control"   {:placement :same-line}
         "control.keyword" {:before false}}
 :space {"control.parens" {:before true :within false}}
 :blank {"def.toplevel" {:min-before 1 :max-before 1}}}
