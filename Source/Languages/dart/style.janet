# The canonical formatter's defaults (`dart format`): opening braces end the
# line that begins the construct, `} else {` stays on one line, and a
# control statement's condition is `if (x)`.
{:break {"brace.function"  {:placement :same-line}
         "brace.class"     {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "control.keyword" {:before false}}
 :space {"control.parens" {:before true :within false}}}
