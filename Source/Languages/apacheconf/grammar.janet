# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "apacheconf"
 :extras [(:pattern "[ \\t]")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source_file (:repeat _directives)
  comment (:pattern "#.*\\n")
  _directives (:choice comment _end_of_line simple_directive tag_directive)
  simple_directive (:seq (:field :name _simple_directive_name) (:repeat simple_param) _end_of_line)
  _simple_directive_name (:choice non_quoted_simple_directive_name string)
  non_quoted_simple_directive_name (:pattern "[^\"'\\s<#\\\\](\\\\\\n|[^\\s\\\\])*")
  simple_param (:choice non_string_param string line_continuation)
  non_string_param (:pattern "[^\"'\\s][^\\\\\\s]*")
  tag_directive (:seq
                 "<"
                 (:field :name tag_name)
                 _optional_spaces
                 (:repeat tag_param)
                 (:repeat (:seq (:alias ">" tag_param) _optional_spaces))
                 ">"
                 _optional_spaces
                 _end_of_line
                 (:repeat _directives)
                 "</"
                 (:field :end_name tag_name)
                 _optional_spaces
                 ">"
                 _end_of_line)
  tag_name (:token-immediate (:pattern "[^>\\s\\\\]([^>\\s\\\\]|[\\\\>][^>\\s\\\\])*"))
  _optional_spaces (:pattern "[ \\t]*")
  tag_param (:choice string non_string_tag_param line_continuation)
  non_string_tag_param (:pattern "([>\\\\]|[^\"'\">\\s\\\\])([^>\\s\\\\]|[>\\\\][^>\\s\\\\])*")
  _end_of_line (:pattern "\\r?\\n")
  line_continuation (:pattern "[\\\\]\\r?\\n")
  string (:choice
          (:seq "'" (:repeat (:choice singlequote_string_content line_continuation "\\'" "\\")) "'")
          (:seq
           "\""
           (:repeat (:choice doublequote_string_content line_continuation "\\\"" "\\"))
           "\""))
  singlequote_string_content (:pattern "[^\\'\\r\\n\\\\]+")
  doublequote_string_content (:pattern "[^\\\"\\r\\n\\\\]+")}}
