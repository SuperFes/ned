# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "gitattributes"
 :word attr_name
 :extras []
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {file (:repeat _line)
  _line (:seq
         (:choice _space :blank)
         (:choice (:choice comment _attr_list macro_def) :blank)
         (:choice _eol _eof))
  _attr_list (:seq
              (:prec-left 0 (:choice pattern quoted_pattern))
              (:repeat1 (:seq _space attribute))
              (:choice _space :blank))
  pattern (:seq
           (:choice (:alias "!" pattern_negation) :blank)
           (:choice (:field :absolute dir_sep) :blank)
           _pattern
           (:repeat (:seq (:field :relative dir_sep) _pattern))
           (:choice (:alias dir_sep trailing_slash) :blank))
  _pattern (:repeat1
            (:choice
             _pattern_char
             wildcard
             escaped_char
             range_notation
             (:alias "\\" redundant_escape)))
  quoted_pattern (:seq
                  "\""
                  (:choice (:alias "!" pattern_negation) :blank)
                  (:choice (:field :absolute dir_sep) :blank)
                  _quoted_pattern
                  (:repeat (:seq (:field :relative dir_sep) _pattern))
                  (:choice (:alias dir_sep trailing_slash) :blank)
                  "\"")
  _quoted_pattern (:repeat1
                   (:choice
                    (:pattern "[^\\n/]")
                    (:choice ansi_c_escape escaped_char)
                    (:alias "\\" redundant_escape)))
  _pattern_char (:pattern "[^\\s/?*]")
  escaped_char (:pattern "\\\\[\\\\\\[\\]!?*]")
  ansi_c_escape (:prec-right 1 (:choice _special_char _char_code))
  _special_char (:pattern "\\\\[abeEfnrtv\\\\'\"?]")
  _char_code (:choice _octal_code _hex_code _unicode_code _control_code)
  _octal_code (:pattern "\\\\\\d{1,3}")
  _hex_code (:pattern "\\\\x[0-9A-Fa-f]{2}")
  _unicode_code (:choice (:pattern "\\\\u[0-9A-Fa-f]{4}") (:pattern "\\\\U[0-9A-Fa-f]{8}"))
  _control_code (:token
                 (:choice (:pattern "\\\\c[\\x00-\\x5B\\x5D-\\x7F]") (:pattern "\\\\c\\\\\\\\")))
  range_notation (:prec-left 0
                  (:seq
                   "["
                   (:choice (:alias (:token (:choice "!" "^")) range_negation) :blank)
                   (:repeat1 (:choice class_range character_class _class_char ansi_c_escape "-"))
                   "]"))
  class_range (:prec-right 2
               (:seq (:choice _class_char _char_code) "-" (:choice _class_char _char_code)))
  _class_char (:token (:choice (:pattern "[^-\\\\\\]\\n]") (:pattern "\\\\[-\\\\\\[\\]!^]")))
  character_class (:token
                   (:seq
                    "[:"
                    (:choice
                     "alnum"
                     "alpha"
                     "blank"
                     "cntrl"
                     "digit"
                     "graph"
                     "lower"
                     "print"
                     "punct"
                     "space"
                     "upper"
                     "xdigit")
                    ":]"))
  wildcard (:token (:choice "?" "*" "**"))
  dir_sep "/"
  attribute (:choice (:seq (:choice attr_name builtin_attr) _attr_value) _prefixed_attr)
  _prefixed_attr (:seq
                  (:choice (:choice (:alias "!" attr_reset) (:alias "-" attr_unset)) :blank)
                  (:choice attr_name builtin_attr)
                  (:prec -1 (:choice (:alias _attr_value ignored_value) :blank)))
  _attr_value (:seq
               (:alias "=" attr_set)
               (:choice
                (:prec 2 (:alias (:token (:choice "true" "false")) boolean_value))
                (:prec 1 (:alias (:pattern "\\S+") string_value))))
  attr_name (:pattern "[A-Za-z0-9_.][-A-Za-z0-9_.]*")
  builtin_attr (:prec 1
                (:choice
                 "text"
                 "eol"
                 "crlf"
                 "working-tree-encoding"
                 "ident"
                 "filter"
                 "diff"
                 "merge"
                 "whitespace"
                 "export-ignore"
                 "export-subst"
                 "delta"
                 "encoding"
                 "binary"))
  macro_def (:seq
             (:alias "[attr]" macro_tag)
             (:field :macro_name attr_name)
             (:repeat1 (:seq _space attribute))
             (:choice _space :blank))
  comment (:seq "#" (:repeat (:pattern "[^\\n]")))
  _eol (:pattern "\\r?\\n")
  _space (:prec -1 (:pattern "[ \\t]+"))
  _eof "\0"}}
