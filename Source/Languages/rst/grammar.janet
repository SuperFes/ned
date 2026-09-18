# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "rst"
 :extras [__newline __whitespace]
 :conflicts []
 :precedences []
 :externals [_newline
             _blankline
             _indent
             _newline_indent
             _dedent
             _overline
             _underline
             transition
             _char_bullet
             _numeric_bullet
             _field_mark
             _field_mark_end
             _literal_indented_block_mark
             _literal_quoted_block_mark
             _quoted_literal_block
             _line_block_mark
             _attribution_mark
             _doctest_block_mark
             _text
             emphasis
             strong
             _interpreted_text
             _interpreted_text_prefix
             _role_name_prefix
             _role_name_suffix
             literal
             substitution_reference
             inline_target
             footnote_reference
             citation_reference
             reference
             standalone_hyperlink
             _explicit_markup_start
             _footnote_label
             _citation_label
             _target_name
             _anonymous_target_mark
             _directive_name
             _substitution_mark
             _empty_comment
             _invalid_token]
 :inline []
 :supertypes [_list _markup_block _inline_markup]
 :rules
 {document (:repeat (:choice section _transition_block _body_element_block))
  section (:choice _overline_section _underline_section)
  _overline_section (:seq
                     (:alias _overline "adornment")
                     (:alias _line title)
                     (:alias _underline "adornment"))
  _underline_section (:seq (:alias _line title) (:alias _underline "adornment"))
  _transition_block (:seq transition _blankline)
  _body_element_block (:seq _body_element _blankline)
  body (:seq (:repeat (:seq _body_element _blankline)) _body_element _dedent)
  _body_element (:choice
                 paragraph
                 _list
                 _explicit_markup_block
                 _literal_block
                 line_block
                 _block_quote_block
                 doctest_block)
  paragraph (:repeat1 _paragraph_line)
  _paragraph_line (:seq (:repeat1 _inline_markup) (:choice _literal_block :blank) _newline)
  _list (:choice bullet_list enumerated_list definition_list field_list)
  bullet_list (:repeat1 (:alias _bullet_list_item list_item))
  _bullet_list_item (:seq (:alias _char_bullet "bullet") (:choice body _dedent))
  enumerated_list (:repeat1 (:alias _numeric_list_item list_item))
  _numeric_list_item (:seq (:alias _numeric_bullet "bullet") (:choice body _dedent))
  definition_list (:repeat1 (:alias _definition_list_item list_item))
  _definition_list_item (:seq
                         (:alias (:repeat1 _inline_markup) term)
                         (:choice _classifiers :blank)
                         _newline_indent
                         (:alias body definition))
  _classifiers (:repeat1
                (:seq
                 (:alias
                  (:token
                   (:seq
                    (:repeat1 (:choice " " "\t" "\x0b" "\x0c" " "))
                    ":"
                    (:repeat1 (:choice " " "\t" "\x0b" "\x0c" " "))))
                  ":")
                 (:alias (:repeat1 _inline_markup) classifier)))
  field_list (:repeat1 field)
  field (:seq
         (:alias _field_mark ":")
         (:alias (:repeat1 _inline_markup) field_name)
         (:alias _field_mark_end ":")
         (:choice (:alias body field_body) _dedent))
  _literal_block (:choice
                  (:seq
                   (:alias _literal_indented_block_mark "::")
                   (:choice (:alias _indented_text_block literal_block) _dedent))
                  (:seq
                   (:alias _literal_quoted_block_mark "::")
                   (:alias _quoted_literal_block literal_block)))
  _indented_text_block (:seq (:repeat (:seq _text_block _blankline)) _text_block _dedent)
  line_block (:repeat1 line)
  line (:seq (:alias _line_block_mark "|") (:repeat _line) _dedent)
  _block_quote_block (:seq _indent block_quote)
  block_quote (:seq
               (:repeat (:seq _body_element _blankline))
               (:choice attribution _body_element)
               _dedent)
  attribution (:seq (:alias _attribution_mark "--") (:repeat1 _line) _dedent)
  doctest_block (:seq _doctest_block_mark _text_block _blankline)
  _explicit_markup_block (:repeat1 _markup_block)
  _markup_block (:choice
                 footnote
                 citation
                 target
                 (:alias _anonymous_target target)
                 directive
                 substitution_definition
                 comment
                 (:alias _empty_comment comment))
  footnote (:seq
            (:alias _explicit_markup_start "..")
            (:field :name (:alias _footnote_label label))
            (:field :body (:choice body _dedent)))
  citation (:seq
            (:alias _explicit_markup_start "..")
            (:field :name (:alias _citation_label label))
            (:field :body (:choice body _dedent)))
  target (:seq
          (:alias _explicit_markup_start "..")
          (:field :name (:alias _target_name name))
          (:field :link (:choice (:alias (:pattern "\\S(.*\\S)?") link) :blank))
          _dedent)
  _anonymous_target (:seq
                     (:alias _anonymous_target_mark "__")
                     (:field :link (:choice (:alias (:pattern "\\S(.*\\S)?") link) :blank))
                     _newline)
  directive (:seq
             (:alias _explicit_markup_start "..")
             (:field :name (:alias _directive_name type))
             "::"
             (:field :body (:choice (:alias _directive_body body) _dedent)))
  _directive_body (:choice
                   (:seq
                    (:choice (:alias _text_line arguments) :blank)
                    (:choice (:alias field_list options) :blank)
                    _blankline
                    (:alias _indented_text_block content))
                   (:seq
                    (:choice
                     (:alias _text_line arguments)
                     (:alias field_list options)
                     (:seq (:alias _text_line arguments) (:alias field_list options)))
                    _dedent)
                   (:seq
                    _newline
                    (:choice
                     (:seq (:alias field_list options) _dedent)
                     (:alias _indented_text_block content)
                     (:seq
                      (:alias field_list options)
                      _blankline
                      (:alias _indented_text_block content))))
                   (:seq (:alias _text_line arguments) (:alias _indented_text_block content)))
  substitution_definition (:seq
                           (:alias _explicit_markup_start "..")
                           (:field :name (:alias _substitution_mark substitution))
                           (:field :body (:alias _embedded_directive directive)))
  _embedded_directive (:seq
                       (:field :name (:alias _directive_name type))
                       (:seq "::" (:choice (:choice " " "\t" "\x0b" "\x0c" " ") _newline))
                       (:field :body (:choice (:alias _directive_body body) _dedent)))
  comment (:seq (:alias _explicit_markup_start "..") (:choice _indented_text_block _dedent))
  _line (:seq (:repeat1 _inline_markup) _newline)
  _text_block (:repeat1 _text_line)
  _text_line (:seq (:repeat1 (:alias _text "text")) _newline)
  _inline_markup (:choice
                  (:alias _text "text")
                  emphasis
                  strong
                  interpreted_text
                  literal
                  substitution_reference
                  inline_target
                  footnote_reference
                  citation_reference
                  reference
                  standalone_hyperlink)
  interpreted_text (:choice _default_role _prefix_role _suffix_role)
  _default_role (:alias _interpreted_text "interpreted_text")
  _prefix_role (:seq (:alias _role_name_prefix role) (:alias _interpreted_text "interpreted_text"))
  _suffix_role (:seq
                (:alias _interpreted_text_prefix "interpreted_text")
                (:alias _role_name_suffix role))
  __newline (:pattern "\\r?\\n")
  __whitespace (:token (:repeat1 (:choice " " "\t" "\x0b" "\x0c" " ")))}}
