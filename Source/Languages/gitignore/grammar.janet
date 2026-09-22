# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "gitignore"
 :extras []
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {document (:repeat _line)
  _line (:seq (:choice (:choice comment pattern) :blank) (:choice _trailing_spaces :blank) _newline)
  comment (:pattern "#[^\\n]*")
  pattern (:seq
           (:choice (:alias "!" negation) :blank)
           (:choice (:field :relative_flag _directory_separator) :blank)
           _pattern
           (:repeat (:seq (:field :relative_flag _directory_separator) _pattern))
           (:choice (:field :directory_flag _directory_separator) :blank))
  _directory_separator (:choice directory_separator directory_separator_escaped)
  directory_separator "/"
  directory_separator_escaped "\\/"
  _pattern (:repeat1 (:choice pattern_char pattern_char_escaped _wildcard bracket_expr))
  pattern_char (:pattern "[^\\n/*?]")
  pattern_char_escaped (:seq "\\" (:pattern "[^\\n/]"))
  _wildcard (:choice
             (:alias "?" wildcard_char_single)
             (:alias "*" wildcard_chars)
             (:alias "**" wildcard_chars_allow_slash))
  bracket_expr (:seq
                "["
                (:choice (:alias (:choice "!" "^") bracket_negation) :blank)
                (:choice
                 (:seq _bracket_pattern_closing_bracket (:repeat _bracket_pattern))
                 (:repeat1 _bracket_pattern))
                "]")
  _bracket_pattern (:choice _bracket_char bracket_range bracket_char_class)
  _bracket_pattern_closing_bracket (:choice
                                    _bracket_char_closing_bracket
                                    (:alias _bracket_range_closing_bracket bracket_range))
  _bracket_char_closing_bracket (:alias "]" bracket_char)
  _bracket_range_closing_bracket (:seq _bracket_char_closing_bracket "-" _bracket_char)
  _bracket_char (:choice bracket_char bracket_char_escaped)
  bracket_char (:pattern "[^\\n/\\]]")
  bracket_char_escaped (:seq "\\" (:pattern "[^\\n/]"))
  bracket_range (:seq _bracket_char "-" _bracket_char)
  bracket_char_class (:choice
                      (:seq "[:" (:field :name "alnum") ":]")
                      (:seq "[:" (:field :name "alpha") ":]")
                      (:seq "[:" (:field :name "blank") ":]")
                      (:seq "[:" (:field :name "cntrl") ":]")
                      (:seq "[:" (:field :name "digit") ":]")
                      (:seq "[:" (:field :name "graph") ":]")
                      (:seq "[:" (:field :name "lower") ":]")
                      (:seq "[:" (:field :name "print") ":]")
                      (:seq "[:" (:field :name "punct") ":]")
                      (:seq "[:" (:field :name "space") ":]")
                      (:seq "[:" (:field :name "upper") ":]")
                      (:seq "[:" (:field :name "xdigit") ":]"))
  _trailing_spaces (:pattern " +")
  _newline (:pattern "\\r?\\n")}}
