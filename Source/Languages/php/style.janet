# PSR-12 (https://www.php-fig.org/psr/psr-12/), PHP's canonical style guide.
# Capture names are format.janet's; keys are scoped to php automatically.
# Anything here is overridden by format.janet or ned/set-format-*, and
# (ned/set-format-builtin-style false) turns the whole file off.
#
# Indentation (4 spaces, SS2.4) comes from IndentDefaults, not from here.
{:break {# SS4.4, SS6: a named function's/method's opening brace goes on its own line
         "brace.function"        {:placement :next-line}
         # SS4: class, trait, interface and enum bodies likewise
         "brace.class"           {:placement :next-line}
         "brace.interface"       {:placement :next-line}
         # SS8: an anonymous class keeps its brace on the `new class` line
         "brace.class.anonymous" {:placement :same-line}
         # SS7: so does a closure
         "brace.closure"         {:placement :same-line}
         # SS5: control structures open on the same line...
         "brace.control"         {:placement :same-line}
         # ...and else/elseif/catch/finally/do-while's while follow the
         # closing brace on its line (SS5.1, SS5.4, SS5.6)
         "control.keyword"       {:before false}}
 # SS5: one space after the control keyword, none just inside the parens
 :space {"control.parens" {:before true :within false}}}
