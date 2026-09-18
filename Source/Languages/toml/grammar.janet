# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "toml"
 :extras [comment (:pattern "[ \\t]")]
 :conflicts []
 :precedences []
 :externals [_line_ending_or_eof
             _multiline_basic_string_content
             _multiline_basic_string_end
             _multiline_literal_string_content
             _multiline_literal_string_end]
 :inline []
 :supertypes []
 :rules
 {document (:seq
            (:repeat (:choice pair (:pattern "\\r?\\n")))
            (:repeat (:choice table table_array_element)))
  comment (:token (:prec -1 (:seq "#" (:repeat (:pattern "[^\\x00-\\x08\\x0a-\\x1f\\x7f]")))))
  table (:seq
         "["
         (:choice dotted_key _key)
         "]"
         _line_ending_or_eof
         (:repeat (:choice pair (:pattern "\\r?\\n"))))
  table_array_element (:seq
                       "[["
                       (:choice dotted_key _key)
                       "]]"
                       _line_ending_or_eof
                       (:repeat (:choice pair (:pattern "\\r?\\n"))))
  pair (:seq _inline_pair _line_ending_or_eof)
  _inline_pair (:seq (:choice dotted_key _key) "=" _inline_value)
  _key (:choice bare_key quoted_key)
  dotted_key (:seq (:choice dotted_key _key) "." _key)
  bare_key (:pattern "[A-Za-z0-9_-]+")
  quoted_key (:choice _basic_string _literal_string)
  _inline_value (:choice
                 string
                 integer
                 float
                 boolean
                 offset_date_time
                 local_date_time
                 local_date
                 local_time
                 array
                 inline_table)
  string (:choice _basic_string _multiline_basic_string _literal_string _multiline_literal_string)
  _basic_string (:seq
                 "\""
                 (:repeat
                  (:choice
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\x00-\\x08\\x0a-\\x1f\\x22\\x5c\\x7f]")))
                   escape_sequence))
                 (:token-immediate "\""))
  _multiline_basic_string (:seq
                           "\"\"\""
                           (:repeat
                            (:choice
                             (:token-immediate
                              (:repeat1 (:pattern "[^\\x00-\\x08\\x0a-\\x1f\\x22\\x5c\\x7f]")))
                             _multiline_basic_string_content
                             (:token-immediate (:pattern "\\r?\\n"))
                             escape_sequence
                             (:alias _escape_line_ending escape_sequence)))
                           _multiline_basic_string_end)
  escape_sequence (:token-immediate
                   (:pattern "\\\\([btnfr\"\\\\]|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8})"))
  _escape_line_ending (:token-immediate (:seq (:pattern "\\\\") (:pattern "\\r?\\n")))
  _literal_string (:seq
                   "'"
                   (:choice
                    (:token-immediate (:repeat1 (:pattern "[^\\x00-\\x08\\x0a-\\x1f\\x27\\x7f]")))
                    :blank)
                   (:token-immediate "'"))
  _multiline_literal_string (:seq
                             "'''"
                             (:repeat
                              (:choice
                               (:token-immediate
                                (:repeat1 (:pattern "[^\\x00-\\x08\\x0a-\\x1f\\x27\\x7f]")))
                               _multiline_literal_string_content
                               (:token-immediate (:pattern "\\r?\\n"))))
                             _multiline_literal_string_end)
  integer (:choice
           (:pattern "[+-]?(0|[1-9](_?[0-9])*)")
           (:pattern "0x[0-9a-fA-F](_?[0-9a-fA-F])*")
           (:pattern "0o[0-7](_?[0-7])*")
           (:pattern "0b[01](_?[01])*"))
  float (:choice
         (:token
          (:seq
           (:pattern "[+-]?(0|[1-9](_?[0-9])*)")
           (:choice
            (:pattern "[.][0-9](_?[0-9])*")
            (:seq
             (:choice (:pattern "[.][0-9](_?[0-9])*") :blank)
             (:seq (:pattern "[eE]") (:pattern "[+-]?[0-9](_?[0-9])*"))))))
         (:pattern "[+-]?(inf|nan)"))
  boolean (:pattern "true|false")
  offset_date_time (:token
                    (:seq
                     (:pattern "([0-9]+)-(0[1-9]|1[012])-(0[1-9]|[12][0-9]|3[01])")
                     (:pattern "[ tT]")
                     (:pattern "([01][0-9]|2[0-3]):([0-5][0-9]):([0-5][0-9]|60)([.][0-9]+)?")
                     (:pattern "([zZ])|([+-]([01][0-9]|2[0-3]):[0-5][0-9])")))
  local_date_time (:token
                   (:seq
                    (:pattern "([0-9]+)-(0[1-9]|1[012])-(0[1-9]|[12][0-9]|3[01])")
                    (:pattern "[ tT]")
                    (:pattern "([01][0-9]|2[0-3]):([0-5][0-9]):([0-5][0-9]|60)([.][0-9]+)?")))
  local_date (:pattern "([0-9]+)-(0[1-9]|1[012])-(0[1-9]|[12][0-9]|3[01])")
  local_time (:pattern "([01][0-9]|2[0-3]):([0-5][0-9]):([0-5][0-9]|60)([.][0-9]+)?")
  array (:seq
         "["
         (:repeat (:pattern "\\r?\\n"))
         (:choice
          (:seq
           _inline_value
           (:repeat (:pattern "\\r?\\n"))
           (:repeat
            (:seq "," (:repeat (:pattern "\\r?\\n")) _inline_value (:repeat (:pattern "\\r?\\n"))))
           (:choice (:seq "," (:repeat (:pattern "\\r?\\n"))) :blank))
          :blank)
         "]")
  inline_table (:seq
                "{"
                (:choice
                 (:seq (:alias _inline_pair pair) (:repeat (:seq "," (:alias _inline_pair pair))))
                 :blank)
                "}")}}
