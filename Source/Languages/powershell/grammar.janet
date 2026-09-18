# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "powershell"
 :extras [comment
          (:pattern "\\s")
          (:pattern "`\\n")
          (:pattern "`\\r\\n")
          (:pattern "[\\uFEFF\\u2060\\u200B\\u00A0]")]
 :conflicts [[_literal member_name]
             [class_property_definition attribute]
             [class_method_definition attribute]
             [expandable_string_literal]
             [path_command_name _value]]
 :precedences []
 :externals [_statement_terminator]
 :inline []
 :supertypes []
 :rules
 {program (:seq (:choice param_block :blank) statement_list)
  comment (:token
           (:choice
            (:pattern "#[^\\r\\n]*")
            (:seq
             "<#"
             (:repeat
              (:choice (:pattern "[^#`]+") (:pattern "#+[^>#]") (:pattern "`.{1}|`\\r?\\n")))
             (:pattern "#+>"))))
  _literal (:choice integer_literal string_literal real_literal)
  integer_literal (:choice decimal_integer_literal hexadecimal_integer_literal)
  decimal_integer_literal (:token
                           (:seq
                            (:pattern "[0-9]+")
                            (:choice (:choice "l" "d") :blank)
                            (:choice (:choice "kb" "mb" "gb" "tb" "pb") :blank)))
  hexadecimal_integer_literal (:token
                               (:seq
                                "0x"
                                (:pattern "[0-9a-fA-F]+")
                                (:choice "l" :blank)
                                (:choice (:choice "kb" "mb" "gb" "tb" "pb") :blank)))
  real_literal (:token
                (:choice
                 (:seq
                  (:pattern "[0-9]+\\.[0-9]+")
                  (:choice
                   (:token (:seq "e" (:choice (:choice "+" "-") :blank) (:pattern "[0-9]+")))
                   :blank)
                  (:choice (:choice "kb" "mb" "gb" "tb" "pb") :blank))
                 (:seq
                  (:pattern "\\.[0-9]+")
                  (:choice
                   (:token (:seq "e" (:choice (:choice "+" "-") :blank) (:pattern "[0-9]+")))
                   :blank)
                  (:choice (:choice "kb" "mb" "gb" "tb" "pb") :blank))
                 (:seq
                  (:pattern "[0-9]+")
                  (:token (:seq "e" (:choice (:choice "+" "-") :blank) (:pattern "[0-9]+")))
                  (:choice (:choice "kb" "mb" "gb" "tb" "pb") :blank))))
  string_literal (:choice
                  expandable_string_literal
                  verbatim_string_characters
                  expandable_here_string_literal
                  verbatim_here_string_characters)
  expandable_string_literal (:seq
                             (:pattern "\\\"(\\s*\\#*)*")
                             (:repeat
                              (:choice
                               (:token-immediate (:pattern "[^\\$\\\"`]+"))
                               variable
                               sub_expression
                               (:token-immediate (:pattern "\\$(`.{1}|`\\r?\\n|[\\s\\\\])"))
                               (:token-immediate (:pattern "`.{1}|`\\r?\\n"))
                               (:token-immediate "\"\"")
                               (:token-immediate "$")))
                             (:repeat (:token-immediate "$"))
                             (:token-immediate (:pattern "(\\s*\\#*)*\\\"")))
  expandable_here_string_literal (:seq
                                  (:pattern "@\\\" *\\r?\\n")
                                  (:repeat
                                   (:choice
                                    (:token-immediate (:pattern "[^\\$\\r\\n`]+"))
                                    variable
                                    sub_expression
                                    (:token-immediate (:pattern "(\\r?\\n)+[^\\\"\\r\\n]"))
                                    (:token-immediate (:pattern "(\\r?\\n)+\\\"[^@]"))
                                    (:token-immediate "$")
                                    (:token-immediate (:pattern "`.{1}|`\\r?\\n"))))
                                  (:token-immediate (:pattern "(\\r?\\n)+\\\"@")))
  verbatim_string_characters (:token (:seq "'" (:repeat (:choice (:pattern "[^']+") "''")) "'"))
  verbatim_here_string_characters (:token
                                   (:seq
                                    (:pattern "@\\'\\s*\\r?\\n")
                                    (:repeat
                                     (:choice
                                      (:pattern "[^\\r\\n]")
                                      (:pattern "(\\r?\\n)+[^\\'\\r\\n]")
                                      (:pattern "\\r?\\n\\'[^@]")))
                                    (:pattern "(\\r?\\n)+\\'@")))
  simple_name (:pattern "[a-zA-Z_][a-zA-Z0-9_]*")
  type_identifier (:pattern "[a-zA-Z0-9_]+")
  type_name (:choice type_identifier (:seq type_name "." type_identifier))
  array_type_name (:seq type_name "[")
  generic_type_name (:seq type_name "[")
  assignement_operator (:choice "=" "!=" "+=" "*=" "/=" "%=" "-=")
  file_redirection_operator (:choice
                             ">"
                             ">>"
                             "2>"
                             "2>>"
                             "3>"
                             "3>>"
                             "4>"
                             "4>>"
                             "5>"
                             "5>>"
                             "6>"
                             "6>>"
                             "*>"
                             "*>>"
                             "<")
  merging_redirection_operator (:choice
                                "*>&1"
                                "2>&1"
                                "3>&1"
                                "4>&1"
                                "5>&1"
                                "6>&1"
                                "*>&2"
                                "1>&2"
                                "3>&2"
                                "4>&2"
                                "5>&2"
                                "6>&2")
  comparison_operator (:choice
                       (:alias (:prec 1 (:pattern "[--][aA][sS]")) "-as")
                       (:alias
                        (:prec 1 (:pattern "[--][cC][cC][oO][nN][tT][aA][iI][nN][sS]"))
                        "-ccontains")
                       (:alias (:prec 1 (:pattern "[--][cC][eE][qQ]")) "-ceq")
                       (:alias (:prec 1 (:pattern "[--][cC][gG][eE]")) "-cge")
                       (:alias (:prec 1 (:pattern "[--][cC][gG][tT]")) "-cgt")
                       (:alias (:prec 1 (:pattern "[--][cC][lL][eE]")) "-cle")
                       (:alias (:prec 1 (:pattern "[--][cC][lL][iI][kK][eE]")) "-clike")
                       (:alias (:prec 1 (:pattern "[--][cC][lL][tT]")) "-clt")
                       (:alias (:prec 1 (:pattern "[--][cC][mM][aA][tT][cC][hH]")) "-cmatch")
                       (:alias (:prec 1 (:pattern "[--][cC][nN][eE]")) "-cne")
                       (:alias
                        (:prec 1 (:pattern "[--][cC][nN][oO][tT][cC][oO][nN][tT][aA][iI][nN][sS]"))
                        "-cnotcontains")
                       (:alias
                        (:prec 1 (:pattern "[--][cC][nN][oO][tT][lL][iI][kK][eE]"))
                        "-cnotlike")
                       (:alias
                        (:prec 1 (:pattern "[--][cC][nN][oO][tT][mM][aA][tT][cC][hH]"))
                        "-cnotmatch")
                       (:alias
                        (:prec 1 (:pattern "[--][cC][oO][nN][tT][aA][iI][nN][sS]"))
                        "-contains")
                       (:alias
                        (:prec 1 (:pattern "[--][cC][rR][eE][pP][lL][aA][cC][eE]"))
                        "-creplace")
                       (:alias (:prec 1 (:pattern "[--][cC][sS][pP][lL][iI][tT]")) "-csplit")
                       (:alias (:prec 1 (:pattern "[--][eE][qQ]")) "-eq")
                       (:alias (:prec 1 (:pattern "[--][gG][eE]")) "-ge")
                       (:alias (:prec 1 (:pattern "[--][gG][tT]")) "-gt")
                       (:alias
                        (:prec 1 (:pattern "[--][iI][cC][oO][nN][tT][aA][iI][nN][sS]"))
                        "-icontains")
                       (:alias (:prec 1 (:pattern "[--][iI][eE][qQ]")) "-ieq")
                       (:alias (:prec 1 (:pattern "[--][iI][gG][eE]")) "-ige")
                       (:alias (:prec 1 (:pattern "[--][iI][gG][tT]")) "-igt")
                       (:alias (:prec 1 (:pattern "[--][iI][lL][eE]")) "-ile")
                       (:alias (:prec 1 (:pattern "[--][iI][lL][iI][kK][eE]")) "-ilike")
                       (:alias (:prec 1 (:pattern "[--][iI][lL][tT]")) "-ilt")
                       (:alias (:prec 1 (:pattern "[--][iI][mM][aA][tT][cC][hH]")) "-imatch")
                       (:alias (:prec 1 (:pattern "[--][iI][nN]")) "-in")
                       (:alias (:prec 1 (:pattern "[--][iI][nN][eE]")) "-ine")
                       (:alias
                        (:prec 1 (:pattern "[--][iI][nN][oO][tT][cC][oO][nN][tT][aA][iI][nN][sS]"))
                        "-inotcontains")
                       (:alias
                        (:prec 1 (:pattern "[--][iI][nN][oO][tT][lL][iI][kK][eE]"))
                        "-inotlike")
                       (:alias
                        (:prec 1 (:pattern "[--][iI][nN][oO][tT][mM][aA][tT][cC][hH]"))
                        "-inotmatch")
                       (:alias
                        (:prec 1 (:pattern "[--][iI][rR][eE][pP][lL][aA][cC][eE]"))
                        "-ireplace")
                       (:alias (:prec 1 (:pattern "[--][iI][sS]")) "-is")
                       (:alias (:prec 1 (:pattern "[--][iI][sS][nN][oO][tT]")) "-isnot")
                       (:alias (:prec 1 (:pattern "[--][iI][sS][pP][lL][iI][tT]")) "-isplit")
                       (:alias (:prec 1 (:pattern "[--][jJ][oO][iI][nN]")) "-join")
                       (:alias (:prec 1 (:pattern "[--][lL][eE]")) "-le")
                       (:alias (:prec 1 (:pattern "[--][lL][iI][kK][eE]")) "-like")
                       (:alias (:prec 1 (:pattern "[--][lL][tT]")) "-lt")
                       (:alias (:prec 1 (:pattern "[--][mM][aA][tT][cC][hH]")) "-match")
                       (:alias (:prec 1 (:pattern "[--][nN][eE]")) "-ne")
                       (:alias
                        (:prec 1 (:pattern "[--][nN][oO][tT][cC][oO][nN][tT][aA][iI][nN][sS]"))
                        "-notcontains")
                       (:alias (:prec 1 (:pattern "[--][nN][oO][tT][iI][nN]")) "-notin")
                       (:alias (:prec 1 (:pattern "[--][nN][oO][tT][lL][iI][kK][eE]")) "-notlike")
                       (:alias
                        (:prec 1 (:pattern "[--][nN][oO][tT][mM][aA][tT][cC][hH]"))
                        "-notmatch")
                       (:alias (:prec 1 (:pattern "[--][rR][eE][pP][lL][aA][cC][eE]")) "-replace")
                       (:alias (:prec 1 (:pattern "[--][sS][hH][lL]")) "-shl")
                       (:alias (:prec 1 (:pattern "[--][sS][hH][rR]")) "-shr")
                       (:alias (:prec 1 (:pattern "[--][sS][pP][lL][iI][tT]")) "-split"))
  format_operator (:alias (:prec 1 (:pattern "[--][fF]")) "-f")
  variable (:choice
            "$$"
            "$^"
            "$?"
            "$_"
            (:token
             (:seq
              "$"
              (:choice
               (:seq
                (:choice
                 (:alias (:prec 1 (:pattern "[gG][lL][oO][bB][aA][lL][::]")) "global:")
                 (:alias (:prec 1 (:pattern "[lL][oO][cC][aA][lL][::]")) "local:")
                 (:alias (:prec 1 (:pattern "[pP][rR][iI][vV][aA][tT][eE][::]")) "private:")
                 (:alias (:prec 1 (:pattern "[sS][cC][rR][iI][pP][tT][::]")) "script:")
                 (:alias (:prec 1 (:pattern "[uU][sS][iI][nN][gG][::]")) "using:")
                 (:alias (:prec 1 (:pattern "[wW][oO][rR][kK][fF][lL][oO][wW][::]")) "workflow:")
                 (:pattern "[a-zA-Z0-9_]+"))
                ":")
               :blank)
              (:pattern "[a-zA-Z0-9_]+|\\?")))
            (:token
             (:seq
              "@"
              (:choice
               (:seq
                (:choice
                 (:alias (:prec 1 (:pattern "[gG][lL][oO][bB][aA][lL][::]")) "global:")
                 (:alias (:prec 1 (:pattern "[lL][oO][cC][aA][lL][::]")) "local:")
                 (:alias (:prec 1 (:pattern "[pP][rR][iI][vV][aA][tT][eE][::]")) "private:")
                 (:alias (:prec 1 (:pattern "[sS][cC][rR][iI][pP][tT][::]")) "script:")
                 (:alias (:prec 1 (:pattern "[uU][sS][iI][nN][gG][::]")) "using:")
                 (:alias (:prec 1 (:pattern "[wW][oO][rR][kK][fF][lL][oO][wW][::]")) "workflow:")
                 (:pattern "[a-zA-Z0-9_]+"))
                ":")
               :blank)
              (:pattern "[a-zA-Z0-9_]+|\\?")))
            braced_variable)
  braced_variable (:pattern "\\$\\{[^}]+\\}")
  generic_token (:token
                 (:pattern "[^\\(\\)\\$\\\"\\'\\-\\{\\}@\\|\\[`\\&\\s][^\\&\\s\\(\\)\\}\\|;,]*"))
  _command_token (:token (:pattern "[^\\(\\)\\{\\}\\s;\\&]+"))
  command_parameter (:token (:choice (:pattern "-+[a-zA-Z_?\\-`]+") "--"))
  _verbatim_command_argument_chars (:repeat1
                                    (:choice
                                     (:pattern "\"[^\"]*\"")
                                     (:pattern "&[^&]*")
                                     (:pattern "[^\\|\\r\\n]+")))
  script_block (:choice
                (:field :script_block_body script_block_body)
                (:seq
                 (:seq param_block _statement_terminator (:repeat ";"))
                 (:field :script_block_body (:choice script_block_body :blank))))
  param_block (:seq
               (:choice attribute_list :blank)
               (:alias (:prec 1 (:pattern "[pP][aA][rR][aA][mM]")) "param")
               "("
               (:choice parameter_list :blank)
               ")")
  parameter_list (:seq script_parameter (:repeat (:seq "," script_parameter)))
  script_parameter (:seq
                    (:choice attribute_list :blank)
                    variable
                    (:choice script_parameter_default :blank))
  script_parameter_default (:seq "=" _expression)
  script_block_body (:choice
                     (:field :named_block_list named_block_list)
                     (:field :statement_list statement_list))
  named_block_list (:repeat1 named_block)
  named_block (:seq block_name statement_block)
  block_name (:choice
              (:alias
               (:prec 1 (:pattern "[dD][yY][nN][aA][mM][iI][cC][pP][aA][rR][aA][mM]"))
               "dynamicparam")
              (:alias (:prec 1 (:pattern "[bB][eE][gG][iI][nN]")) "begin")
              (:alias (:prec 1 (:pattern "[pP][rR][oO][cC][eE][sS][sS]")) "process")
              (:alias (:prec 1 (:pattern "[eE][nN][dD]")) "end"))
  statement_block (:seq "{" (:field :statement_list (:choice statement_list :blank)) "}")
  statement_list (:repeat1 _statement)
  _statement (:prec-right 0
              (:choice
               if_statement
               (:seq (:choice label :blank) _labeled_statement)
               function_statement
               class_statement
               enum_statement
               (:seq flow_control_statement _statement_terminator)
               trap_statement
               try_statement
               data_statement
               inlinescript_statement
               parallel_statement
               sequence_statement
               (:seq pipeline _statement_terminator)
               empty_statement))
  empty_statement (:prec 5 ";")
  if_statement (:prec-left 0
                (:seq
                 (:alias (:prec 1 (:pattern "[iI][fF]")) "if")
                 "("
                 (:field :condition pipeline)
                 ")"
                 statement_block
                 (:field :elseif_clauses (:choice elseif_clauses :blank))
                 (:field :else_clause (:choice else_clause :blank))))
  elseif_clauses (:prec-left 0 (:repeat1 elseif_clause))
  elseif_clause (:seq
                 (:alias (:prec 1 (:pattern "[eE][lL][sS][eE][iI][fF]")) "elseif")
                 "("
                 (:field :condition pipeline)
                 ")"
                 statement_block)
  else_clause (:seq (:alias (:prec 1 (:pattern "[eE][lL][sS][eE]")) "else") statement_block)
  _labeled_statement (:choice
                      switch_statement
                      foreach_statement
                      for_statement
                      while_statement
                      do_statement)
  switch_statement (:seq
                    (:alias (:prec 1 (:pattern "[sS][wW][iI][tT][cC][hH]")) "switch")
                    (:choice switch_parameters :blank)
                    switch_condition
                    switch_body)
  switch_parameters (:repeat1 switch_parameter)
  switch_parameter (:choice
                    (:alias (:prec 1 (:pattern "[--][rR][eE][gG][eE][xX]")) "-regex")
                    (:alias (:prec 1 (:pattern "[--][wW][iI][lL][dD][cC][aA][rR][dD]")) "-wildcard")
                    (:alias (:prec 1 (:pattern "[--][eE][xX][aA][cC][tT]")) "-exact")
                    (:alias
                     (:prec 1 (:pattern "[--][cC][aA][sS][eE][sS][eE][nN][sS][iI][tT][iI][vV][eE]"))
                     "-casesensitive")
                    (:alias (:prec 1 (:pattern "[--][pP][aA][rR][aA][lL][lL][eE][lL]")) "-parallel"))
  switch_condition (:choice
                    (:seq "(" pipeline ")")
                    (:seq
                     (:alias (:prec 1 (:pattern "[--][fF][iI][lL][eE]")) "-file")
                     switch_filename))
  switch_filename (:choice _command_token _primary_expression)
  switch_body (:seq "{" (:choice switch_clauses :blank) "}")
  switch_clauses (:repeat1 switch_clause)
  switch_clause (:seq switch_clause_condition statement_block _statement_terminator (:repeat ";"))
  switch_clause_condition (:choice _command_token _primary_expression)
  foreach_statement (:seq
                     (:alias (:prec 1 (:pattern "[fF][oO][rR][eE][aA][cC][hH]")) "foreach")
                     (:choice foreach_parameter :blank)
                     "("
                     variable
                     (:alias (:prec 1 (:pattern "[iI][nN]")) "in")
                     pipeline
                     ")"
                     statement_block)
  foreach_parameter (:choice
                     (:alias
                      (:prec 1 (:pattern "[--][pP][aA][rR][aA][lL][lL][eE][lL]"))
                      "-parallel"))
  for_statement (:seq
                 (:alias (:prec 1 (:pattern "[fF][oO][rR]")) "for")
                 "("
                 (:choice
                  (:seq
                   (:choice
                    (:seq (:field :for_initializer for_initializer) _statement_terminator)
                    :blank)
                   (:choice
                    (:seq
                     (:choice ";" (:token-immediate (:pattern "\\r?\\n")))
                     (:choice
                      (:seq (:field :for_condition for_condition) _statement_terminator)
                      :blank)
                     (:choice
                      (:seq
                       (:choice ";" (:token-immediate (:pattern "\\r?\\n")))
                       (:choice
                        (:seq (:field :for_iterator for_iterator) _statement_terminator)
                        :blank))
                      :blank))
                    :blank))
                  :blank)
                 ")"
                 statement_block)
  for_initializer pipeline
  for_condition pipeline
  for_iterator pipeline
  while_statement (:seq
                   (:alias (:prec 1 (:pattern "[wW][hH][iI][lL][eE]")) "while")
                   "("
                   (:field :condition while_condition)
                   ")"
                   statement_block)
  while_condition pipeline
  do_statement (:seq
                (:alias (:prec 1 (:pattern "[dD][oO]")) "do")
                statement_block
                (:choice
                 (:alias (:prec 1 (:pattern "[wW][hH][iI][lL][eE]")) "while")
                 (:alias (:prec 1 (:pattern "[uU][nN][tT][iI][lL]")) "until"))
                "("
                (:field :condition while_condition)
                ")")
  function_statement (:seq
                      (:choice
                       (:alias (:prec 1 (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]")) "function")
                       (:alias (:prec 1 (:pattern "[fF][iI][lL][tT][eE][rR]")) "filter")
                       (:alias (:prec 1 (:pattern "[wW][oO][rR][kK][fF][lL][oO][wW]")) "workflow"))
                      function_name
                      (:choice function_parameter_declaration :blank)
                      "{"
                      (:choice script_block :blank)
                      "}")
  function_name _command_token
  function_parameter_declaration (:seq "(" (:choice parameter_list :blank) ")")
  flow_control_statement (:choice
                          (:seq
                           (:alias (:prec 1 (:pattern "[bB][rR][eE][aA][kK]")) "break")
                           (:choice label_expression :blank))
                          (:seq
                           (:alias
                            (:prec 1 (:pattern "[cC][oO][nN][tT][iI][nN][uU][eE]"))
                            "continue")
                           (:choice label_expression :blank))
                          (:seq
                           (:alias (:prec 1 (:pattern "[tT][hH][rR][oO][wW]")) "throw")
                           (:choice pipeline :blank))
                          (:seq
                           (:alias (:prec 1 (:pattern "[rR][eE][tT][uU][rR][nN]")) "return")
                           (:choice pipeline :blank))
                          (:seq
                           (:alias (:prec 1 (:pattern "[eE][xX][iI][tT]")) "exit")
                           (:choice pipeline :blank)))
  label (:token (:seq ":" (:pattern "[a-zA-Z_][a-zA-Z0-9_]*")))
  label_expression (:choice label unary_expression)
  trap_statement (:seq
                  (:alias (:prec 1 (:pattern "[tT][rR][aA][pP]")) "trap")
                  (:choice type_literal :blank)
                  statement_block)
  try_statement (:seq
                 (:alias (:prec 1 (:pattern "[tT][rR][yY]")) "try")
                 statement_block
                 (:choice
                  (:seq catch_clauses (:choice finally_clause :blank))
                  (:choice finally_clause :blank)))
  catch_clauses (:repeat1 catch_clause)
  catch_clause (:seq
                (:alias (:prec 1 (:pattern "[cC][aA][tT][cC][hH]")) "catch")
                (:choice catch_type_list :blank)
                statement_block)
  catch_type_list (:seq type_literal (:repeat (:seq "," type_literal)))
  finally_clause (:seq
                  (:alias (:prec 1 (:pattern "[fF][iI][nN][aA][lL][lL][yY]")) "finally")
                  statement_block)
  data_statement (:seq
                  (:alias (:prec 1 (:pattern "[dD][aA][tT][aA]")) "data")
                  (:choice data_name :blank)
                  (:choice data_commands_allowed :blank)
                  statement_block)
  data_name simple_name
  data_commands_allowed (:seq
                         (:alias
                          (:prec 1
                           (:pattern "[--][sS][uU][pP][pP][oO][rR][tT][eE][dD][cC][oO][mM][mM][aA][nN][dD]"))
                          "-supportedcommand")
                         data_commands_list)
  data_commands_list (:seq data_command (:repeat (:seq "," data_command)))
  data_command command_name_expr
  inlinescript_statement (:seq
                          (:alias
                           (:prec 1 (:pattern "[iI][nN][lL][iI][nN][eE][sS][cC][rR][iI][pP][tT]"))
                           "inlinescript")
                          statement_block)
  parallel_statement (:seq
                      (:alias (:prec 1 (:pattern "[pP][aA][rR][aA][lL][lL][eE][lL]")) "parallel")
                      statement_block)
  sequence_statement (:seq
                      (:alias (:prec 1 (:pattern "[sS][eE][qQ][uU][eE][nN][cC][eE]")) "sequence")
                      statement_block)
  pipeline (:choice
            assignment_expression
            (:seq pipeline_chain (:repeat (:seq pipeline_chain_tail pipeline_chain))))
  pipeline_chain (:choice
                  (:seq _expression (:choice redirections :blank) (:choice _pipeline_tail :blank))
                  (:seq
                   command
                   (:choice verbatim_command_argument :blank)
                   (:choice _pipeline_tail :blank)))
  pipeline_chain_tail (:choice "&&" "||")
  left_assignment_expression _expression
  assignment_expression (:seq
                         left_assignment_expression
                         assignement_operator
                         (:field :value _statement))
  _pipeline_tail (:repeat1 (:seq "|" command))
  command (:choice
           (:seq
            (:field :command_name command_name)
            (:field :command_elements (:choice command_elements :blank)))
           (:seq
            command_invokation_operator
            (:field :command_name command_name_expr)
            (:field :command_elements (:choice command_elements :blank))))
  command_invokation_operator (:choice "." "&")
  _expandable_string_literal_immediate (:seq
                                        (:repeat
                                         (:choice
                                          (:pattern "[^\\$\"`]+")
                                          variable
                                          (:pattern "\\$`(.{1}|`\\r?\\n)")
                                          (:pattern "`.{1}|`\\r?\\n")
                                          "\"\""
                                          sub_expression))
                                        (:repeat "$")
                                        "\"")
  _string_literal_immediate (:seq (:pattern "[^']+") "'")
  command_name (:seq
                (:choice
                 (:pattern "[^\\{\\}\\(\\);,\\|\\&`\"'\\s\\r\\n\\[\\]\\+\\-\\*\\/\\$@<\\!]+")
                 (:pattern "[bB][rR][eE][aA][kK][--]")
                 (:pattern "[cC][oO][nN][tT][iI][nN][uU][eE][--]")
                 (:pattern "[tT][hH][rR][oO][wW][--]")
                 (:pattern "[rR][eE][tT][uU][rR][nN][--]")
                 (:pattern "[eE][xX][iI][tT][--]")
                 (:pattern "[tT][rR][yY][--]")
                 (:pattern "[tT][rR][aA][pP][--]")
                 (:pattern "[iI][fF][--]")
                 (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN][--]")
                 (:pattern "[fF][iI][lL][tT][eE][rR][--]")
                 (:pattern "[wW][oO][rR][kK][fF][lL][oO][wW][--]")
                 (:pattern "[cC][lL][aA][sS][sS][--]")
                 (:pattern "[eE][nN][uU][mM][--]")
                 (:pattern "[sS][wW][iI][tT][cC][hH][--]")
                 (:pattern "[fF][oO][rR][--]")
                 (:pattern "[wW][hH][iI][lL][eE][--]")
                 (:pattern "[pP][aA][rR][aA][lL][lL][eE][lL][--]"))
                (:repeat
                 (:choice
                  (:token-immediate (:pattern "[^\\{\\}\\(\\);,\\|\\&\"'\\s\\r\\n]+"))
                  (:seq (:token-immediate "\"") _expandable_string_literal_immediate)
                  (:seq (:token-immediate "'") _string_literal_immediate)
                  (:token-immediate "\"\"")
                  (:token-immediate "''"))))
  path_command_name_token (:pattern "[0-9a-zA-Z_?\\-\\.\\\\]+")
  path_command_name (:repeat1 (:choice path_command_name_token variable))
  command_name_expr (:choice command_name path_command_name _primary_expression)
  command_elements (:prec-right 0 (:repeat1 _command_element))
  _command_element (:prec-right 0
                    (:choice
                     command_parameter
                     (:seq _command_argument (:choice argument_list :blank))
                     redirection
                     stop_parsing))
  stop_parsing (:pattern "--%[^\\r\\n]*")
  command_argument_sep (:prec-right 0 (:choice (:repeat1 " ") ":"))
  _command_argument (:prec-right 6
                     (:choice
                      (:seq command_argument_sep (:choice generic_token :blank))
                      (:seq command_argument_sep array_literal_expression)
                      parenthesized_expression
                      script_block_expression))
  verbatim_command_argument (:seq "--%" _verbatim_command_argument_chars)
  redirections (:repeat1 redirection)
  redirection (:choice
               merging_redirection_operator
               (:seq file_redirection_operator redirected_file_name))
  redirected_file_name (:choice _command_argument _primary_expression)
  class_attribute (:choice
                   (:token (:alias (:prec 1 (:pattern "[hH][iI][dD][dD][eE][nN]")) "hidden"))
                   (:token (:alias (:prec 1 (:pattern "[sS][tT][aA][tT][iI][cC]")) "static")))
  class_property_definition (:seq
                             (:choice attribute :blank)
                             (:repeat class_attribute)
                             (:choice type_literal :blank)
                             variable
                             (:choice (:seq "=" _expression) :blank))
  class_method_parameter (:seq (:choice type_literal :blank) variable)
  class_method_parameter_list (:seq
                               class_method_parameter
                               (:repeat (:seq "," class_method_parameter)))
  class_method_definition (:seq
                           (:choice attribute :blank)
                           (:repeat class_attribute)
                           (:choice type_literal :blank)
                           simple_name
                           "("
                           (:choice class_method_parameter_list :blank)
                           ")"
                           "{"
                           (:choice script_block :blank)
                           "}")
  class_statement (:seq
                   (:token (:alias (:prec 1 (:pattern "[cC][lL][aA][sS][sS]")) "class"))
                   simple_name
                   (:choice (:seq ":" simple_name (:repeat (:seq "," simple_name))) :blank)
                   "{"
                   (:repeat
                    (:choice
                     (:seq class_property_definition _statement_terminator (:repeat ";"))
                     class_method_definition))
                   "}")
  enum_statement (:seq
                  (:token (:alias (:prec 1 (:pattern "[eE][nN][uU][mM]")) "enum"))
                  simple_name
                  "{"
                  (:repeat (:seq enum_member _statement_terminator (:repeat ";")))
                  "}")
  enum_member (:seq simple_name (:choice (:seq "=" integer_literal) :blank))
  _expression logical_expression
  logical_expression (:prec-left 0
                      (:choice
                       bitwise_expression
                       (:seq
                        logical_expression
                        (:choice
                         (:alias (:prec 1 (:pattern "[--][aA][nN][dD]")) "-and")
                         (:alias (:prec 1 (:pattern "[--][oO][rR]")) "-or")
                         (:alias (:prec 1 (:pattern "[--][xX][oO][rR]")) "-xor"))
                        bitwise_expression)))
  bitwise_expression (:prec-left 0
                      (:choice
                       comparison_expression
                       (:seq
                        bitwise_expression
                        (:choice
                         (:alias (:prec 1 (:pattern "[--][bB][aA][nN][dD]")) "-band")
                         (:alias (:prec 1 (:pattern "[--][bB][oO][rR]")) "-bor")
                         (:alias (:prec 1 (:pattern "[--][bB][xX][oO][rR]")) "-bxor"))
                        comparison_expression)))
  comparison_expression (:prec-left 0
                         (:choice
                          additive_expression
                          (:seq comparison_expression comparison_operator additive_expression)))
  additive_expression (:prec-left 0
                       (:choice
                        multiplicative_expression
                        (:seq additive_expression (:choice "+" "-") multiplicative_expression)))
  multiplicative_expression (:prec-left 0
                             (:choice
                              format_expression
                              (:seq
                               multiplicative_expression
                               (:choice "/" "\\" "%" "*")
                               format_expression)))
  format_expression (:prec-left 0
                     (:choice
                      range_expression
                      (:seq format_expression format_operator range_expression)))
  range_expression (:prec-left 0
                    (:choice
                     array_literal_expression
                     (:seq range_expression ".." array_literal_expression)))
  array_literal_expression (:prec-left 0
                            (:seq unary_expression (:repeat (:seq "," unary_expression))))
  unary_expression (:prec-right 0 (:choice _primary_expression expression_with_unary_operator))
  expression_with_unary_operator (:choice
                                  (:seq "," unary_expression)
                                  (:seq
                                   (:alias (:prec 1 (:pattern "[--][nN][oO][tT]")) "-not")
                                   unary_expression)
                                  (:seq "!" unary_expression)
                                  (:seq
                                   (:alias (:prec 1 (:pattern "[--][bB][nN][oO][tT]")) "-bnot")
                                   unary_expression)
                                  (:seq "+" unary_expression)
                                  (:seq "-" unary_expression)
                                  pre_increment_expression
                                  pre_decrement_expression
                                  cast_expression
                                  (:seq
                                   (:alias (:prec 1 (:pattern "[--][sS][pP][lL][iI][tT]")) "-split")
                                   unary_expression)
                                  (:seq
                                   (:alias (:prec 1 (:pattern "[--][jJ][oO][iI][nN]")) "-join")
                                   unary_expression))
  pre_increment_expression (:seq "++" unary_expression)
  pre_decrement_expression (:seq "--" unary_expression)
  cast_expression (:prec 3 (:seq type_literal unary_expression))
  attributed_variable (:seq type_literal variable)
  _primary_expression (:choice
                       _value
                       member_access
                       element_access
                       invokation_expression
                       post_increment_expression
                       post_decrement_expression)
  _value (:choice
          parenthesized_expression
          sub_expression
          array_expression
          script_block_expression
          hash_literal_expression
          _literal
          type_literal
          variable)
  parenthesized_expression (:seq "(" pipeline ")")
  sub_expression (:seq "$(" (:field :statements (:choice statement_list :blank)) ")")
  array_expression (:seq "@(" (:field :statements (:choice statement_list :blank)) ")")
  script_block_expression (:seq "{" (:choice param_block :blank) script_block "}")
  hash_literal_expression (:seq "@{" (:choice hash_literal_body :blank) "}")
  hash_literal_body (:repeat1 hash_entry)
  hash_entry (:seq key_expression "=" _statement _statement_terminator (:repeat ";"))
  key_expression (:choice simple_name unary_expression)
  post_increment_expression (:prec 2 (:seq _primary_expression "++"))
  post_decrement_expression (:prec 2 (:seq _primary_expression "--"))
  member_access (:prec-left 0
                 (:choice
                  (:seq _primary_expression (:token-immediate ".") member_name)
                  (:seq _primary_expression "::" member_name)))
  member_name (:choice simple_name string_literal expression_with_unary_operator _value)
  element_access (:prec 4 (:seq _primary_expression "[" _expression "]"))
  invokation_expression (:choice
                         (:seq _primary_expression (:token-immediate ".") member_name argument_list)
                         (:seq _primary_expression "::" member_name argument_list)
                         invokation_foreach_expression)
  invokation_foreach_expression (:seq
                                 _primary_expression
                                 (:token-immediate
                                  (:alias
                                   (:prec 1 (:pattern "[..][fF][oO][rR][eE][aA][cC][hH]"))
                                   ".foreach"))
                                 script_block_expression)
  argument_list (:seq
                 "("
                 (:field :argument_expression_list (:choice argument_expression_list :blank))
                 ")")
  argument_expression_list (:prec-left 0
                            (:seq argument_expression (:repeat (:seq "," argument_expression))))
  argument_expression logical_argument_expression
  logical_argument_expression (:prec-left 0
                               (:choice
                                bitwise_argument_expression
                                (:seq
                                 logical_argument_expression
                                 (:choice
                                  (:alias (:prec 1 (:pattern "[--][aA][nN][dD]")) "-and")
                                  (:alias (:prec 1 (:pattern "[--][oO][rR]")) "-or")
                                  (:alias (:prec 1 (:pattern "[--][xX][oO][rR]")) "-xor"))
                                 bitwise_argument_expression)))
  bitwise_argument_expression (:prec-left 0
                               (:choice
                                comparison_argument_expression
                                (:seq
                                 bitwise_argument_expression
                                 (:choice
                                  (:alias (:prec 1 (:pattern "[--][aA][nN][dD]")) "-and")
                                  (:alias (:prec 1 (:pattern "[--][oO][rR]")) "-or")
                                  (:alias (:prec 1 (:pattern "[--][xX][oO][rR]")) "-xor"))
                                 comparison_argument_expression)))
  comparison_argument_expression (:prec-left 0
                                  (:choice
                                   additive_argument_expression
                                   (:seq
                                    comparison_argument_expression
                                    comparison_operator
                                    additive_argument_expression)))
  additive_argument_expression (:prec-left 0
                                (:choice
                                 multiplicative_argument_expression
                                 (:seq
                                  additive_argument_expression
                                  (:choice "+" "-")
                                  multiplicative_argument_expression)))
  multiplicative_argument_expression (:prec-left 0
                                      (:choice
                                       format_argument_expression
                                       (:seq
                                        multiplicative_argument_expression
                                        (:choice "/" "\\" "%" "*")
                                        format_argument_expression)))
  format_argument_expression (:prec-left 0
                              (:choice
                               range_argument_expression
                               (:seq
                                format_argument_expression
                                format_operator
                                range_argument_expression)))
  range_argument_expression (:prec-left 0
                             (:choice
                              unary_expression
                              (:seq range_argument_expression ".." unary_expression)))
  type_literal (:seq "[" type_spec "]")
  type_spec (:choice
             (:seq array_type_name (:choice dimension :blank) "]")
             (:seq generic_type_name generic_type_arguments "]")
             type_name)
  dimension (:repeat1 ",")
  generic_type_arguments (:seq type_spec (:repeat (:seq "," type_spec)))
  attribute_list (:repeat1 attribute)
  attribute (:choice
             (:seq "[" attribute_name "(" (:choice attribute_arguments :blank) ")" "]")
             type_literal)
  attribute_name type_spec
  attribute_arguments (:seq attribute_argument (:repeat (:seq "," attribute_argument)))
  attribute_argument (:choice
                      _expression
                      (:seq simple_name (:choice (:seq "=" _expression) :blank)))}}
