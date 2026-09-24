# ned-authored, latexindent's defaults: an environment's body one level in
# and its \end back at its \begin, except `document`, whose body stays
# flush. Verbatim-like bodies are String-highlighted and left as written.
((generic_environment
   (begin name: (curly_group_text (text) @_name))) @indent.headed
 (:not-eq? @_name "document"))
(math_environment) @indent.headed
[(generic_environment (end) @dedent)
 (math_environment (end) @dedent)]
