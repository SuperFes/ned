# Injects a #+BEGIN_<NAME> ... #+END_<NAME> block's body as the language
# named by its own first parameter -- e.g. a source block
#
#   #+begin_src python
#   def f(): pass
#   #+end_src
#
# injects "python", and an export block
#
#   #+begin_export html
#   <p>hi</p>
#   #+end_export
#
# injects "html". Every "#+begin_<NAME>" construct (SRC, QUOTE, EXAMPLE,
# EXPORT, COMMENT, CENTER, VERSE, ...) parses as one generic "block" node --
# there's no separate "src_block" node type to target instead, so this
# pattern isn't filtered by the block's own name field. A block with no
# parameter at all (QUOTE, EXAMPLE, COMMENT, CENTER, VERSE) simply never
# matches. A block with more than one parameter (a source block's own
# header arguments, e.g. "#+begin_src python :results output") produces one
# @injection.language capture per parameter in the same match; a consumer
# should use the first and ignore the rest.
(block
  parameter: (expr) @injection.language
  contents: (contents) @injection.content)
