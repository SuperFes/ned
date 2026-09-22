#; Highlight query. The grammar ships none, so this is ned's own: a pattern is
#; mostly path text, and what a reader needs picked out of it is the parts that
#; change what it matches.

(comment) @comment @spell

(negation) @operator

[
  (wildcard_char_single)
  (wildcard_chars)
  (wildcard_chars_allow_slash)
] @character.special

(directory_separator) @punctuation.delimiter
(directory_separator_escaped) @string.escape
(pattern_char_escaped) @string.escape
(bracket_char_escaped) @string.escape

(bracket_expr
  [
    "["
    "]"
  ] @punctuation.bracket)

(bracket_negation) @operator
(bracket_range "-" @operator)
(bracket_char_class) @constant.builtin
