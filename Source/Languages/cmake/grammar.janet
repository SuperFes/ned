# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "cmake"
 :extras []
 :conflicts []
 :precedences []
 :externals [bracket_argument_open
             bracket_argument_content
             bracket_argument_close
             bracket_comment_open
             bracket_comment_content
             bracket_comment_close
             line_comment]
 :inline []
 :supertypes []
 :rules
 {source_file (:repeat _untrimmed_command_invocation)
  escape_sequence (:choice _escape_identity _escape_encoded _escape_semicolon)
  _escape_identity (:pattern "\\\\[^A-Za-z0-9;]")
  _escape_encoded (:choice "\\t" "\\r" "\\n")
  _escape_semicolon (:choice ";" "\\;")
  variable (:prec-left 0
            (:repeat1 (:choice (:pattern "[a-zA-Z0-9/_.+-]") escape_sequence variable_ref)))
  variable_ref (:choice normal_var env_var cache_var)
  normal_var (:seq "$" "{" variable "}")
  env_var (:seq "$" "ENV" "{" variable "}")
  cache_var (:seq "$" "CACHE" "{" variable "}")
  bracket_argument (:seq bracket_argument_open bracket_argument_content bracket_argument_close)
  bracket_comment (:seq bracket_comment_open bracket_comment_content bracket_comment_close)
  argument (:choice bracket_argument quoted_argument unquoted_argument)
  _untrimmed_argument (:choice
                       (:pattern "\\s")
                       bracket_comment
                       line_comment
                       argument
                       _paren_argument)
  _paren_argument (:seq "(" (:repeat _untrimmed_argument) ")")
  quoted_argument (:seq "\"" (:choice quoted_element :blank) "\"")
  quoted_element (:repeat1 (:choice variable_ref _quoted_text escape_sequence))
  _quoted_text (:prec-left 0 (:repeat1 (:choice "$" (:pattern "[^\\\\\"]"))))
  unquoted_argument (:prec-right 0 (:repeat1 (:choice variable_ref _unquoted_text escape_sequence)))
  _unquoted_text (:prec-left 0 (:repeat1 (:choice "$" (:pattern "[^()#\"\\\\]"))))
  body (:prec-right 0 (:repeat1 _untrimmed_command_invocation))
  argument_list (:repeat1 _untrimmed_argument)
  if_command (:seq if (:repeat (:pattern "[\\t ]")) "(" argument_list ")")
  elseif_command (:seq elseif (:repeat (:pattern "[\\t ]")) "(" argument_list ")")
  else_command (:seq else (:repeat (:pattern "[\\t ]")) "(" (:choice argument_list :blank) ")")
  endif_command (:seq endif (:repeat (:pattern "[\\t ]")) "(" (:choice argument_list :blank) ")")
  if_condition (:seq if_command (:repeat (:choice body elseif_command else_command)) endif_command)
  foreach_command (:seq foreach (:repeat (:pattern "[\\t ]")) "(" argument_list ")")
  endforeach_command (:seq
                      endforeach
                      (:repeat (:pattern "[\\t ]"))
                      "("
                      (:choice argument :blank)
                      ")")
  foreach_loop (:seq foreach_command body endforeach_command)
  while_command (:seq while (:repeat (:pattern "[\\t ]")) "(" argument_list ")")
  endwhile_command (:seq
                    endwhile
                    (:repeat (:pattern "[\\t ]"))
                    "("
                    (:choice (:seq (:pattern "\\s*") argument (:pattern "\\s*")) :blank)
                    ")")
  while_loop (:seq while_command body endwhile_command)
  function_command (:seq function (:repeat (:pattern "[\\t ]")) "(" argument_list ")")
  endfunction_command (:seq
                       endfunction
                       (:repeat (:pattern "[\\t ]"))
                       "("
                       (:choice argument_list :blank)
                       ")")
  function_def (:seq function_command body endfunction_command)
  macro_command (:seq macro (:repeat (:pattern "[\\t ]")) "(" argument_list ")")
  endmacro_command (:seq
                    endmacro
                    (:repeat (:pattern "[\\t ]"))
                    "("
                    (:choice argument_list :blank)
                    ")")
  macro_def (:seq macro_command body endmacro_command)
  block_command (:seq block (:repeat (:pattern "[\\t ]")) "(" (:choice argument_list :blank) ")")
  endblock_command (:seq
                    endblock
                    (:repeat (:pattern "[\\t ]"))
                    "("
                    (:choice argument_list :blank)
                    ")")
  block_def (:seq block_command body endblock_command)
  normal_command (:seq
                  identifier
                  (:repeat (:pattern "[\\t ]"))
                  "("
                  (:choice argument_list :blank)
                  ")")
  _command_invocation (:choice
                       normal_command
                       if_condition
                       foreach_loop
                       while_loop
                       function_def
                       macro_def
                       block_def)
  _untrimmed_command_invocation (:choice
                                 (:pattern "\\s")
                                 bracket_comment
                                 line_comment
                                 _command_invocation)
  if (:pattern "[iI][fF]")
  elseif (:pattern "[eE][lL][sS][eE][iI][fF]")
  else (:pattern "[eE][lL][sS][eE]")
  endif (:pattern "[eE][nN][dD][iI][fF]")
  foreach (:pattern "[fF][oO][rR][eE][aA][cC][hH]")
  endforeach (:pattern "[eE][nN][dD][fF][oO][rR][eE][aA][cC][hH]")
  while (:pattern "[wW][hH][iI][lL][eE]")
  endwhile (:pattern "[eE][nN][dD][wW][hH][iI][lL][eE]")
  function (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]")
  endfunction (:pattern "[eE][nN][dD][fF][uU][nN][cC][tT][iI][oO][nN]")
  macro (:pattern "[mM][aA][cC][rR][oO]")
  endmacro (:pattern "[eE][nN][dD][mM][aA][cC][rR][oO]")
  block (:pattern "[bB][lL][oO][cC][kK]")
  endblock (:pattern "[eE][nN][dD][bB][lL][oO][cC][kK]")
  identifier (:pattern "[A-Za-z_][A-Za-z0-9_]*")
  integer (:pattern "[+-]*\\d+")}}
