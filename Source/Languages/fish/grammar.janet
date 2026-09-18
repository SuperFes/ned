# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "fish"
 :word word
 :extras [comment
          (:pattern "[\\u0009-\\u000D\\u0085\\u2028\\u2029\\u0020\\u3000\\u1680\\u2000-\\u2006\\u2008-\\u200A\\u205F\\u00A0\\u2007\\u202F]+")]
 :conflicts []
 :precedences []
 :externals [_concat _brace_concat _concat_list _begin_brace]
 :inline [_terminator _statement _base_expression]
 :supertypes []
 :rules
 {program (:repeat (:seq (:choice _statement :blank) _terminator))
  conditional_execution (:choice
                         (:prec-right -1 (:seq (:choice "and" "or") _statement))
                         (:prec-right -1 (:seq _statement (:choice "||" "&&") _statement)))
  pipe (:prec-left 0 (:seq _statement (:choice "&|" "2>|" "|") _statement))
  redirect_statement (:seq _statement (:choice file_redirect stream_redirect))
  _terminator (:choice ";" "&" "\n" "\r" "\r\n")
  _statement (:choice
              conditional_execution
              pipe
              command
              redirect_statement
              begin_statement
              if_statement
              while_statement
              for_statement
              switch_statement
              function_definition
              break
              continue
              return
              negated_statement)
  _terminated_statement (:seq _statement _terminator)
  _terminated_opt_statement (:seq (:choice _statement :blank) _terminator)
  negated_statement (:prec-left -1 (:seq (:choice "!" "not") _statement))
  command_substitution (:seq
                        (:choice "$" :blank)
                        "("
                        (:repeat (:seq (:choice _statement :blank) _terminator))
                        (:choice _statement :blank)
                        ")")
  function_definition (:seq
                       "function"
                       (:field :name _expression)
                       (:repeat (:field :option _expression))
                       _terminator
                       (:repeat _terminated_statement)
                       "end")
  integer (:pattern "(-|\\+)?\\d+")
  float (:pattern "(-|\\+)?\\d+\\.\\d+")
  return (:prec-left 0 (:seq "return" (:choice _expression :blank)))
  switch_statement (:seq
                    "switch"
                    (:field :value _expression)
                    _terminator
                    (:choice (:repeat1 case_clause) :blank)
                    "end")
  case_clause (:seq
               "case"
               (:repeat1 _expression)
               _terminator
               (:choice (:repeat1 _terminated_statement) :blank))
  break "break"
  continue "continue"
  for_statement (:seq
                 "for"
                 (:field :variable variable_name)
                 "in"
                 (:repeat1 (:field :value _expression))
                 _terminator
                 (:choice (:repeat1 _terminated_statement) :blank)
                 "end")
  while_statement (:seq
                   "while"
                   (:field :condition _terminated_statement)
                   (:choice (:repeat1 _terminated_opt_statement) :blank)
                   "end")
  if_statement (:seq
                "if"
                (:field :condition _terminated_statement)
                (:choice (:repeat1 _terminated_opt_statement) :blank)
                (:repeat else_if_clause)
                (:choice else_clause :blank)
                "end")
  else_if_clause (:seq
                  (:seq "else" "if")
                  (:field :condition _terminated_statement)
                  (:choice (:repeat1 _terminated_opt_statement) :blank))
  else_clause (:seq "else" _terminator (:choice (:repeat1 _terminated_opt_statement) :blank))
  begin_statement (:choice
                   (:seq "begin" (:choice (:repeat1 _terminated_opt_statement) :blank) "end")
                   (:seq
                    (:alias _begin_brace "{")
                    (:repeat _terminated_opt_statement)
                    (:choice _statement :blank)
                    "}"))
  comment (:token (:prec -11 (:pattern "#.*")))
  variable_name (:pattern "[a-zA-Z0-9_]+")
  variable_expansion (:prec-left 0
                      (:seq
                       "$"
                       (:choice variable_name variable_expansion)
                       (:repeat (:seq _concat_list list_element_access))))
  index (:choice
         integer
         single_quote_string
         variable_expansion
         double_quote_string
         command_substitution)
  range (:prec-right 2 (:seq (:choice index :blank) ".." (:choice index :blank)))
  list_element_access (:seq "[" (:repeat (:choice index range)) "]")
  brace_expansion (:prec-right 0
                   (:seq
                    "{"
                    (:seq
                     (:choice _brace_expression :blank)
                     (:repeat (:seq "," (:choice _brace_expression :blank))))
                    "}"))
  double_quote_string (:seq
                       "\""
                       (:repeat
                        (:choice
                         (:pattern "[^\\$\\\\\"]+")
                         variable_expansion
                         escape_sequence
                         command_substitution))
                       "\"")
  single_quote_string (:seq "'" (:repeat (:choice (:pattern "[^'\\\\]+") escape_sequence)) "'")
  escape_sequence (:token
                   (:seq
                    "\\"
                    (:token-immediate
                     (:choice
                      (:pattern "[^xXuUc]")
                      (:pattern "[0-7]{1,3}")
                      (:pattern "x[0-9a-fA-F]{0,2}")
                      (:pattern "X[0-9a-fA-F]{0,2}")
                      (:pattern "u[0-9a-fA-F]{0,4}")
                      (:pattern "U[0-9a-fA-F]{0,8}")
                      (:pattern "c[a-zA-Z]?")))))
  command (:prec-right 0
           (:seq
            (:field :name _expression)
            (:repeat
             (:choice
              (:field :redirect (:choice file_redirect stream_redirect))
              (:field :argument _expression)))))
  stream_redirect (:pattern "\\d*(>>|>|<)&[012-]")
  direction (:pattern "(\\d*|&)(>>?\\??|<)")
  file_redirect (:seq (:field :operator direction) (:field :destination _expression))
  _special_character (:choice "[" "]")
  concatenation (:seq
                 (:choice _base_expression _special_character)
                 (:repeat1 (:seq _concat (:choice _base_expression _special_character "#"))))
  _expression (:choice _base_expression concatenation (:alias _special_character word))
  _base_expression (:choice
                    command_substitution
                    single_quote_string
                    double_quote_string
                    variable_expansion
                    word
                    integer
                    float
                    brace_expansion
                    escape_sequence
                    glob
                    home_dir_expansion)
  brace_concatenation (:seq
                       (:choice _base_brace_expression brace_expansion)
                       (:repeat1
                        (:seq _brace_concat (:choice _base_brace_expression brace_expansion))))
  _brace_expression (:choice
                     (:alias brace_concatenation concatenation)
                     _base_brace_expression
                     brace_expansion)
  _base_brace_expression (:choice
                          command_substitution
                          single_quote_string
                          double_quote_string
                          variable_expansion
                          (:alias brace_word word)
                          integer
                          float
                          escape_sequence
                          glob)
  home_dir_expansion "~"
  glob (:token (:repeat1 "*"))
  word (:pattern "[^\\$\\*~#\\(\\)\\{\\}\\[\\]<>\"'&\\|;\\\\\\s][^\\$\\*~\\(\\)\\{\\}\\[\\]<>\"'&\\|;\\\\\\s]*")
  brace_word (:pattern "[^\\$'\\*\",\\\\\\{\\}\\(\\)]+")}}
