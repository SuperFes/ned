# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "asciidoc"
 :extras [_NEWLINE]
 :conflicts []
 :precedences []
 :externals [_eof
             title_h0_marker
             title_h1_marker
             title_h2_marker
             title_h3_marker
             title_h4_marker
             title_h5_marker
             list_marker_star
             list_marker_hyphen
             list_marker_dot
             list_marker_digit
             list_marker_geek
             list_marker_alpha
             document_attr_marker
             element_attr_marker
             block_title_marker
             breaks_marker
             table_block_marker
             ntable_block_marker
             table_cell_attr
             delimited_block_start_marker
             delimited_block_end_marker
             listing_block_start_marker
             listing_block_end_marker
             literal_block_marker
             quoted_block_start_marker
             quoted_block_end_marker
             quoted_block_md_marker
             quoted_paragraph_marker
             open_block_marker
             passthrough_block_marker
             block_macro_name
             callout_marker
             callout_list_marker
             line_comment_marker
             block_comment_start_marker
             block_comment_end_marker
             admonition_note
             admonition_tip
             admonition_important
             admonition_caution
             admonition_warning
             ident_marker
             list_continuation
             sidebar_block_start_marker
             sidebar_block_end_marker
             csv_table_block_marker
             dsv_table_block_marker
             term
             description_marker]
 :inline []
 :supertypes []
 :rules
 {document (:repeat block_element)
  list (:choice unordered_list ordered_list checked_list)
  checked_list (:prec-left 0 (:repeat1 checked_list_item))
  checked_list_item (:prec-left 0
                     (:seq
                      checked_list_marker
                      _WHITE_SPACE
                      line
                      (:choice (:seq list_continuation block_element) :blank)))
  checked_list_marker (:seq
                       unordered_list_marker
                       _WHITE_SPACE
                       (:choice checked_list_marker_checked checked_list_marker_unchecked))
  checked_list_marker_checked (:token (:prec 1 (:pattern "\\[[*x]\\]")))
  checked_list_marker_unchecked (:token (:prec 1 "[ ]"))
  unordered_list (:prec-right 0 (:repeat1 unordered_list_item))
  unordered_list_item (:prec-left 0
                       (:seq
                        unordered_list_marker
                        _WHITE_SPACE
                        line
                        (:choice (:seq list_continuation block_element) :blank)))
  unordered_list_marker (:choice list_marker_star list_marker_hyphen)
  ordered_list (:prec-right 0 (:repeat1 ordered_list_item))
  ordered_list_item (:prec-left 0
                     (:seq
                      ordered_list_marker
                      _WHITE_SPACE
                      line
                      (:choice (:seq list_continuation block_element) :blank)))
  ordered_list_marker (:choice list_marker_digit list_marker_geek list_marker_alpha list_marker_dot)
  callout_list (:prec-left 0 (:repeat1 callout_list_item))
  callout_list_item (:prec-left 0
                     (:seq
                      callout_list_marker
                      _WHITE_SPACE
                      line
                      (:choice (:seq list_continuation block_element) :blank)))
  document_title (:seq
                  title_h0_marker
                  _WHITE_SPACE
                  line
                  (:choice (:seq author_line (:seq _NEWLINE (:choice revision_line :blank))) :blank)
                  (:repeat (:choice document_attr block_macro))
                  _block_end)
  author_line (:seq author (:repeat (:seq (:seq ";" _WHITE_SPACE) author)))
  author (:seq
          (:alias _author_line_word firstname)
          _WHITE_SPACE
          (:choice (:seq (:alias _author_line_word middlename) _WHITE_SPACE) :blank)
          (:alias _author_line_word lastname)
          (:choice (:seq _WHITE_SPACE "<" email ">") :blank))
  _author_line_word (:repeat1 (:choice (:pattern "[^ ;\\r\\n]") "\\ " "\\;"))
  email (:pattern "(?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*|\"(?:[\\x01-\\x08\\x0b\\x0c\\x0e-\\x1f\\x21\\x23-\\x5b\\x5d-\\x7f]|\\\\[\\x01-\\x09\\x0b\\x0c\\x0e-\\x7f])*\")@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\\.)+[a-z0-9](?:[a-z0-9-]*[a-z0-9])?|\\[(?:(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9]))\\.){3}(?:(2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9])|[a-z0-9-]*[a-z0-9]:(?:[\\x01-\\x08\\x0b\\x0c\\x0e-\\x1f\\x21-\\x5a\\x53-\\x7f]|\\\\[\\x01-\\x09\\x0b\\x0c\\x0e-\\x7f])+)\\])")
  revision_line (:seq revnumber "," _WHITE_SPACE revdate ":" _WHITE_SPACE revremark)
  revnumber (:repeat1 (:choice (:pattern "[^,\\r\\n]") "\\,"))
  revdate (:repeat1 (:choice (:pattern "[^:\\r\\n]") "\\:"))
  revremark (:repeat1 (:choice (:pattern "[^\\r\\n]")))
  document_attr (:seq
                 document_attr_marker
                 (:alias (:repeat1 (:choice (:pattern "[^:\\r\\n]") "\\:")) attr_name)
                 (:alias ":" document_attr_marker)
                 (:choice (:seq (:token-immediate " ") (:alias escaped_line line)) :blank)
                 _block_end)
  csv_table_block (:prec-left 0
                   (:seq csv_table_block_marker (:repeat csv_record) csv_table_block_marker))
  csv_record (:seq
              (:choice
               (:seq
                (:alias (:repeat1 (:pattern "[^,\\r\\n]")) table_cell_content)
                (:repeat
                 (:seq
                  ","
                  (:choice (:alias (:repeat1 (:pattern "[^,\\r\\n]")) table_cell_content) :blank))))
               (:seq
                ","
                (:choice (:alias (:repeat1 (:pattern "[^,\\r\\n]")) table_cell_content) :blank)
                (:repeat
                 (:seq
                  ","
                  (:choice (:alias (:repeat1 (:pattern "[^,\\r\\n]")) table_cell_content) :blank)))))
              _block_end)
  dsv_table_block (:prec-left 0
                   (:seq dsv_table_block_marker (:repeat dsv_record) dsv_table_block_marker))
  dsv_record (:seq
              (:choice
               (:seq
                (:alias (:repeat1 (:pattern "[^:\\r\\n]")) table_cell_content)
                (:repeat
                 (:seq
                  ":"
                  (:choice (:alias (:repeat1 (:pattern "[^:\\r\\n]")) table_cell_content) :blank))))
               (:seq
                ":"
                (:choice (:alias (:repeat1 (:pattern "[^:\\r\\n]")) table_cell_content) :blank)
                (:repeat
                 (:seq
                  ":"
                  (:choice (:alias (:repeat1 (:pattern "[^:\\r\\n]")) table_cell_content) :blank)))))
              _block_end)
  table_block (:prec-left 0
               (:seq
                table_block_marker
                (:repeat (:choice table_cell ntable_block))
                table_block_marker))
  table_cell (:prec-right 0
              (:seq
               (:choice table_cell_attr :blank)
               (:choice
                (:seq
                 "|"
                 (:token-immediate (:pattern "\\r?\\n"))
                 (:choice
                  (:seq
                   (:alias _section_block section_block)
                   (:repeat (:seq list_continuation (:alias _section_block section_block))))
                  :blank))
                (:seq "|" table_cell_content))))
  table_cell_content (:repeat1 (:choice (:pattern "[^|]") "\\|"))
  ntable_block (:prec-left 0
                (:seq
                 (:choice element_attr :blank)
                 ntable_block_marker
                 (:repeat ntable_cell)
                 ntable_block_marker))
  ntable_cell (:seq
               (:choice table_cell_attr :blank)
               (:choice
                (:seq
                 "!"
                 (:token-immediate (:pattern "\\r?\\n"))
                 (:alias _section_block section_block))
                (:seq "!" (:repeat1 (:choice (:pattern "[^!]") "\\!")))))
  block_element (:prec-left 0
                 (:seq
                  (:choice document_title document_attr section_block line_comment block_comment)))
  section_block (:choice _section_block_para _section_block)
  _section_block_para (:seq (:repeat (:choice element_attr block_title)) paragraph)
  _section_block (:seq
                  (:repeat (:choice element_attr block_title))
                  (:choice
                   title1
                   title2
                   title3
                   title4
                   title5
                   list
                   description_list
                   table_block
                   csv_table_block
                   dsv_table_block
                   delimited_block
                   listing_block
                   literal_block
                   ident_block
                   open_block
                   breaks
                   admonition
                   quoted_block
                   quoted_md_block
                   passthrough_block
                   sidebar_block
                   block_macro))
  description_list (:prec-right 0 (:repeat1 description_list_item))
  description_list_item (:prec-right 0 (:seq term description_marker (:choice line :blank)))
  title1 (:seq title_h1_marker _WHITE_SPACE line)
  title2 (:seq title_h2_marker _WHITE_SPACE line)
  title3 (:seq title_h3_marker _WHITE_SPACE line)
  title4 (:seq title_h4_marker _WHITE_SPACE line)
  title5 (:seq title_h5_marker _WHITE_SPACE line)
  breaks (:seq breaks_marker _block_end)
  admonition (:seq
              (:choice
               admonition_note
               admonition_tip
               admonition_important
               admonition_caution
               admonition_warning)
              ":"
              (:token-immediate " ")
              line)
  block_macro (:seq
               block_macro_name
               "::"
               (:alias (:repeat (:choice (:pattern "[^\\[]") "\\[")) target)
               "["
               (:choice (:seq block_macro_attr (:repeat (:seq "," block_macro_attr))) :blank)
               "]"
               _block_end)
  block_macro_attr (:seq attribute_name (:choice (:seq "=" attribute_value) :blank))
  attribute_name (:repeat1 (:choice (:pattern "[^=]") "\\="))
  attribute_value (:repeat1 (:choice (:pattern "[^\\]]") "\\]"))
  escaped_line (:repeat1
                (:choice
                 (:pattern "[^/\\n]")
                 (:pattern "\\/[^*]")
                 (:pattern "\\\\\\r?\\n")
                 (:seq hard_wrap)))
  hard_wrap " +"
  element_attr (:seq
                element_attr_marker
                (:alias (:token (:repeat (:choice (:pattern "[^\\]\\r\\n]") "\\]"))) attr_value)
                (:alias "]" element_attr_marker)
                _NEWLINE)
  block_title (:seq block_title_marker line)
  delimited_block (:prec-left 0
                   (:seq
                    delimited_block_start_marker
                    (:repeat section_block)
                    delimited_block_end_marker))
  open_block (:prec-left 0 (:seq open_block_marker (:repeat section_block) open_block_marker))
  sidebar_block (:prec-left 0
                 (:seq sidebar_block_start_marker (:repeat section_block) sidebar_block_end_marker))
  passthrough_block (:seq
                     passthrough_block_marker
                     (:alias (:repeat (:seq (:pattern "[^\\r\\n]*") _NEWLINE)) listing_block_body)
                     passthrough_block_marker)
  listing_block (:prec-left 0
                 (:seq
                  listing_block_start_marker
                  (:alias
                   (:repeat
                    (:choice
                     (:seq (:repeat1 (:choice (:pattern "[^\\r\\n]") callout_marker)) _block_end)
                     block_macro))
                   listing_block_body)
                  listing_block_end_marker
                  (:choice callout_list :blank)))
  literal_block (:prec-left 0
                 (:seq
                  literal_block_marker
                  (:alias
                   (:repeat
                    (:choice
                     (:seq (:repeat1 (:choice (:pattern "[^\\r\\n]") callout_marker)) _block_end)
                     block_macro))
                   literal_block_body)
                  literal_block_marker
                  (:choice callout_list :blank)))
  ident_block (:prec-left 0 (:seq (:repeat1 ident_block_line) (:choice callout_list :blank)))
  ident_block_line (:seq
                    ident_marker
                    (:repeat1 (:choice (:pattern "[^\\r\\n]") callout_marker))
                    _block_end)
  line (:seq (:pattern "[^\\r\\n]+") _block_end)
  paragraph (:prec-left 0 (:prec -1 (:seq (:repeat1 line) (:choice quoted_line :blank))))
  quoted_line (:seq quoted_paragraph_marker (:token-immediate " ") line)
  line_comment (:seq
                line_comment_marker
                (:choice (:alias (:pattern "[^\\r\\n]+") body) :blank)
                _block_end)
  block_comment (:seq
                 block_comment_start_marker
                 (:alias (:repeat (:seq (:pattern "[^\\r\\n]+") _NEWLINE)) body)
                 block_comment_end_marker)
  quoted_block (:prec-left 0
                (:seq
                 quoted_block_start_marker
                 (:repeat (:choice line quoted_block))
                 quoted_block_end_marker))
  quoted_md_block (:prec-right 0
                   (:repeat1
                    (:choice
                     (:seq quoted_block_md_marker _NEWLINE)
                     (:seq quoted_block_md_marker (:token-immediate " ") line))))
  _block_end (:choice _NEWLINE _eof)
  _NEWLINE (:pattern "\\r?\\n")
  _WHITE_SPACE (:pattern "[ \\t]+")}}
