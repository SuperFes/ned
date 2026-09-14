# ned-authored: tree-sitter-grammars/tree-sitter-hcl ships no queries/ at
# all (verified at admission -- no highlights.scm/tags.scm anywhere in the
# repo), unlike every other Release-0.6-batch grammar. This is core HCL
# only (the repo's separate dialects/terraform grammar is not fetched --
# same "one core, dialect deltas deferred" precedent as SQL), authored
# directly from grammar.json's rule shapes rather than adapted from an
# upstream file.
#
# HCL wraps nearly every structural punctuation mark in its own named node
# (block_start/block_end/tuple_start/tuple_end/object_start/object_end/
# ellipsis/null_lit all have a grammar RULE that is just that literal
# string, which swallows it -- there is no separate anonymous token left to
# match via a plain "{"-style literal, confirmed against node-types.json
# rather than assumed). Every such construct below is matched by its named
# node instead; only the genuinely hidden/anonymous tokens (parens, the
# arithmetic/comparison operators, delimiters) use literal string matches.

(comment) @comment

(numeric_lit) @number

(bool_lit) @constant.builtin.boolean

(null_lit) @constant.builtin

(template_literal) @string
(quoted_template_start) @string
(quoted_template_end) @string
(heredoc_identifier) @string

# A later span wins on overlap (Mode.h's rule), so the specific captures
# below must be listed after this catch-all, not before it.
(identifier) @variable

(attribute
  (identifier) @property)

(block
  (identifier) @type
  .
  (string_lit)? @string)

(function_call
  (identifier) @function)

[
  "for"
  "in"
  "if"
  "else"
] @keyword

[
  "=="
  "!="
  "<"
  "<="
  ">"
  ">="
  "&&"
  "||"
  "!"
  "+"
  "-"
  "*"
  "/"
  "%"
  "?"
  "="
] @operator

(ellipsis) @operator

[
  ":"
  ","
  "."
] @punctuation.delimiter

[
  "("
  ")"
] @punctuation.bracket

[
  (block_start)
  (block_end)
  (tuple_start)
  (tuple_end)
  (object_start)
  (object_end)
] @punctuation.bracket

[
  (template_interpolation_start)
  (template_interpolation_end)
  (template_directive_start)
  (template_directive_end)
] @punctuation.special
