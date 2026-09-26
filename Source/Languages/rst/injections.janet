# `.. code-block:: <language>` (and `code`/`sourcecode`), `.. raw:: <format>`
# and `.. math::`.
((directive
   (type) @_type
   (body (arguments) @injection.language (content) @injection.content))
 (:any-of? @_type "code" "code-block" "sourcecode" "raw"))

((directive
   (type) @_type
   (body (content) @injection.content))
 (:eq? @_type "math")
 (:set! injection.language "latex"))
