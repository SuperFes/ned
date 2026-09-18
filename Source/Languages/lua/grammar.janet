# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "lua"
 :word identifier
 :extras [comment (:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals [_block_comment_start
             _block_comment_content
             _block_comment_end
             _block_string_start
             _block_string_content
             _block_string_end]
 :inline []
 :supertypes [statement expression declaration variable]
 :rules
 {chunk (:seq (:choice hash_bang_line :blank) (:repeat statement) (:choice return_statement :blank))
  hash_bang_line (:pattern "#.*")
  _block (:choice
          (:seq (:repeat1 statement) (:choice return_statement :blank))
          (:seq (:repeat statement) return_statement))
  statement (:choice
             empty_statement
             assignment_statement
             function_call
             label_statement
             break_statement
             goto_statement
             do_statement
             while_statement
             repeat_statement
             if_statement
             for_statement
             declaration)
  return_statement (:seq
                    "return"
                    (:choice (:alias _expression_list expression_list) :blank)
                    (:choice ";" :blank))
  empty_statement ";"
  assignment_statement (:seq
                        (:alias _variable_assignment_varlist variable_list)
                        (:field :operator "=")
                        (:alias _variable_assignment_explist expression_list))
  _variable_assignment_varlist (:seq
                                (:field :name variable)
                                (:repeat (:seq "," (:field :name variable))))
  _variable_assignment_explist (:seq
                                (:field :value expression)
                                (:repeat (:seq "," (:field :value expression))))
  label_statement (:seq "::" identifier "::")
  break_statement "break"
  goto_statement (:seq "goto" identifier)
  do_statement (:seq "do" (:field :body (:alias (:choice _block :blank) block)) "end")
  while_statement (:seq
                   "while"
                   (:field :condition expression)
                   "do"
                   (:field :body (:alias (:choice _block :blank) block))
                   "end")
  repeat_statement (:seq
                    "repeat"
                    (:field :body (:alias (:choice _block :blank) block))
                    "until"
                    (:field :condition expression))
  if_statement (:seq
                "if"
                (:field :condition expression)
                "then"
                (:field :consequence (:alias (:choice _block :blank) block))
                (:repeat (:field :alternative elseif_statement))
                (:choice (:field :alternative else_statement) :blank)
                "end")
  elseif_statement (:seq
                    "elseif"
                    (:field :condition expression)
                    "then"
                    (:field :consequence (:alias (:choice _block :blank) block)))
  else_statement (:seq "else" (:field :body (:alias (:choice _block :blank) block)))
  for_statement (:seq
                 "for"
                 (:field :clause (:choice for_generic_clause for_numeric_clause))
                 "do"
                 (:field :body (:alias (:choice _block :blank) block))
                 "end")
  for_generic_clause (:seq
                      (:alias _name_list variable_list)
                      "in"
                      (:alias _expression_list expression_list))
  for_numeric_clause (:seq
                      (:field :name identifier)
                      (:field :operator "=")
                      (:field :start expression)
                      ","
                      (:field :end expression)
                      (:choice (:seq "," (:field :step expression)) :blank))
  _name_list (:seq (:field :name identifier) (:repeat (:seq "," (:field :name identifier))))
  declaration (:choice
               function_declaration
               (:field :local_declaration (:alias _local_function_declaration function_declaration))
               (:field :local_declaration variable_declaration)
               (:field :global_declaration
                (:alias _global_function_declaration function_declaration))
               (:field :global_declaration
                (:alias _global_variable_declaration variable_declaration))
               (:field :global_declaration
                (:alias _global_implicit_variable_declaration implicit_variable_declaration)))
  function_declaration (:seq "function" (:field :name _function_name) _function_body)
  _local_function_declaration (:seq "local" "function" (:field :name identifier) _function_body)
  _global_function_declaration (:seq "global" "function" (:field :name identifier) _function_body)
  _function_name (:choice
                  _function_name_prefix_expression
                  (:alias _function_name_method_index_expression method_index_expression))
  _function_name_prefix_expression (:choice
                                    identifier
                                    (:alias
                                     _function_name_dot_index_expression
                                     dot_index_expression))
  _function_name_dot_index_expression (:seq
                                       (:field :table _function_name_prefix_expression)
                                       "."
                                       (:field :field identifier))
  _function_name_method_index_expression (:seq
                                          (:field :table _function_name_prefix_expression)
                                          ":"
                                          (:field :method identifier))
  variable_declaration (:seq
                        "local"
                        (:choice
                         (:alias _att_name_list variable_list)
                         (:alias _variable_assignment assignment_statement)))
  _global_variable_declaration (:seq
                                "global"
                                (:choice
                                 (:alias _att_name_list variable_list)
                                 (:alias _variable_assignment assignment_statement)))
  _variable_assignment (:seq
                        (:alias _att_name_list variable_list)
                        (:field :operator "=")
                        (:alias _variable_assignment_explist expression_list))
  _att_name_list (:seq
                  (:choice (:field :attribute (:alias _attrib attribute)) :blank)
                  (:seq
                   (:seq
                    (:field :name identifier)
                    (:choice (:field :attribute (:alias _attrib attribute)) :blank))
                   (:repeat
                    (:seq
                     ","
                     (:seq
                      (:field :name identifier)
                      (:choice (:field :attribute (:alias _attrib attribute)) :blank))))))
  _global_implicit_variable_declaration (:seq
                                         "global"
                                         (:choice
                                          (:field :attribute (:alias _attrib attribute))
                                          :blank)
                                         "*")
  _attrib (:seq "<" identifier ">")
  _expression_list (:seq expression (:repeat (:seq "," expression)))
  expression (:choice
              (:ref "nil")
              (:ref "false")
              (:ref "true")
              number
              string
              vararg_expression
              function_definition
              variable
              function_call
              parenthesized_expression
              table_constructor
              binary_expression
              unary_expression)
  (:ref "nil") "nil"
  (:ref "false") "false"
  (:ref "true") "true"
  number (:token
          (:choice
           (:choice
            (:seq (:pattern "[0-9]+") (:pattern "U?LL" "i"))
            (:seq
             (:choice
              (:seq (:choice (:pattern "[0-9]+") :blank) (:choice "." :blank) (:pattern "[0-9]+"))
              (:seq (:pattern "[0-9]+") (:choice "." :blank) (:choice (:pattern "[0-9]+") :blank)))
             (:choice
              (:seq (:choice "e" "E") (:seq (:choice (:choice "-" "+") :blank) (:pattern "[0-9]+")))
              :blank)
             (:choice (:choice "i" "I") :blank)))
           (:seq
            (:choice "0x" "0X")
            (:choice
             (:seq (:pattern "[a-fA-F0-9]+") (:pattern "U?LL" "i"))
             (:seq
              (:choice
               (:seq
                (:choice (:pattern "[a-fA-F0-9]+") :blank)
                (:choice "." :blank)
                (:pattern "[a-fA-F0-9]+"))
               (:seq
                (:pattern "[a-fA-F0-9]+")
                (:choice "." :blank)
                (:choice (:pattern "[a-fA-F0-9]+") :blank)))
              (:choice
               (:seq
                (:choice "p" "P")
                (:seq (:choice (:choice "-" "+") :blank) (:pattern "[0-9]+")))
               :blank)
              (:choice (:choice "i" "I") :blank))))
           (:seq
            (:choice "0b" "0B")
            (:choice
             (:seq (:pattern "[01]+") (:pattern "U?LL" "i"))
             (:seq (:pattern "[01]+") (:choice (:choice "i" "I") :blank))))))
  string (:choice _quote_string _block_string)
  _quote_string (:choice
                 (:seq
                  (:field :start (:alias "\"" "\""))
                  (:field :content
                   (:choice (:alias _doublequote_string_content string_content) :blank))
                  (:field :end (:alias "\"" "\"")))
                 (:seq
                  (:field :start (:alias "'" "'"))
                  (:field :content
                   (:choice (:alias _singlequote_string_content string_content) :blank))
                  (:field :end (:alias "'" "'"))))
  _doublequote_string_content (:repeat1
                               (:choice
                                (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+")))
                                escape_sequence))
  _singlequote_string_content (:repeat1
                               (:choice
                                (:token-immediate (:prec 1 (:pattern "[^'\\\\]+")))
                                escape_sequence))
  _block_string (:seq
                 (:field :start (:alias _block_string_start "[["))
                 (:field :content (:alias _block_string_content string_content))
                 (:field :end (:alias _block_string_end "]]")))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[\\nabfnrtv\\\\'\"]")
                     (:pattern "z\\s*")
                     (:pattern "[0-9]{1,3}")
                     (:pattern "x[0-9a-fA-F]{2}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}"))))
  vararg_expression "..."
  function_definition (:seq "function" _function_body)
  _function_body (:seq
                  (:field :parameters parameters)
                  (:field :body (:alias (:choice _block :blank) block))
                  "end")
  parameters (:seq "(" (:choice _parameter_list :blank) ")")
  _parameter_list (:choice
                   (:seq
                    (:seq (:field :name identifier) (:repeat (:seq "," (:field :name identifier))))
                    (:choice (:seq "," _vararg_parameter) :blank))
                   _vararg_parameter)
  _vararg_parameter (:seq vararg_expression (:choice (:field :name identifier) :blank))
  _prefix_expression (:prec 1 (:choice variable function_call parenthesized_expression))
  variable (:choice _contextual_keyword identifier bracket_index_expression dot_index_expression)
  bracket_index_expression (:seq
                            (:field :table _prefix_expression)
                            "["
                            (:field :field expression)
                            "]")
  dot_index_expression (:seq (:field :table _prefix_expression) "." (:field :field identifier))
  function_call (:seq
                 (:field :name (:choice _prefix_expression method_index_expression))
                 (:field :arguments arguments))
  method_index_expression (:seq (:field :table _prefix_expression) ":" (:field :method identifier))
  arguments (:choice
             (:seq "(" (:choice (:seq expression (:repeat (:seq "," expression))) :blank) ")")
             table_constructor
             string)
  parenthesized_expression (:seq "(" expression ")")
  table_constructor (:seq "{" (:choice _field_list :blank) "}")
  _field_list (:seq field (:repeat (:seq _field_sep field)) (:choice _field_sep :blank))
  _field_sep (:choice "," ";")
  field (:choice
         (:seq "[" (:field :name expression) "]" (:field :operator "=") (:field :value expression))
         (:seq
          (:field :name (:choice _contextual_keyword identifier))
          "="
          (:field :value expression))
         (:field :value expression))
  binary_expression (:choice
                     (:prec-left 1
                      (:seq
                       (:field :left expression)
                       (:field :operator "or")
                       (:field :right expression)))
                     (:prec-left 2
                      (:seq
                       (:field :left expression)
                       (:field :operator "and")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "<")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "==")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "~=")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator ">=")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator ">")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator "~")
                       (:field :right expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "<<")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "+")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "-")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "*")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "/")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "//")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
                       (:field :right expression)))
                     (:prec-right 8
                      (:seq
                       (:field :left expression)
                       (:field :operator "..")
                       (:field :right expression)))
                     (:prec-right 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "^")
                       (:field :right expression))))
  unary_expression (:prec-left 11
                    (:seq
                     (:field :operator (:choice "not" "#" "-" "~"))
                     (:field :operand expression)))
  identifier (:token
              (:seq
               (:pattern "[^\\p{Control}\\s+\\-*/%^#&~|<>=(){}\\[\\];:,.\\\\'\"\\d]")
               (:pattern "[^\\p{Control}\\s+\\-*/%^#&~|<>=(){}\\[\\];:,.\\\\'\"]*")))
  comment (:choice
           (:seq
            (:field :start "--")
            (:field :content (:alias (:pattern "[^\\r\\n]*") comment_content)))
           (:seq
            (:field :start (:alias _block_comment_start "[["))
            (:field :content (:alias _block_comment_content comment_content))
            (:field :end (:alias _block_comment_end "]]"))))
  _contextual_keyword "global"}}
