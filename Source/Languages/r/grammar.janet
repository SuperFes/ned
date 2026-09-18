# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "r"
 :word identifier
 :extras [comment (:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals [_start
             _newline
             _semicolon
             _raw_string_open
             _raw_string_content
             _raw_string_close
             _external_else
             _external_open_parenthesis
             _external_close_parenthesis
             _external_open_brace
             _external_close_brace
             _external_open_bracket
             _external_close_bracket
             _external_open_bracket2
             _external_close_bracket2
             _error_sentinel]
 :inline [_identifier _string_or_identifier]
 :supertypes []
 :rules
 {program (:seq _start (:repeat (:choice _expression _semicolon _newline)))
  function_definition (:prec-left 2
                       (:seq
                        (:field :name (:choice "\\" "function"))
                        (:repeat _newline)
                        (:field :parameters parameters)
                        (:repeat _newline)
                        (:field :body _expression)))
  parameters (:seq
              (:field :open _open_parenthesis)
              (:choice
               (:seq
                (:field :parameter parameter)
                (:repeat (:seq comma (:field :parameter parameter))))
               :blank)
              (:field :close _close_parenthesis))
  parameter (:choice _parameter_with_default _parameter_without_default)
  _parameter_with_default (:seq _parameter_name "=" (:field :default _expression))
  _parameter_without_default _parameter_name
  _parameter_name (:field :name _identifier)
  if_statement (:prec-right 3
                (:seq
                 "if"
                 (:repeat _newline)
                 (:field :open _open_parenthesis)
                 (:field :condition _expression)
                 (:field :close _close_parenthesis)
                 (:repeat _newline)
                 (:field :consequence _expression)
                 (:choice (:seq _else (:repeat _newline) (:field :alternative _expression)) :blank)))
  for_statement (:prec-left 2
                 (:seq
                  "for"
                  (:repeat _newline)
                  (:field :open _open_parenthesis)
                  (:field :variable _identifier)
                  "in"
                  (:field :sequence _expression)
                  (:field :close _close_parenthesis)
                  (:repeat _newline)
                  (:field :body _expression)))
  while_statement (:prec-left 2
                   (:seq
                    "while"
                    (:repeat _newline)
                    (:field :open _open_parenthesis)
                    (:field :condition _expression)
                    (:field :close _close_parenthesis)
                    (:repeat _newline)
                    (:field :body _expression)))
  repeat_statement (:prec-left 2 (:seq "repeat" (:repeat _newline) (:field :body _expression)))
  braced_expression (:prec 0
                     (:seq
                      (:field :open _open_brace)
                      (:repeat (:field :body (:choice _expression _semicolon _newline)))
                      (:field :close _close_brace)))
  parenthesized_expression (:prec 0
                            (:seq
                             (:field :open _open_parenthesis)
                             (:field :body _expression)
                             (:field :close _close_parenthesis)))
  call (:prec-right 20
        (:seq (:field :function _expression) (:field :arguments (:alias call_arguments arguments))))
  subset (:prec-right 20
          (:seq
           (:field :function _expression)
           (:field :arguments (:alias subset_arguments arguments))))
  subset2 (:prec-right 20
           (:seq
            (:field :function _expression)
            (:field :arguments (:alias subset2_arguments arguments))))
  call_arguments (:seq
                  (:field :open _open_parenthesis)
                  (:seq
                   (:choice (:field :argument argument) :blank)
                   (:repeat (:seq comma (:choice (:field :argument argument) :blank))))
                  (:field :close _close_parenthesis))
  subset_arguments (:seq
                    (:field :open _open_bracket)
                    (:seq
                     (:choice (:field :argument argument) :blank)
                     (:repeat (:seq comma (:choice (:field :argument argument) :blank))))
                    (:field :close _close_bracket))
  subset2_arguments (:seq
                     (:field :open _open_bracket2)
                     (:seq
                      (:choice (:field :argument argument) :blank)
                      (:repeat (:seq comma (:choice (:field :argument argument) :blank))))
                     (:field :close _close_bracket2))
  argument (:choice _argument_named _argument_unnamed)
  _argument_named (:seq
                   (:field :name _argument_name_string_or_identifier_or_null)
                   "="
                   (:choice _argument_value :blank))
  _argument_unnamed _argument_value
  _argument_value (:field :value _expression)
  unary_operator (:choice
                  (:prec-left 1
                   (:seq (:field :operator "?") (:repeat _newline) (:field :rhs _expression)))
                  (:prec-left 7
                   (:seq (:field :operator "~") (:repeat _newline) (:field :rhs _expression)))
                  (:prec-left 10
                   (:seq (:field :operator "!") (:repeat _newline) (:field :rhs _expression)))
                  (:prec-left 16
                   (:seq (:field :operator "+") (:repeat _newline) (:field :rhs _expression)))
                  (:prec-left 16
                   (:seq (:field :operator "-") (:repeat _newline) (:field :rhs _expression))))
  binary_operator (:choice
                   (:prec-left 1
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "?")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 7
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "~")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-right 4
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "<-")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-right 4
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "<<-")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-right 4
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator ":=")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 6
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "->")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 6
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "->>")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-right 5
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "=")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 8
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "|")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 9
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "&")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 8
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "||")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 9
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "&&")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 11
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "<")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 11
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "<=")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 11
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator ">")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 11
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator ">=")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 11
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "==")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 11
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "!=")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 12
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "+")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 12
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "-")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 13
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "*")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 13
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "/")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-right 17
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "**")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-right 17
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "^")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 14
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator (:alias (:pattern "%[^%\\\\\\n]*%") "special"))
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 14
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator "|>")
                     (:repeat _newline)
                     (:field :rhs _expression)))
                   (:prec-left 15
                    (:seq
                     (:field :lhs _expression)
                     (:field :operator ":")
                     (:repeat _newline)
                     (:field :rhs _expression))))
  extract_operator (:choice
                    (:prec-right 18
                     (:seq
                      (:field :lhs _expression)
                      (:field :operator "$")
                      (:repeat _newline)
                      (:choice (:field :rhs _string_or_identifier) :blank)))
                    (:prec-right 18
                     (:seq
                      (:field :lhs _expression)
                      (:field :operator "@")
                      (:repeat _newline)
                      (:choice (:field :rhs _string_or_identifier) :blank))))
  namespace_operator (:choice
                      (:prec-right 19
                       (:seq
                        (:field :lhs _string_or_identifier)
                        (:field :operator "::")
                        (:choice (:field :rhs _string_or_identifier) :blank)))
                      (:prec-right 19
                       (:seq
                        (:field :lhs _string_or_identifier)
                        (:field :operator ":::")
                        (:choice (:field :rhs _string_or_identifier) :blank))))
  integer (:seq _float_literal "L")
  complex (:seq _float_literal "i")
  float _float_literal
  _hex_literal (:pattern "0[xX](([0-9a-fA-F]+(\\.[0-9a-fA-F]*)?)|(\\.[0-9a-fA-F]*))([pP][+-]?[0-9]+)?")
  _number_literal (:pattern "(?:(?:\\d+(?:\\.\\d*)?)|(?:\\.\\d+))(?:[eE][+-]?\\d*)?")
  _float_literal (:choice _hex_literal _number_literal)
  string (:choice _raw_string _single_quoted_string _double_quoted_string)
  _raw_string (:seq
               (:field :open (:alias _raw_string_open string_open))
               (:choice (:field :content (:alias _raw_string_content string_content)) :blank)
               (:field :close (:alias _raw_string_close string_close)))
  _single_quoted_string (:seq
                         (:field :open (:alias "'" string_open))
                         (:choice
                          (:field :content (:alias _single_quoted_string_content string_content))
                          :blank)
                         (:field :close (:alias "'" string_close)))
  _double_quoted_string (:seq
                         (:field :open (:alias "\"" string_open))
                         (:choice
                          (:field :content (:alias _double_quoted_string_content string_content))
                          :blank)
                         (:field :close (:alias "\"" string_close)))
  _single_quoted_string_content (:repeat1 (:choice (:pattern "[^'\\\\]+") escape_sequence))
  _double_quoted_string_content (:repeat1 (:choice (:pattern "[^\"\\\\]+") escape_sequence))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^0-9xuU]")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9a-fA-F]{1,2}")
                     (:pattern "u[0-9a-fA-F]{1,4}")
                     (:pattern "u\\{[0-9a-fA-F]{1,4}\\}")
                     (:pattern "U[0-9a-fA-F]{1,8}")
                     (:pattern "U\\{[0-9a-fA-F]{1,8}\\}"))))
  dots "..."
  dot_dot_i (:pattern "[.][.]\\d+")
  identifier (:token
              (:choice
               (:pattern "[\\p{XID_Start}_][\\p{XID_Continue}.]*")
               (:pattern "\\.(?:[\\p{XID_Start}._][\\p{XID_Continue}.]*)?")
               (:pattern "`((?:\\\\(.|\\n))|[^`\\\\])*`")))
  _identifier (:choice dots dot_dot_i identifier)
  _string_or_identifier (:choice string _identifier)
  _argument_name_string_or_identifier_or_null (:prec 1 (:choice _string_or_identifier null))
  next "next"
  break "break"
  (:ref "true") "TRUE"
  (:ref "false") "FALSE"
  null "NULL"
  inf "Inf"
  nan "NaN"
  na (:choice "NA" "NA_integer_" "NA_real_" "NA_complex_" "NA_character_")
  _expression (:choice
               function_definition
               if_statement
               for_statement
               while_statement
               repeat_statement
               braced_expression
               parenthesized_expression
               call
               subset
               subset2
               unary_operator
               binary_operator
               extract_operator
               namespace_operator
               integer
               complex
               float
               string
               identifier
               dots
               dot_dot_i
               next
               break
               (:ref "true")
               (:ref "false")
               null
               inf
               nan
               na)
  comment (:token (:prec -1 (:seq "#" (:pattern "[^\\r\\n]*"))))
  comma ","
  _else (:alias _external_else "else")
  _open_parenthesis (:alias _external_open_parenthesis "(")
  _close_parenthesis (:alias _external_close_parenthesis ")")
  _open_brace (:alias _external_open_brace "{")
  _close_brace (:alias _external_close_brace "}")
  _open_bracket (:alias _external_open_bracket "[")
  _close_bracket (:alias _external_close_bracket "]")
  _open_bracket2 (:alias _external_open_bracket2 "[[")
  _close_bracket2 (:alias _external_close_bracket2 "]]")}}
