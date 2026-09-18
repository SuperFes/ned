# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "bash"
 :word word
 :extras [comment (:pattern "\\s") (:pattern "\\\\\\r?\\n") (:pattern "\\\\( |\\t|\\v|\\f)")]
 :conflicts [[_expression command_name]
             [command variable_assignments]
             [redirected_statement command]
             [redirected_statement command_substitution]
             [function_definition command_name]
             [pipeline]]
 :precedences []
 :externals [heredoc_start
             simple_heredoc_body
             _heredoc_body_beginning
             heredoc_content
             heredoc_end
             file_descriptor
             _empty_value
             _concat
             variable_name
             test_operator
             regex
             _regex_no_slash
             _regex_no_space
             _expansion_word
             extglob_pattern
             _bare_dollar
             _brace_start
             _immediate_double_hash
             _external_expansion_sym_hash
             _external_expansion_sym_bang
             _external_expansion_sym_equal
             "}"
             "]"
             "<<"
             "<<-"
             (:pattern "\\n")
             "("
             "esac"
             __error_recovery]
 :inline [_statement
          _terminator
          _literal
          _terminated_statement
          _primary_expression
          _simple_variable_name
          _multiline_variable_name
          _special_variable_name
          _c_word
          _statement_not_subshell
          _redirect]
 :supertypes [_statement _expression _primary_expression]
 :rules
 {program (:choice _statements :blank)
  _statements (:prec 1
               (:seq
                (:repeat (:seq _statement _terminator))
                _statement
                (:choice _terminator :blank)))
  _terminated_statement (:repeat1 (:seq _statement _terminator))
  _statement (:choice _statement_not_subshell subshell)
  _statement_not_subshell (:choice
                           redirected_statement
                           variable_assignment
                           variable_assignments
                           command
                           declaration_command
                           unset_command
                           test_command
                           negated_command
                           for_statement
                           c_style_for_statement
                           while_statement
                           if_statement
                           case_statement
                           pipeline
                           list
                           compound_statement
                           function_definition)
  _statement_not_pipeline (:prec 1
                           (:choice
                            redirected_statement
                            variable_assignment
                            variable_assignments
                            command
                            declaration_command
                            unset_command
                            test_command
                            negated_command
                            for_statement
                            c_style_for_statement
                            while_statement
                            if_statement
                            case_statement
                            list
                            compound_statement
                            function_definition
                            subshell))
  redirected_statement (:prec-dynamic -1
                        (:prec-right -1
                         (:choice
                          (:seq
                           (:field :body _statement)
                           (:field :redirect
                            (:choice (:repeat1 (:choice file_redirect heredoc_redirect)))))
                          (:seq
                           (:field :body (:choice if_statement while_statement))
                           herestring_redirect)
                          (:field :redirect (:repeat1 _redirect))
                          herestring_redirect)))
  for_statement (:seq
                 (:choice "for" "select")
                 (:field :variable _simple_variable_name)
                 (:choice (:seq "in" (:field :value (:repeat1 _literal))) :blank)
                 _terminator
                 (:field :body do_group))
  c_style_for_statement (:seq
                         "for"
                         "(("
                         (:choice _for_body)
                         "))"
                         (:choice ";" :blank)
                         (:field :body (:choice do_group compound_statement)))
  _for_body (:seq
             (:field :initializer
              (:choice (:seq _c_expression (:repeat (:seq "," _c_expression))) :blank))
             _c_terminator
             (:field :condition
              (:choice (:seq _c_expression (:repeat (:seq "," _c_expression))) :blank))
             _c_terminator
             (:field :update
              (:choice (:seq _c_expression (:repeat (:seq "," _c_expression))) :blank)))
  _c_expression (:choice
                 _c_expression_not_assignment
                 (:alias _c_variable_assignment variable_assignment))
  _c_expression_not_assignment (:choice
                                _c_word
                                simple_expansion
                                expansion
                                number
                                string
                                (:alias _c_unary_expression unary_expression)
                                (:alias _c_binary_expression binary_expression)
                                (:alias _c_postfix_expression postfix_expression)
                                (:alias _c_parenthesized_expression parenthesized_expression)
                                command_substitution)
  _c_variable_assignment (:seq
                          (:field :name (:alias _c_word variable_name))
                          "="
                          (:field :value _c_expression))
  _c_unary_expression (:prec 17
                       (:seq (:field :operator (:choice "++" "--")) _c_expression_not_assignment))
  _c_binary_expression (:choice
                        (:prec-left 0
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator
                           (:choice "+=" "-=" "*=" "/=" "%=" "**=" "<<=" ">>=" "&=" "^=" "|="))
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 3
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator (:choice "||" "-o"))
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 4
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator (:choice "&&" "-a"))
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 5
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator "|")
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 6
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator "^")
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 7
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator "&")
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 8
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator (:choice "==" "!="))
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 9
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator (:choice "<" ">" "<=" ">="))
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 12
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator (:choice "<<" ">>"))
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 13
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator (:choice "+" "-"))
                          (:field :right _c_expression_not_assignment)))
                        (:prec-left 14
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator (:choice "*" "/" "%"))
                          (:field :right _c_expression_not_assignment)))
                        (:prec-right 15
                         (:seq
                          (:field :left _c_expression_not_assignment)
                          (:field :operator "**")
                          (:field :right _c_expression_not_assignment))))
  _c_postfix_expression (:prec 18
                         (:seq _c_expression_not_assignment (:field :operator (:choice "++" "--"))))
  _c_parenthesized_expression (:seq "(" (:seq _c_expression (:repeat (:seq "," _c_expression))) ")")
  _c_word (:alias (:pattern "[a-zA-Z_][a-zA-Z0-9_]*") word)
  while_statement (:seq
                   (:choice "while" "until")
                   (:field :condition _terminated_statement)
                   (:field :body do_group))
  do_group (:seq "do" (:choice _terminated_statement :blank) "done")
  if_statement (:seq
                "if"
                (:field :condition _terminated_statement)
                "then"
                (:choice _terminated_statement :blank)
                (:repeat elif_clause)
                (:choice else_clause :blank)
                "fi")
  elif_clause (:seq "elif" _terminated_statement "then" (:choice _terminated_statement :blank))
  else_clause (:seq "else" (:choice _terminated_statement :blank))
  case_statement (:seq
                  "case"
                  (:field :value _literal)
                  (:choice _terminator :blank)
                  "in"
                  (:choice _terminator :blank)
                  (:choice (:seq (:repeat case_item) (:alias last_case_item case_item)) :blank)
                  "esac")
  case_item (:seq
             (:choice
              (:seq
               (:choice "(" :blank)
               (:field :value (:choice _literal _extglob_blob))
               (:repeat (:seq "|" (:field :value (:choice _literal _extglob_blob))))
               ")"))
             (:choice _statements :blank)
             (:prec 1
              (:choice (:field :termination ";;") (:field :fallthrough (:choice ";&" ";;&")))))
  last_case_item (:seq
                  (:choice "(" :blank)
                  (:field :value (:choice _literal _extglob_blob))
                  (:repeat (:seq "|" (:field :value (:choice _literal _extglob_blob))))
                  ")"
                  (:choice _statements :blank)
                  (:choice (:prec 1 ";;") :blank))
  function_definition (:prec-right 0
                       (:seq
                        (:choice
                         (:seq "function" (:field :name word) (:choice (:seq "(" ")") :blank))
                         (:seq (:field :name word) "(" ")"))
                        (:field :body
                         (:choice compound_statement subshell test_command if_statement))
                        (:field :redirect (:choice _redirect :blank))))
  compound_statement (:choice
                      (:seq "{" (:choice _terminated_statement :blank) (:token (:prec -1 "}")))
                      (:seq
                       "(("
                       (:repeat (:seq _arithmetic_expression ","))
                       _arithmetic_expression
                       "))"))
  subshell (:seq "(" _statements ")")
  pipeline (:prec-right 0
            (:seq
             _statement_not_pipeline
             (:repeat1 (:seq (:choice "|" "|&") _statement_not_pipeline))))
  list (:prec-left -1 (:seq _statement (:choice "&&" "||") _statement))
  negated_command (:seq
                   "!"
                   (:choice (:prec 2 command) (:prec 1 variable_assignment) test_command subshell))
  test_command (:seq
                (:choice
                 (:seq "[" (:choice (:choice _expression redirected_statement) :blank) "]")
                 (:seq
                  "[["
                  (:choice _expression (:alias _test_command_binary_expression binary_expression))
                  "]]")))
  _test_command_binary_expression (:prec 1
                                   (:seq
                                    (:field :left _expression)
                                    (:field :operator "=")
                                    (:field :right (:alias _regex_no_space regex))))
  declaration_command (:prec-left 0
                       (:seq
                        (:choice "declare" "typeset" "export" "readonly" "local")
                        (:repeat (:choice _literal _simple_variable_name variable_assignment))))
  unset_command (:prec-left 0
                 (:seq
                  (:choice "unset" "unsetenv")
                  (:repeat (:choice _literal _simple_variable_name))))
  command (:prec-left 0
           (:seq
            (:repeat (:choice variable_assignment (:field :redirect _redirect)))
            (:field :name command_name)
            (:choice
             (:repeat
              (:choice
               (:field :argument _literal)
               (:field :argument (:alias _bare_dollar "$"))
               (:field :argument (:seq (:choice "=~" "==") (:choice _literal regex)))
               (:field :redirect herestring_redirect)))
             subshell)))
  command_name _literal
  variable_assignment (:seq
                       (:field :name (:choice variable_name subscript))
                       (:choice "=" "+=")
                       (:field :value
                        (:choice _literal array _empty_value (:alias _comment_word word))))
  variable_assignments (:seq variable_assignment (:repeat1 variable_assignment))
  subscript (:seq
             (:field :name variable_name)
             "["
             (:field :index
              (:choice _literal binary_expression unary_expression compound_statement subshell))
             (:choice _concat :blank)
             "]"
             (:choice _concat :blank))
  file_redirect (:prec-left 0
                 (:seq
                  (:field :descriptor (:choice file_descriptor :blank))
                  (:choice
                   (:seq
                    (:choice "<" ">" ">>" "&>" "&>>" "<&" ">&" ">|")
                    (:field :destination (:repeat1 _literal)))
                   (:seq (:choice "<&-" ">&-") (:choice (:field :destination _literal) :blank)))))
  heredoc_redirect (:seq
                    (:field :descriptor (:choice file_descriptor :blank))
                    (:choice "<<" "<<-")
                    heredoc_start
                    (:choice
                     (:choice
                      (:alias _heredoc_pipeline pipeline)
                      (:seq
                       (:field :redirect (:repeat1 _redirect))
                       (:choice _heredoc_expression :blank))
                      _heredoc_expression
                      _heredoc_command)
                     :blank)
                    (:pattern "\\n")
                    (:choice _heredoc_body _simple_heredoc_body))
  _heredoc_pipeline (:seq (:choice "|" "|&") _statement)
  _heredoc_expression (:seq (:field :operator (:choice "||" "&&")) (:field :right _statement))
  _heredoc_command (:repeat1 (:field :argument _literal))
  _heredoc_body (:seq heredoc_body heredoc_end)
  heredoc_body (:seq
                _heredoc_body_beginning
                (:repeat (:choice expansion simple_expansion command_substitution heredoc_content)))
  _simple_heredoc_body (:seq (:alias simple_heredoc_body heredoc_body) heredoc_end)
  herestring_redirect (:prec-left 0
                       (:seq (:field :descriptor (:choice file_descriptor :blank)) "<<<" _literal))
  _redirect (:choice file_redirect herestring_redirect)
  _expression (:choice
               _literal
               unary_expression
               ternary_expression
               binary_expression
               postfix_expression
               parenthesized_expression)
  binary_expression (:choice
                     (:choice
                      (:prec-left 0
                       (:seq
                        (:field :left _expression)
                        (:field :operator
                         (:choice "+=" "-=" "*=" "/=" "%=" "**=" "<<=" ">>=" "&=" "^=" "|="))
                        (:field :right _expression)))
                      (:prec-left 1
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:choice "=" "=~"))
                        (:field :right _expression)))
                      (:prec-left 3
                       (:seq
                        (:field :left _expression)
                        (:field :operator "||")
                        (:field :right _expression)))
                      (:prec-left 4
                       (:seq
                        (:field :left _expression)
                        (:field :operator "&&")
                        (:field :right _expression)))
                      (:prec-left 5
                       (:seq
                        (:field :left _expression)
                        (:field :operator "|")
                        (:field :right _expression)))
                      (:prec-left 6
                       (:seq
                        (:field :left _expression)
                        (:field :operator "^")
                        (:field :right _expression)))
                      (:prec-left 7
                       (:seq
                        (:field :left _expression)
                        (:field :operator "&")
                        (:field :right _expression)))
                      (:prec-left 8
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:choice "==" "!="))
                        (:field :right _expression)))
                      (:prec-left 9
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:choice "<" ">" "<=" ">="))
                        (:field :right _expression)))
                      (:prec-left 10
                       (:seq
                        (:field :left _expression)
                        (:field :operator test_operator)
                        (:field :right _expression)))
                      (:prec-left 12
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:choice "<<" ">>"))
                        (:field :right _expression)))
                      (:prec-left 13
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:choice "+" "-"))
                        (:field :right _expression)))
                      (:prec-left 14
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:choice "*" "/" "%"))
                        (:field :right _expression)))
                      (:prec-right 15
                       (:seq
                        (:field :left _expression)
                        (:field :operator "**")
                        (:field :right _expression))))
                     (:prec 1
                      (:seq
                       (:field :left _expression)
                       (:field :operator "=~")
                       (:field :right (:alias _regex_no_space regex))))
                     (:prec 8
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "==" "!="))
                       (:field :right _extglob_blob))))
  ternary_expression (:prec-left 2
                      (:seq
                       (:field :condition _expression)
                       "?"
                       (:field :consequence _expression)
                       ":"
                       (:field :alternative _expression)))
  unary_expression (:choice
                    (:prec 17
                     (:seq
                      (:field :operator (:choice (:token (:prec 1 "++")) (:token (:prec 1 "--"))))
                      _expression))
                    (:prec 11
                     (:seq
                      (:field :operator
                       (:choice
                        (:token (:prec 1 "-"))
                        (:token (:prec 1 "+"))
                        (:token (:prec 1 "~"))))
                      _expression))
                    (:prec-right 11 (:seq (:field :operator "!") _expression))
                    (:prec-right 10 (:seq (:field :operator test_operator) _expression)))
  postfix_expression (:prec 18 (:seq _expression (:field :operator (:choice "++" "--"))))
  parenthesized_expression (:seq "(" _expression ")")
  _literal (:choice
            concatenation
            _primary_expression
            (:alias (:prec -2 (:repeat1 _special_character)) word))
  _primary_expression (:choice
                       word
                       (:alias test_operator word)
                       string
                       raw_string
                       translated_string
                       ansi_c_string
                       number
                       expansion
                       simple_expansion
                       command_substitution
                       process_substitution
                       arithmetic_expansion
                       brace_expression)
  arithmetic_expansion (:choice
                        (:seq
                         "$(("
                         (:seq _arithmetic_expression (:repeat (:seq "," _arithmetic_expression)))
                         "))")
                        (:seq "$[" _arithmetic_expression "]"))
  brace_expression (:seq
                    (:alias _brace_start "{")
                    (:alias (:token-immediate (:pattern "\\d+")) number)
                    (:token-immediate "..")
                    (:alias (:token-immediate (:pattern "\\d+")) number)
                    (:token-immediate "}"))
  _arithmetic_expression (:prec 1
                          (:choice
                           _arithmetic_literal
                           (:alias _arithmetic_unary_expression unary_expression)
                           (:alias _arithmetic_ternary_expression ternary_expression)
                           (:alias _arithmetic_binary_expression binary_expression)
                           (:alias _arithmetic_postfix_expression postfix_expression)
                           (:alias _arithmetic_parenthesized_expression parenthesized_expression)
                           command_substitution))
  _arithmetic_literal (:prec 1
                       (:choice
                        number
                        subscript
                        simple_expansion
                        expansion
                        _simple_variable_name
                        variable_name
                        string
                        raw_string))
  _arithmetic_binary_expression (:choice
                                 (:prec-left 0
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator
                                    (:choice
                                     "+="
                                     "-="
                                     "*="
                                     "/="
                                     "%="
                                     "**="
                                     "<<="
                                     ">>="
                                     "&="
                                     "^="
                                     "|="))
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 1
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator (:choice "=" "=~"))
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 3
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator "||")
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 4
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator "&&")
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 5
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator "|")
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 6
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator "^")
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 7
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator "&")
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 8
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator (:choice "==" "!="))
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 9
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator (:choice "<" ">" "<=" ">="))
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 12
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator (:choice "<<" ">>"))
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 13
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator (:choice "+" "-"))
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 14
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator (:choice "*" "/" "%"))
                                   (:field :right _arithmetic_expression)))
                                 (:prec-left 15
                                  (:seq
                                   (:field :left _arithmetic_expression)
                                   (:field :operator "**")
                                   (:field :right _arithmetic_expression))))
  _arithmetic_ternary_expression (:prec-left 2
                                  (:seq
                                   (:field :condition _arithmetic_expression)
                                   "?"
                                   (:field :consequence _arithmetic_expression)
                                   ":"
                                   (:field :alternative _arithmetic_expression)))
  _arithmetic_unary_expression (:choice
                                (:prec 17
                                 (:seq
                                  (:field :operator
                                   (:choice (:token (:prec 1 "++")) (:token (:prec 1 "--"))))
                                  _arithmetic_expression))
                                (:prec 11
                                 (:seq
                                  (:field :operator
                                   (:choice
                                    (:token (:prec 1 "-"))
                                    (:token (:prec 1 "+"))
                                    (:token (:prec 1 "~"))))
                                  _arithmetic_expression))
                                (:prec-right 11
                                 (:seq (:field :operator "!") _arithmetic_expression)))
  _arithmetic_postfix_expression (:prec 18
                                  (:seq
                                   _arithmetic_expression
                                   (:field :operator (:choice "++" "--"))))
  _arithmetic_parenthesized_expression (:seq "(" _arithmetic_expression ")")
  concatenation (:prec -1
                 (:seq
                  (:choice _primary_expression (:alias _special_character word))
                  (:repeat1
                   (:seq
                    (:choice _concat (:alias (:pattern "`\\s*`") "``"))
                    (:choice
                     _primary_expression
                     (:alias _special_character word)
                     (:alias _comment_word word)
                     (:alias _bare_dollar "$"))))
                  (:choice (:seq _concat "$") :blank)))
  _special_character (:token (:prec -1 (:choice "{" "}" "[" "]")))
  string (:seq
          "\""
          (:repeat
           (:seq
            (:choice
             (:seq (:choice "$" :blank) string_content)
             expansion
             simple_expansion
             command_substitution
             arithmetic_expansion)
            (:choice _concat :blank)))
          (:choice "$" :blank)
          "\"")
  string_content (:token (:prec -1 (:pattern "([^\"`$\\\\\\r\\n]|\\\\(.|\\r?\\n))+")))
  translated_string (:seq "$" string)
  array (:seq "(" (:repeat _literal) ")")
  raw_string (:pattern "'[^']*'")
  ansi_c_string (:pattern "\\$'([^']|\\\\')*'")
  number (:choice
          (:pattern "-?(0x)?[0-9]+(#[0-9A-Za-z@_]+)?")
          (:seq (:pattern "-?(0x)?[0-9]+#") (:choice expansion command_substitution)))
  simple_expansion (:seq
                    "$"
                    (:choice
                     _simple_variable_name
                     _multiline_variable_name
                     _special_variable_name
                     variable_name
                     (:alias "!" special_variable_name)
                     (:alias "#" special_variable_name)))
  string_expansion (:seq "$" string)
  expansion (:seq "${" (:choice _expansion_body :blank) "}")
  _expansion_body (:choice
                   (:repeat1
                    (:field :operator
                     (:choice
                      (:alias _external_expansion_sym_hash "#")
                      (:alias _external_expansion_sym_bang "!")
                      (:alias _external_expansion_sym_equal "="))))
                   (:seq
                    (:choice (:field :operator (:token-immediate "!")) :blank)
                    (:choice variable_name _simple_variable_name _special_variable_name subscript)
                    (:choice
                     _expansion_expression
                     _expansion_regex
                     _expansion_regex_replacement
                     _expansion_regex_removal
                     _expansion_max_length
                     _expansion_operator))
                   (:seq
                    (:field :operator (:token-immediate "!"))
                    (:choice _simple_variable_name variable_name)
                    (:choice
                     (:field :operator (:choice (:token-immediate "@") (:token-immediate "*")))
                     :blank))
                   (:seq
                    (:choice
                     (:field :operator
                      (:choice (:token-immediate "#") (:token-immediate "!") (:token-immediate "=")))
                     :blank)
                    (:choice
                     subscript
                     _simple_variable_name
                     _special_variable_name
                     command_substitution)
                    (:repeat
                     (:field :operator
                      (:choice
                       (:alias _external_expansion_sym_hash "#")
                       (:alias _external_expansion_sym_bang "!")
                       (:alias _external_expansion_sym_equal "="))))))
  _expansion_expression (:prec 1
                         (:seq
                          (:field :operator
                           (:choice
                            (:token-immediate "=")
                            (:token-immediate ":=")
                            (:token-immediate "-")
                            (:token-immediate ":-")
                            (:token-immediate "+")
                            (:token-immediate ":+")
                            (:token-immediate "?")
                            (:token-immediate ":?")))
                          (:choice
                           (:seq
                            (:choice
                             (:alias _concatenation_in_expansion concatenation)
                             command_substitution
                             word
                             expansion
                             simple_expansion
                             array
                             string
                             raw_string
                             ansi_c_string
                             (:alias _expansion_word word)))
                           :blank)))
  _expansion_regex (:seq
                    (:field :operator (:choice "#" (:alias _immediate_double_hash "##") "%" "%%"))
                    (:repeat
                     (:choice
                      regex
                      (:alias ")" regex)
                      string
                      raw_string
                      (:alias (:pattern "\\s+") regex))))
  _expansion_regex_replacement (:seq
                                (:field :operator (:choice "/" "//" "/#" "/%"))
                                (:choice
                                 (:choice
                                  (:alias _regex_no_slash regex)
                                  string
                                  command_substitution
                                  (:seq string (:alias _regex_no_slash regex)))
                                 :blank)
                                (:choice
                                 (:seq
                                  (:field :operator "/")
                                  (:choice
                                   (:seq
                                    (:choice
                                     _primary_expression
                                     (:alias (:prec -2 (:repeat1 _special_character)) word)
                                     (:seq command_substitution (:alias _expansion_word word))
                                     (:alias _expansion_word word)
                                     (:alias _concatenation_in_expansion concatenation)
                                     array)
                                    (:field :operator (:choice "/" :blank)))
                                   :blank))
                                 :blank))
  _expansion_regex_removal (:seq
                            (:field :operator (:choice "," ",," "^" "^^"))
                            (:choice regex :blank))
  _expansion_max_length (:seq
                         (:field :operator ":")
                         (:choice
                          (:choice
                           _simple_variable_name
                           number
                           arithmetic_expansion
                           expansion
                           parenthesized_expression
                           command_substitution
                           (:alias _expansion_max_length_binary_expression binary_expression)
                           (:pattern "\\n"))
                          :blank)
                         (:choice
                          (:seq
                           (:field :operator ":")
                           (:choice simple_expansion :blank)
                           (:choice
                            (:choice
                             _simple_variable_name
                             number
                             arithmetic_expansion
                             expansion
                             parenthesized_expression
                             command_substitution
                             (:alias _expansion_max_length_binary_expression binary_expression)
                             (:pattern "\\n"))
                            :blank))
                          :blank))
  _expansion_max_length_expression (:choice
                                    _simple_variable_name
                                    number
                                    expansion
                                    (:alias
                                     _expansion_max_length_binary_expression
                                     binary_expression))
  _expansion_max_length_binary_expression (:choice
                                           (:prec-left 13
                                            (:seq
                                             _expansion_max_length_expression
                                             (:field :operator (:choice "+" "-"))
                                             _expansion_max_length_expression))
                                           (:prec-left 14
                                            (:seq
                                             _expansion_max_length_expression
                                             (:field :operator (:choice "*" "/" "%"))
                                             _expansion_max_length_expression)))
  _expansion_operator (:seq
                       (:field :operator (:token-immediate "@"))
                       (:field :operator
                        (:choice
                         (:token-immediate "U")
                         (:token-immediate "u")
                         (:token-immediate "L")
                         (:token-immediate "Q")
                         (:token-immediate "E")
                         (:token-immediate "P")
                         (:token-immediate "A")
                         (:token-immediate "K")
                         (:token-immediate "a")
                         (:token-immediate "k"))))
  _concatenation_in_expansion (:prec -2
                               (:seq
                                (:choice
                                 word
                                 variable_name
                                 simple_expansion
                                 expansion
                                 string
                                 raw_string
                                 ansi_c_string
                                 command_substitution
                                 (:alias _expansion_word word)
                                 array
                                 process_substitution)
                                (:repeat1
                                 (:seq
                                  (:choice _concat (:alias (:pattern "`\\s*`") "``"))
                                  (:choice
                                   word
                                   variable_name
                                   simple_expansion
                                   expansion
                                   string
                                   raw_string
                                   ansi_c_string
                                   command_substitution
                                   (:alias _expansion_word word)
                                   array
                                   process_substitution)))))
  command_substitution (:choice
                        (:seq "$(" _statements ")")
                        (:seq "$(" (:field :redirect file_redirect) ")")
                        (:prec 1 (:seq "`" _statements "`"))
                        (:seq "$`" _statements "`"))
  process_substitution (:seq (:choice "<(" ">(") _statements ")")
  _extglob_blob (:choice
                 extglob_pattern
                 (:seq
                  extglob_pattern
                  (:choice string expansion command_substitution)
                  (:choice extglob_pattern :blank)))
  comment (:token (:prec -10 (:pattern "#.*")))
  _comment_word (:token
                 (:prec -8
                  (:seq
                   (:choice
                    (:pattern "[^'\"<>{}\\[\\]()`$|&;\\\\\\s]")
                    (:seq "\\" (:pattern "[^\\s]")))
                   (:repeat
                    (:choice
                     (:pattern "[^'\"<>{}\\[\\]()`$|&;\\\\\\s]")
                     (:seq "\\" (:pattern "[^\\s]"))
                     "\\ ")))))
  _simple_variable_name (:alias (:pattern "\\w+") variable_name)
  _multiline_variable_name (:alias
                            (:token (:prec -1 (:pattern "(\\w|\\\\\\r?\\n)+")))
                            variable_name)
  _special_variable_name (:alias (:choice "*" "@" "?" "!" "#" "-" "$" "_") special_variable_name)
  word (:token
        (:seq
         (:choice (:pattern "[^#'\"<>{}\\[\\]()`$|&;\\\\\\s]") (:seq "\\" (:pattern "[^\\s]")))
         (:repeat
          (:choice
           (:pattern "[^'\"<>{}\\[\\]()`$|&;\\\\\\s]")
           (:seq "\\" (:pattern "[^\\s]"))
           "\\ "))))
  _c_terminator (:choice ";" (:pattern "\\n") "&")
  _terminator (:choice ";" ";;" (:pattern "\\n") "&")}}
