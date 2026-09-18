# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "meson"
 :extras [comment (:pattern "\\s")]
 :conflicts [[_logic_unit normal_command]
             [variableunit _logic_unit]
             [operatorunit _logic_unit]
             [expression_statement]
             [variableunit]
             [pair variableunit _logic_unit]
             [operatorunit variableunit _logic_unit]
             [normal_command variableunit _logic_unit]
             [pair _logic_unit]
             [listitem expression_statement]
             [list variableunit]
             [_logic_unit]]
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source_file (:repeat _unit)
  _unit (:seq
         (:choice
          expression_statement
          comment
          normal_command
          operatorunit
          if_condition
          foreach_command))
  normal_command (:seq
                  (:field :command identifier)
                  "("
                  (:choice
                   (:seq
                    (:repeat
                     (:seq
                      (:choice
                       dictionaries
                       normal_command
                       listitem
                       list
                       expression_statement
                       pair
                       variableunit
                       var_unit)
                      ","))
                    (:choice
                     dictionaries
                     normal_command
                     listitem
                     list
                     expression_statement
                     pair
                     variableunit
                     var_unit)
                    (:choice "," :blank))
                   :blank)
                  ")")
  bool (:choice "true" "false")
  string (:choice
          (:seq
           "'"
           (:choice
            (:repeat
             (:choice
              (:token-immediate (:prec 1 (:pattern "[^'\\\\^@]+")))
              escape_sequence
              formatunit))
            :blank)
           "'")
          (:seq
           "'''"
           (:choice
            (:repeat
             (:choice
              (:token-immediate (:prec 1 (:pattern "[^'''\\\\^@]+")))
              escape_sequence
              formatunit))
            :blank)
           "'''"))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu0-7]")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9a-fA-F]{2}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}"))))
  formatunit (:seq "@" (:choice number (:field :variable identifier)) "@")
  dictionaries (:seq "{" (:repeat (:seq pair ",")) (:choice (:seq pair) :blank) "}")
  listitem (:seq
            identifier
            "["
            (:choice (:field :index number) (:field :index identifier) (:field :key string))
            "]")
  list (:seq
        "["
        (:choice
         (:seq
          (:repeat
           (:seq
            (:choice
             dictionaries
             normal_command
             number
             string
             list
             listitem
             bool
             variableunit
             (:field :variable identifier)
             (:field :variable expression_statement))
            ","))
          (:choice
           (:choice
            normal_command
            number
            string
            list
            bool
            variableunit
            (:field :variable identifier)
            (:field :variable expression_statement))
           :blank))
         :blank)
        "]")
  pair (:seq
        (:field :key (:choice var_unit variableunit identifier string))
        ":"
        (:field :value
         (:choice
          bool
          var_unit
          variableunit
          dictionaries
          normal_command
          number
          identifier
          list
          string
          expression_statement)))
  identifier (:pattern "[A-Za-z_][A-Za-z0-9_]*")
  number (:token
          (:choice
           (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*"))
           (:seq (:choice "0x" "0X") (:pattern "[\\da-fA-F](_?[\\da-fA-F])*"))
           (:choice
            (:seq
             (:choice
              "0"
              (:seq
               (:choice "0" :blank)
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank)))
             "."
             (:choice (:pattern "\\d(_?\\d)*") :blank)
             (:choice
              (:seq
               (:choice "e" "E")
               (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*")))
              :blank))
            (:seq
             "."
             (:pattern "\\d(_?\\d)*")
             (:choice
              (:seq
               (:choice "e" "E")
               (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*")))
              :blank))
            (:seq
             (:choice
              "0"
              (:seq
               (:choice "0" :blank)
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank)))
             (:seq
              (:choice "e" "E")
              (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*"))))
            (:seq (:pattern "\\d(_?\\d)*")))
           (:seq (:choice "0b" "0B") (:pattern "[0-1](_?[0-1])*"))
           (:seq (:choice "0o" "0O") (:pattern "[0-7](_?[0-7])*"))
           (:seq
            (:choice
             (:seq (:choice "0x" "0X") (:pattern "[\\da-fA-F](_?[\\da-fA-F])*"))
             (:seq (:choice "0b" "0B") (:pattern "[0-1](_?[0-1])*"))
             (:seq (:choice "0o" "0O") (:pattern "[0-7](_?[0-7])*"))
             (:pattern "\\d(_?\\d)*"))
            "n")))
  operatorunit (:seq
                identifier
                (:choice "=" "+=" "-=" "/=" "/")
                (:choice
                 listitem
                 dictionaries
                 list
                 variableunit
                 bool
                 normal_command
                 number
                 expression_statement
                 ternaryoperator
                 var_unit))
  variableunit (:seq
                (:choice normal_command string list expression_statement (:field :value identifier))
                (:choice
                 (:repeat
                  (:seq
                   (:choice "+" "/")
                   (:choice
                    normal_command
                    list
                    string
                    expression_statement
                    (:field :value identifier))))
                 :blank))
  ternaryoperator (:seq var_unit "?" var_unit ":" var_unit)
  _logic_unit (:choice
               string
               bool
               normal_command
               number
               expression_statement
               listitem
               list
               (:seq "(" var_unit ")")
               (:field :value identifier))
  var_unit (:seq
            (:repeat
             (:seq
              (:choice "not" :blank)
              _logic_unit
              (:choice "not" :blank)
              (:choice "==" "!=" ">=" ">" "<" "and" "or" "in")))
            (:choice "not" :blank)
            _logic_unit)
  expression_statement (:seq
                        (:choice
                         (:field :object identifier)
                         (:field :object listitem)
                         (:field :object string)
                         (:field :function normal_command))
                        (:repeat1
                         (:seq
                          "."
                          (:choice
                           (:field :function normal_command)
                           (:field :object listitem)
                           (:field :function expression_statement)
                           (:field :property identifier))
                          (:choice (:field :index (:seq "[" number "]")) :blank))))
  if_condition (:seq if_command (:repeat elseif_command) (:choice else_command :blank) "endif")
  if_command (:seq
              "if"
              var_unit
              (:choice (:repeat _unit) :blank)
              (:choice (:choice keyword_break keyword_continue) :blank))
  elseif_command (:seq
                  "elif"
                  var_unit
                  (:choice (:repeat _unit) :blank)
                  (:choice (:choice keyword_break keyword_continue) :blank))
  else_command (:seq "else" (:choice (:repeat _unit) :blank))
  foreach_command (:seq
                   "foreach"
                   (:seq (:field :item identifier) (:repeat (:seq "," (:field :item identifier))))
                   ":"
                   (:choice
                    (:field :array identifier)
                    (:field :array list)
                    (:field :dictionaries dictionaries))
                   (:choice (:repeat _unit) :blank)
                   "endforeach")
  keyword_break "break"
  keyword_continue "continue"
  comment (:token (:seq "#" (:pattern "[^\\n]+")))}}
