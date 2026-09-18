#; Highlights, ned-authored: stsewd/tree-sitter-rst ships no queries.
(section
  (title) @markup.heading)
(section
  "adornment" @punctuation.special)

(comment) @comment

(directive
  name: (type) @keyword)
(directive
  (arguments) @string)
(directive
  (options
    (field
      (field_name) @property)))

(field
  (field_name) @property)

(target
  name: (name) @label)
(target
  link: (link) @markup.link.url)
(footnote
  name: (label) @label)
(citation
  name: (label) @label)

(substitution_definition
  (substitution) @constant)

(interpreted_text
  (role) @function)
(interpreted_text) @string.special

(list_item
  "bullet" @markup.list)
(definition_list
  (list_item
    (term) @markup.strong))

(emphasis) @markup.italic
(strong) @markup.strong
(literal) @markup.raw
(literal_block) @markup.raw.block
(doctest_block) @markup.raw.block
(reference) @markup.link
(standalone_hyperlink) @markup.link.url
(substitution_reference) @constant
(footnote_reference) @markup.link.label
(citation_reference) @markup.link.label
(inline_target) @label
(transition) @punctuation.special
