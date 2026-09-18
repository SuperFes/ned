# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "elixir"
 :extras [(:pattern "\\r?\\n")
          (:pattern "[ \\t]|\\r?\\n|\\\\\\r?\\n")
          comment
          _newline_before_comment
          _newline_before_binary_operator]
 :conflicts [[_expression _local_call_without_parentheses]
             [binary_operator _stab_clause_arguments_without_parentheses]
             [_stab_clause_arguments_without_parentheses _stab_clause_arguments_with_parentheses]
             [operator_identifier stab_clause]
             [unary_operator operator_identifier]
             [body]]
 :precedences []
 :externals [_quoted_content_i_single
             _quoted_content_i_double
             _quoted_content_i_heredoc_single
             _quoted_content_i_heredoc_double
             _quoted_content_i_parenthesis
             _quoted_content_i_curly
             _quoted_content_i_square
             _quoted_content_i_angle
             _quoted_content_i_bar
             _quoted_content_i_slash
             _quoted_content_single
             _quoted_content_double
             _quoted_content_heredoc_single
             _quoted_content_heredoc_double
             _quoted_content_parenthesis
             _quoted_content_curly
             _quoted_content_square
             _quoted_content_angle
             _quoted_content_bar
             _quoted_content_slash
             _newline_before_do
             _newline_before_binary_operator
             _newline_before_comment
             _before_unary_op
             _not_in
             _quoted_atom_start]
 :inline []
 :supertypes []
 :rules
 {source (:seq
          (:choice _terminator :blank)
          (:choice
           (:seq
            (:seq _expression (:repeat (:seq _terminator _expression)))
            (:choice _terminator :blank))
           :blank))
  _terminator (:prec-right 0
               (:choice (:seq (:repeat (:pattern "\\r?\\n")) ";") (:repeat1 (:pattern "\\r?\\n"))))
  _expression (:choice
               block
               identifier
               alias
               integer
               float
               char
               boolean
               (:ref "nil")
               _atom
               string
               charlist
               sigil
               list
               tuple
               bitstring
               map
               _nullary_operator
               unary_operator
               binary_operator
               dot
               call
               access_call
               anonymous_function)
  block (:seq
         "("
         (:choice _terminator :blank)
         (:choice
          (:choice
           (:seq (:choice stab_clause) (:repeat (:seq _terminator (:choice stab_clause))))
           (:seq
            (:seq (:choice _expression) (:repeat (:seq _terminator (:choice _expression))))
            (:choice _terminator :blank)))
          :blank)
         ")")
  identifier (:choice
              (:pattern "[_\\p{Ll}\\p{Lm}\\p{Lo}\\p{Nl}\\u1885\\u1886\\u2118\\u212E\\u309B\\u309C][\\p{ID_Continue}]*[?!]?" "u")
              "...")
  alias (:token
         (:seq
          (:pattern "[A-Z][_a-zA-Z0-9]*")
          (:repeat (:seq (:pattern "\\s*\\.\\s*") (:pattern "[A-Z][_a-zA-Z0-9]*")))))
  integer (:token
           (:choice
            (:seq (:pattern "[0-9]+") (:repeat (:seq "_" (:pattern "[0-9]+"))))
            (:seq "0b" (:seq (:pattern "[0-1]+") (:repeat (:seq "_" (:pattern "[0-1]+")))))
            (:seq "0o" (:seq (:pattern "[0-7]+") (:repeat (:seq "_" (:pattern "[0-7]+")))))
            (:seq
             "0x"
             (:seq (:pattern "[0-9a-fA-F]+") (:repeat (:seq "_" (:pattern "[0-9a-fA-F]+")))))))
  float (:token
         (:seq
          (:seq (:pattern "[0-9]+") (:repeat (:seq "_" (:pattern "[0-9]+"))))
          "."
          (:seq (:pattern "[0-9]+") (:repeat (:seq "_" (:pattern "[0-9]+"))))
          (:choice
           (:seq
            (:pattern "[eE]")
            (:choice (:choice "-" "+") :blank)
            (:choice
             (:seq (:pattern "[0-9]+") (:repeat (:seq "_" (:pattern "[0-9]+"))))
             (:seq "0b" (:seq (:pattern "[0-1]+") (:repeat (:seq "_" (:pattern "[0-1]+")))))
             (:seq "0o" (:seq (:pattern "[0-7]+") (:repeat (:seq "_" (:pattern "[0-7]+")))))
             (:seq
              "0x"
              (:seq (:pattern "[0-9a-fA-F]+") (:repeat (:seq "_" (:pattern "[0-9a-fA-F]+")))))))
           :blank)))
  char (:pattern "\\?(.|\\\\.)")
  boolean (:choice "true" "false")
  (:ref "nil") "nil"
  _atom (:choice atom quoted_atom)
  atom (:token
        (:seq
         ":"
         (:choice
          (:pattern "[\\p{ID_Start}_][\\p{ID_Continue}@]*[?!]?" "u")
          "->"
          "::"
          "|"
          "&"
          "="
          "^^^"
          "//"
          ".."
          "**"
          "."
          "@"
          "<-"
          "\\\\"
          "||"
          "|||"
          "&&"
          "&&&"
          "=="
          "!="
          "=~"
          "==="
          "!=="
          "<"
          ">"
          "<="
          ">="
          "|>"
          "<<<"
          ">>>"
          "<<~"
          "~>>"
          "<~"
          "~>"
          "<~>"
          "<|>"
          "++"
          "--"
          "+++"
          "---"
          "<>"
          "+"
          "-"
          "*"
          "/"
          "+"
          "-"
          "!"
          "^"
          "~~~"
          "..."
          "%{}"
          "{}"
          "%"
          "<<>>"
          "..//")))
  quoted_atom (:seq (:alias _quoted_atom_start ":") (:choice _quoted_i_double _quoted_i_single))
  _quoted_i_double (:seq
                    (:field :quoted_start "\"")
                    (:choice (:alias _quoted_content_i_double quoted_content) :blank)
                    (:repeat
                     (:seq
                      (:choice interpolation escape_sequence)
                      (:choice (:alias _quoted_content_i_double quoted_content) :blank)))
                    (:field :quoted_end "\""))
  _quoted_double (:seq
                  (:field :quoted_start "\"")
                  (:choice (:alias _quoted_content_double quoted_content) :blank)
                  (:repeat
                   (:seq
                    escape_sequence
                    (:choice (:alias _quoted_content_double quoted_content) :blank)))
                  (:field :quoted_end "\""))
  _quoted_i_single (:seq
                    (:field :quoted_start "'")
                    (:choice (:alias _quoted_content_i_single quoted_content) :blank)
                    (:repeat
                     (:seq
                      (:choice interpolation escape_sequence)
                      (:choice (:alias _quoted_content_i_single quoted_content) :blank)))
                    (:field :quoted_end "'"))
  _quoted_single (:seq
                  (:field :quoted_start "'")
                  (:choice (:alias _quoted_content_single quoted_content) :blank)
                  (:repeat
                   (:seq
                    escape_sequence
                    (:choice (:alias _quoted_content_single quoted_content) :blank)))
                  (:field :quoted_end "'"))
  _quoted_i_heredoc_single (:seq
                            (:field :quoted_start "'''")
                            (:choice
                             (:alias _quoted_content_i_heredoc_single quoted_content)
                             :blank)
                            (:repeat
                             (:seq
                              (:choice interpolation escape_sequence)
                              (:choice
                               (:alias _quoted_content_i_heredoc_single quoted_content)
                               :blank)))
                            (:field :quoted_end "'''"))
  _quoted_heredoc_single (:seq
                          (:field :quoted_start "'''")
                          (:choice (:alias _quoted_content_heredoc_single quoted_content) :blank)
                          (:repeat
                           (:seq
                            escape_sequence
                            (:choice (:alias _quoted_content_heredoc_single quoted_content) :blank)))
                          (:field :quoted_end "'''"))
  _quoted_i_heredoc_double (:seq
                            (:field :quoted_start "\"\"\"")
                            (:choice
                             (:alias _quoted_content_i_heredoc_double quoted_content)
                             :blank)
                            (:repeat
                             (:seq
                              (:choice interpolation escape_sequence)
                              (:choice
                               (:alias _quoted_content_i_heredoc_double quoted_content)
                               :blank)))
                            (:field :quoted_end "\"\"\""))
  _quoted_heredoc_double (:seq
                          (:field :quoted_start "\"\"\"")
                          (:choice (:alias _quoted_content_heredoc_double quoted_content) :blank)
                          (:repeat
                           (:seq
                            escape_sequence
                            (:choice (:alias _quoted_content_heredoc_double quoted_content) :blank)))
                          (:field :quoted_end "\"\"\""))
  _quoted_i_parenthesis (:seq
                         (:field :quoted_start "(")
                         (:choice (:alias _quoted_content_i_parenthesis quoted_content) :blank)
                         (:repeat
                          (:seq
                           (:choice interpolation escape_sequence)
                           (:choice (:alias _quoted_content_i_parenthesis quoted_content) :blank)))
                         (:field :quoted_end ")"))
  _quoted_parenthesis (:seq
                       (:field :quoted_start "(")
                       (:choice (:alias _quoted_content_parenthesis quoted_content) :blank)
                       (:repeat
                        (:seq
                         escape_sequence
                         (:choice (:alias _quoted_content_parenthesis quoted_content) :blank)))
                       (:field :quoted_end ")"))
  _quoted_i_curly (:seq
                   (:field :quoted_start "{")
                   (:choice (:alias _quoted_content_i_curly quoted_content) :blank)
                   (:repeat
                    (:seq
                     (:choice interpolation escape_sequence)
                     (:choice (:alias _quoted_content_i_curly quoted_content) :blank)))
                   (:field :quoted_end "}"))
  _quoted_curly (:seq
                 (:field :quoted_start "{")
                 (:choice (:alias _quoted_content_curly quoted_content) :blank)
                 (:repeat
                  (:seq
                   escape_sequence
                   (:choice (:alias _quoted_content_curly quoted_content) :blank)))
                 (:field :quoted_end "}"))
  _quoted_i_square (:seq
                    (:field :quoted_start "[")
                    (:choice (:alias _quoted_content_i_square quoted_content) :blank)
                    (:repeat
                     (:seq
                      (:choice interpolation escape_sequence)
                      (:choice (:alias _quoted_content_i_square quoted_content) :blank)))
                    (:field :quoted_end "]"))
  _quoted_square (:seq
                  (:field :quoted_start "[")
                  (:choice (:alias _quoted_content_square quoted_content) :blank)
                  (:repeat
                   (:seq
                    escape_sequence
                    (:choice (:alias _quoted_content_square quoted_content) :blank)))
                  (:field :quoted_end "]"))
  _quoted_i_angle (:seq
                   (:field :quoted_start "<")
                   (:choice (:alias _quoted_content_i_angle quoted_content) :blank)
                   (:repeat
                    (:seq
                     (:choice interpolation escape_sequence)
                     (:choice (:alias _quoted_content_i_angle quoted_content) :blank)))
                   (:field :quoted_end ">"))
  _quoted_angle (:seq
                 (:field :quoted_start "<")
                 (:choice (:alias _quoted_content_angle quoted_content) :blank)
                 (:repeat
                  (:seq
                   escape_sequence
                   (:choice (:alias _quoted_content_angle quoted_content) :blank)))
                 (:field :quoted_end ">"))
  _quoted_i_bar (:seq
                 (:field :quoted_start "|")
                 (:choice (:alias _quoted_content_i_bar quoted_content) :blank)
                 (:repeat
                  (:seq
                   (:choice interpolation escape_sequence)
                   (:choice (:alias _quoted_content_i_bar quoted_content) :blank)))
                 (:field :quoted_end "|"))
  _quoted_bar (:seq
               (:field :quoted_start "|")
               (:choice (:alias _quoted_content_bar quoted_content) :blank)
               (:repeat
                (:seq escape_sequence (:choice (:alias _quoted_content_bar quoted_content) :blank)))
               (:field :quoted_end "|"))
  _quoted_i_slash (:seq
                   (:field :quoted_start "/")
                   (:choice (:alias _quoted_content_i_slash quoted_content) :blank)
                   (:repeat
                    (:seq
                     (:choice interpolation escape_sequence)
                     (:choice (:alias _quoted_content_i_slash quoted_content) :blank)))
                   (:field :quoted_end "/"))
  _quoted_slash (:seq
                 (:field :quoted_start "/")
                 (:choice (:alias _quoted_content_slash quoted_content) :blank)
                 (:repeat
                  (:seq
                   escape_sequence
                   (:choice (:alias _quoted_content_slash quoted_content) :blank)))
                 (:field :quoted_end "/"))
  string (:choice _quoted_i_double _quoted_i_heredoc_double)
  charlist (:choice _quoted_i_single _quoted_i_heredoc_single)
  interpolation (:seq "#{" (:choice _expression :blank) "}")
  escape_sequence (:token
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^ux]")
                     (:pattern "x[0-9a-fA-F]{1,2}")
                     (:pattern "x\\{[0-9a-fA-F]+\\}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}")
                     (:pattern "u[0-9a-fA-F]{4}"))))
  sigil (:seq
         "~"
         (:choice
          (:seq
           (:alias (:token-immediate (:pattern "[a-z]")) sigil_name)
           (:choice
            _quoted_i_double
            _quoted_i_single
            _quoted_i_heredoc_single
            _quoted_i_heredoc_double
            _quoted_i_parenthesis
            _quoted_i_curly
            _quoted_i_square
            _quoted_i_angle
            _quoted_i_bar
            _quoted_i_slash))
          (:seq
           (:alias (:token-immediate (:pattern "[A-Z][A-Z0-9]*")) sigil_name)
           (:choice
            _quoted_double
            _quoted_single
            _quoted_heredoc_single
            _quoted_heredoc_double
            _quoted_parenthesis
            _quoted_curly
            _quoted_square
            _quoted_angle
            _quoted_bar
            _quoted_slash)))
         (:choice (:alias (:token-immediate (:pattern "[a-zA-Z0-9]+")) sigil_modifiers) :blank))
  keywords (:prec-right 0 (:seq pair (:repeat (:seq "," pair))))
  _keywords_with_trailing_separator (:seq
                                     (:seq pair (:repeat (:seq "," pair)))
                                     (:choice "," :blank))
  pair (:seq (:field :key _keyword) (:field :value _expression))
  _keyword (:choice keyword quoted_keyword)
  keyword (:token
           (:seq
            (:choice
             (:pattern "[\\p{ID_Start}_][\\p{ID_Continue}@]*[?!]?" "u")
             "->"
             "|"
             "&"
             "="
             "^^^"
             "//"
             ".."
             "**"
             "."
             "@"
             "<-"
             "\\\\"
             "||"
             "|||"
             "&&"
             "&&&"
             "=="
             "!="
             "=~"
             "==="
             "!=="
             "<"
             ">"
             "<="
             ">="
             "|>"
             "<<<"
             ">>>"
             "<<~"
             "~>>"
             "<~"
             "~>"
             "<~>"
             "<|>"
             "++"
             "--"
             "+++"
             "---"
             "<>"
             "+"
             "-"
             "*"
             "/"
             "+"
             "-"
             "!"
             "^"
             "~~~"
             "..."
             "%{}"
             "{}"
             "%"
             "<<>>"
             "..//")
            (:pattern ":\\s")))
  quoted_keyword (:seq
                  (:choice _quoted_i_double _quoted_i_single)
                  (:token-immediate (:pattern ":\\s")))
  list (:seq "[" (:choice _items_with_trailing_separator :blank) "]")
  tuple (:seq "{" (:choice _items_with_trailing_separator :blank) "}")
  bitstring (:seq "<<" (:choice _items_with_trailing_separator :blank) ">>")
  map (:prec 1
       (:seq
        "%"
        (:choice struct :blank)
        "{"
        (:choice (:alias _items_with_trailing_separator map_content) :blank)
        "}"))
  struct (:prec-left 0
          (:choice alias _atom identifier unary_operator dot (:alias _call_with_parentheses call)))
  _items_with_trailing_separator (:seq
                                  (:choice
                                   (:seq
                                    (:seq _expression (:repeat (:seq "," _expression)))
                                    (:choice "," :blank))
                                   (:seq
                                    (:choice
                                     (:seq (:seq _expression (:repeat (:seq "," _expression))) ",")
                                     :blank)
                                    (:alias _keywords_with_trailing_separator keywords))))
  _nullary_operator (:alias (:prec 160 "..") operator_identifier)
  unary_operator (:choice
                  (:prec-dynamic -1
                   (:prec 60
                    (:seq
                     (:choice _before_unary_op :blank)
                     (:field :operator "&")
                     (:field :operand _capture_expression))))
                  (:prec-dynamic -1
                   (:prec 200
                    (:seq
                     (:choice _before_unary_op :blank)
                     (:field :operator (:choice "+" "-" "!" "^" "~~~" "not"))
                     (:field :operand _expression))))
                  (:prec-dynamic -1
                   (:prec 220
                    (:seq
                     (:choice _before_unary_op :blank)
                     (:field :operator "@")
                     (:field :operand _expression))))
                  (:prec-dynamic -1
                   (:prec 235
                    (:seq
                     (:choice _before_unary_op :blank)
                     (:field :operator "&")
                     (:field :operand integer)))))
  _capture_expression (:choice (:prec 1 (:seq "(" _expression ")")) _expression)
  binary_operator (:choice
                   (:prec-left 10
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "<-" "\\\\"))
                     (:field :right _expression)))
                   (:prec-right 20
                    (:seq
                     (:field :left _expression)
                     (:field :operator "when")
                     (:field :right (:choice _expression keywords))))
                   (:prec-right 30
                    (:seq
                     (:field :left _expression)
                     (:field :operator "::")
                     (:field :right _expression)))
                   (:prec-right 40
                    (:seq
                     (:field :left _expression)
                     (:field :operator "|")
                     (:field :right (:choice _expression keywords))))
                   (:prec-right 50
                    (:seq
                     (:field :left _expression)
                     (:field :operator "=>")
                     (:field :right _expression)))
                   (:prec-right 70
                    (:seq
                     (:field :left _expression)
                     (:field :operator "=")
                     (:field :right _expression)))
                   (:prec-left 80
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "||" "|||" "or"))
                     (:field :right _expression)))
                   (:prec-left 90
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "&&" "&&&" "and"))
                     (:field :right _expression)))
                   (:prec-left 100
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "==" "!=" "=~" "===" "!=="))
                     (:field :right _expression)))
                   (:prec-left 110
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "<" ">" "<=" ">="))
                     (:field :right _expression)))
                   (:prec-left 120
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "|>" "<<<" ">>>" "<<~" "~>>" "<~" "~>" "<~>" "<|>"))
                     (:field :right _expression)))
                   (:prec-left 130
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "in" (:alias _not_in "not in")))
                     (:field :right _expression)))
                   (:prec-left 140
                    (:seq
                     (:field :left _expression)
                     (:field :operator "^^^")
                     (:field :right _expression)))
                   (:prec-right 150
                    (:seq
                     (:field :left _expression)
                     (:field :operator "//")
                     (:field :right _expression)))
                   (:prec-right 160
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "++" "--" "+++" "---" "<>"))
                     (:field :right _expression)))
                   (:prec-right 160
                    (:seq
                     (:field :left _expression)
                     (:field :operator "..")
                     (:field :right _expression)))
                   (:prec-left 170
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "+" "-"))
                     (:field :right _expression)))
                   (:prec-left 180
                    (:seq
                     (:field :left _expression)
                     (:field :operator (:choice "*" "/"))
                     (:field :right _expression)))
                   (:prec-left 190
                    (:seq
                     (:field :left _expression)
                     (:field :operator "**")
                     (:field :right _expression)))
                   (:prec-left 180
                    (:seq
                     (:field :left operator_identifier)
                     (:field :operator "/")
                     (:field :right integer))))
  operator_identifier (:choice
                       (:prec 60 "&")
                       (:prec 200 (:choice "+" "-" "!" "^" "~~~" "not"))
                       (:prec 220 "@")
                       "<-"
                       "\\\\"
                       "when"
                       "::"
                       "|"
                       "="
                       "||"
                       "|||"
                       "or"
                       "&&"
                       "&&&"
                       "and"
                       "=="
                       "!="
                       "=~"
                       "==="
                       "!=="
                       "<"
                       ">"
                       "<="
                       ">="
                       "|>"
                       "<<<"
                       ">>>"
                       "<<~"
                       "~>>"
                       "<~"
                       "~>"
                       "<~>"
                       "<|>"
                       "in"
                       (:alias _not_in "not in")
                       "^^^"
                       "++"
                       "--"
                       "+++"
                       "---"
                       "<>"
                       "*"
                       "/"
                       "**"
                       "->")
  dot (:prec 210
       (:seq
        (:field :left _expression)
        (:field :operator ".")
        (:field :right (:choice alias tuple))))
  call (:choice _call_without_parentheses _call_with_parentheses)
  _call_without_parentheses (:choice
                             _local_call_without_parentheses
                             _local_call_just_do_block
                             _remote_call_without_parentheses)
  _call_with_parentheses (:choice
                          _local_call_with_parentheses
                          _remote_call_with_parentheses
                          _anonymous_call
                          _double_call)
  _local_call_without_parentheses (:prec-left 0
                                   (:seq
                                    (:field :target identifier)
                                    (:alias _call_arguments_without_parentheses arguments)
                                    (:choice
                                     (:seq (:choice _newline_before_do :blank) do_block)
                                     :blank)))
  _local_call_with_parentheses (:prec-left 0
                                (:seq
                                 (:field :target identifier)
                                 (:alias _call_arguments_with_parentheses_immediate arguments)
                                 (:choice
                                  (:seq (:choice _newline_before_do :blank) do_block)
                                  :blank)))
  _local_call_just_do_block (:prec -1 (:seq (:field :target identifier) do_block))
  _remote_call_without_parentheses (:prec-left 0
                                    (:seq
                                     (:field :target (:alias _remote_dot dot))
                                     (:choice
                                      (:alias _call_arguments_without_parentheses arguments)
                                      :blank)
                                     (:choice
                                      (:seq (:choice _newline_before_do :blank) do_block)
                                      :blank)))
  _remote_call_with_parentheses (:prec-left 0
                                 (:seq
                                  (:field :target (:alias _remote_dot dot))
                                  (:alias _call_arguments_with_parentheses_immediate arguments)
                                  (:choice
                                   (:seq (:choice _newline_before_do :blank) do_block)
                                   :blank)))
  _remote_dot (:prec 210
               (:seq
                (:field :left _expression)
                (:field :operator ".")
                (:field :right
                 (:choice
                  identifier
                  (:alias
                   (:choice
                    "and"
                    "in"
                    "not"
                    "or"
                    "when"
                    "true"
                    "false"
                    "nil"
                    "after"
                    "catch"
                    "do"
                    "else"
                    "end"
                    "fn"
                    "rescue")
                   identifier)
                  operator_identifier
                  (:alias _quoted_i_double string)
                  (:alias _quoted_i_single charlist)))))
  _anonymous_call (:seq
                   (:field :target (:alias _anonymous_dot dot))
                   (:alias _call_arguments_with_parentheses arguments))
  _anonymous_dot (:prec 210 (:seq (:field :left _expression) (:field :operator ".")))
  _double_call (:prec-left 0
                (:seq
                 (:field :target
                  (:alias
                   (:choice
                    _local_call_with_parentheses
                    _remote_call_with_parentheses
                    _anonymous_call)
                   call))
                 (:alias _call_arguments_with_parentheses arguments)
                 (:choice (:seq (:choice _newline_before_do :blank) do_block) :blank)))
  _call_arguments_with_parentheses (:seq
                                    "("
                                    (:choice _call_arguments_with_trailing_separator :blank)
                                    ")")
  _call_arguments_with_parentheses_immediate (:seq
                                              (:token-immediate "(")
                                              (:choice
                                               _call_arguments_with_trailing_separator
                                               :blank)
                                              ")")
  _call_arguments_with_trailing_separator (:choice
                                           (:seq
                                            (:seq _expression (:repeat (:seq "," _expression)))
                                            (:choice
                                             (:seq
                                              ","
                                              (:alias _keywords_with_trailing_separator keywords))
                                             :blank))
                                           (:alias _keywords_with_trailing_separator keywords))
  _call_arguments_without_parentheses (:prec-dynamic -1
                                       (:prec-right 0
                                        (:choice
                                         (:seq
                                          (:seq _expression (:repeat (:seq "," _expression)))
                                          (:choice (:seq "," keywords) :blank))
                                         keywords)))
  do_block (:seq
            (:seq
             "do"
             (:choice _terminator :blank)
             (:choice
              (:choice
               (:seq (:choice stab_clause) (:repeat (:seq _terminator (:choice stab_clause))))
               (:seq
                (:seq (:choice _expression) (:repeat (:seq _terminator (:choice _expression))))
                (:choice _terminator :blank)))
              :blank))
            (:repeat (:choice after_block rescue_block catch_block else_block))
            "end")
  after_block (:seq
               "after"
               (:choice _terminator :blank)
               (:choice
                (:choice
                 (:seq (:choice stab_clause) (:repeat (:seq _terminator (:choice stab_clause))))
                 (:seq
                  (:seq (:choice _expression) (:repeat (:seq _terminator (:choice _expression))))
                  (:choice _terminator :blank)))
                :blank))
  rescue_block (:seq
                "rescue"
                (:choice _terminator :blank)
                (:choice
                 (:choice
                  (:seq (:choice stab_clause) (:repeat (:seq _terminator (:choice stab_clause))))
                  (:seq
                   (:seq (:choice _expression) (:repeat (:seq _terminator (:choice _expression))))
                   (:choice _terminator :blank)))
                 :blank))
  catch_block (:seq
               "catch"
               (:choice _terminator :blank)
               (:choice
                (:choice
                 (:seq (:choice stab_clause) (:repeat (:seq _terminator (:choice stab_clause))))
                 (:seq
                  (:seq (:choice _expression) (:repeat (:seq _terminator (:choice _expression))))
                  (:choice _terminator :blank)))
                :blank))
  else_block (:seq
              "else"
              (:choice _terminator :blank)
              (:choice
               (:choice
                (:seq (:choice stab_clause) (:repeat (:seq _terminator (:choice stab_clause))))
                (:seq
                 (:seq (:choice _expression) (:repeat (:seq _terminator (:choice _expression))))
                 (:choice _terminator :blank)))
               :blank))
  access_call (:prec 205
               (:seq
                (:field :target _expression)
                (:token-immediate "[")
                (:field :key _expression)
                "]"))
  stab_clause (:prec-right 0
               (:seq
                (:choice (:field :left _stab_clause_left) :blank)
                (:field :operator "->")
                (:choice (:field :right body) :blank)))
  _stab_clause_left (:choice
                     (:alias _stab_clause_arguments_with_parentheses arguments)
                     (:alias _stab_clause_arguments_with_parentheses_with_guard binary_operator)
                     (:alias _stab_clause_arguments_without_parentheses arguments)
                     (:alias _stab_clause_arguments_without_parentheses_with_guard binary_operator))
  _stab_clause_arguments_with_parentheses (:prec 1
                                           (:seq
                                            "("
                                            (:choice
                                             (:choice
                                              (:seq
                                               (:seq
                                                (:prec-right 20 _expression)
                                                (:repeat (:seq "," (:prec-right 20 _expression))))
                                               (:choice (:seq "," keywords) :blank))
                                              keywords)
                                             :blank)
                                            ")"))
  _stab_clause_arguments_without_parentheses (:prec 20
                                              (:choice
                                               (:seq
                                                (:seq
                                                 (:prec 20 _expression)
                                                 (:repeat (:seq "," (:prec 20 _expression))))
                                                (:choice (:seq "," keywords) :blank))
                                               keywords))
  _stab_clause_arguments_with_parentheses_with_guard (:seq
                                                      (:field :left
                                                       (:alias
                                                        _stab_clause_arguments_with_parentheses
                                                        arguments))
                                                      (:field :operator "when")
                                                      (:field :right _expression))
  _stab_clause_arguments_without_parentheses_with_guard (:prec-dynamic 1
                                                         (:seq
                                                          (:field :left
                                                           (:alias
                                                            _stab_clause_arguments_without_parentheses
                                                            arguments))
                                                          (:field :operator "when")
                                                          (:field :right _expression)))
  body (:choice
        _terminator
        (:seq
         (:choice _terminator :blank)
         (:seq _expression (:repeat (:seq _terminator _expression)))
         (:choice _terminator :blank)))
  anonymous_function (:seq
                      "fn"
                      (:choice _terminator :blank)
                      (:choice (:seq stab_clause (:repeat (:seq _terminator stab_clause))) :blank)
                      "end")
  comment (:token (:prec -1 (:seq "#" (:pattern ".*"))))}}
