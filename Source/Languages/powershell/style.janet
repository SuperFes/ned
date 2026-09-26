# The PowerShell Practice and Style guide: One True Brace Style (`} else {`,
# `} catch {`), `if ($x)`, two blank lines around functions and classes and
# one around methods.
{:break {"brace.function"  {:placement :same-line}
         "brace.class"     {:placement :same-line}
         "brace.control"   {:placement :same-line}
         "control.keyword" {:before false}}
 :space {"control.parens" {:before true :within false}}
 :blank {"def.toplevel" {:min-before 2 :max-before 2}
         "def.method"   {:min-before 1 :max-before 1}}}
