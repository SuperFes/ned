#; Highlights, ned-authored: latex-lsp/tree-sitter-latex ships no queries
#; (its consumer is the texlab server). Sectioning commands are keywords,
#; every other command is a function, math is a string-like wash.
[
  (line_comment)
  (block_comment)
  (comment_environment)
] @comment

(command_name) @function

[
  "\\begin"
  "\\end"
] @keyword

(begin
  name: (curly_group_text
    (text) @type))
(end
  name: (curly_group_text
    (text) @type))

[
  (part) (chapter) (section) (subsection) (subsubsection) (paragraph) (subparagraph)
] @markup.heading

(part command: _ @keyword)
(chapter command: _ @keyword)
(section command: _ @keyword)
(subsection command: _ @keyword)
(subsubsection command: _ @keyword)
(paragraph command: _ @keyword)
(subparagraph command: _ @keyword)

(package_include command: _ @keyword.import)
(class_include command: _ @keyword.import)
(latex_include command: _ @keyword.import)
(package_include
  paths: (curly_group_path_list
    (path) @string.special.path))
(class_include
  path: (curly_group_path
    (path) @string.special.path))

(label_definition
  name: (curly_group_label
    (label) @label))
(label_reference
  names: (curly_group_label_list
    (label) @label))
(citation
  keys: (curly_group_text_list
    (text) @constant))

(key_value_pair
  key: (text) @property)

[
  (inline_formula)
  (displayed_equation)
  (math_environment)
] @markup.math

(placeholder) @variable.parameter
(value_literal) @number

[
  "{" "}" "[" "]"
] @punctuation.bracket

(operator) @operator
(delimiter) @punctuation.delimiter
