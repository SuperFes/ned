# dfmt's defaults: Allman braces -- every opening brace on its own line at
# the construct's indent, `else`/`catch`/`finally` starting their own line --
# and a control statement's condition is `if (x)`.
{:break {"brace.function"  {:placement :next-line}
         "brace.class"     {:placement :next-line}
         "brace.interface" {:placement :next-line}
         "brace.control"   {:placement :next-line}
         "control.keyword" {:before true}}
 :space {"control.parens" {:before true :within false}}}
