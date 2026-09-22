#; Highlight query. adamrunner/tree-sitter-systemd ships no queries; this is
#; ned's own, written against the node names in grammar.janet.

(comment) @comment @spell

#; A section is the namespace its directives live in, spelled like INI/TOML.
(section_name) @type

[
  "["
  "]"
] @punctuation.bracket

"=" @operator

(directive_name) @property

(boolean_value) @boolean
(number_value) @number
(quoted_string) @string
(escape_sequence) @string.escape

#; "%i", "%H": expanded by systemd before the value is used.
(specifier) @constant.macro
(env_variable) @constant

(line_continuation) @punctuation.special
