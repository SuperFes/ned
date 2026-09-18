# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "go"
 :word identifier
 :extras [comment (:pattern "\\s")]
 :conflicts [[_simple_type _expression]
             [_simple_type generic_type _expression]
             [qualified_type _expression]
             [generic_type _simple_type]
             [parameter_declaration _simple_type]
             [type_parameter_declaration _simple_type _expression]
             [type_parameter_declaration _expression]
             [type_parameter_declaration _simple_type generic_type _expression]]
 :precedences []
 :externals []
 :inline [_type
          _type_identifier
          _field_identifier
          _package_identifier
          _top_level_declaration
          _string_literal
          _interface_elem]
 :supertypes [_expression _type _simple_type _statement _simple_statement]
 :reserved
 {:global ["break"
            "default"
            "func"
            "interface"
            "select"
            "case"
            "defer"
            "go"
            "map"
            "struct"
            "chan"
            "else"
            "goto"
            "package"
            "switch"
            "const"
            "fallthrough"
            "if"
            "range"
            "type"
            "continue"
            "for"
            "import"
            "return"
            "var"]}
 :rules
 {source_file (:seq
               (:repeat
                (:choice
                 (:seq _statement (:choice (:pattern "\\n") ";" "\0"))
                 (:seq _top_level_declaration (:choice (:pattern "\\n") ";" "\0"))))
               (:choice _top_level_declaration :blank))
  _top_level_declaration (:choice
                          package_clause
                          function_declaration
                          method_declaration
                          import_declaration)
  package_clause (:seq "package" _package_identifier)
  import_declaration (:seq "import" (:choice import_spec import_spec_list))
  import_spec (:seq
               (:choice (:field :name (:choice dot blank_identifier _package_identifier)) :blank)
               (:field :path _string_literal))
  dot "."
  blank_identifier "_"
  import_spec_list (:seq
                    "("
                    (:choice
                     (:seq
                      import_spec
                      (:repeat (:seq (:choice (:pattern "\\n") ";" "\0") import_spec))
                      (:choice (:choice (:pattern "\\n") ";" "\0") :blank))
                     :blank)
                    ")")
  _declaration (:choice const_declaration type_declaration var_declaration)
  const_declaration (:seq
                     "const"
                     (:choice
                      const_spec
                      (:seq "(" (:repeat (:seq const_spec (:choice (:pattern "\\n") ";" "\0"))) ")")))
  const_spec (:prec-left 0
              (:seq
               (:field :name (:seq identifier (:repeat (:seq "," identifier))))
               (:choice
                (:seq (:choice (:field :type _type) :blank) "=" (:field :value expression_list))
                :blank)))
  var_declaration (:seq "var" (:choice var_spec var_spec_list))
  var_spec (:seq
            (:seq (:field :name identifier) (:repeat (:seq "," (:field :name identifier))))
            (:choice
             (:seq (:field :type _type) (:choice (:seq "=" (:field :value expression_list)) :blank))
             (:seq "=" (:field :value expression_list))))
  var_spec_list (:seq "(" (:repeat (:seq var_spec (:choice (:pattern "\\n") ";" "\0"))) ")")
  function_declaration (:prec-right 1
                        (:seq
                         "func"
                         (:field :name identifier)
                         (:field :type_parameters (:choice type_parameter_list :blank))
                         (:field :parameters parameter_list)
                         (:field :result (:choice (:choice parameter_list _simple_type) :blank))
                         (:field :body (:choice block :blank))))
  method_declaration (:prec-right 1
                      (:seq
                       "func"
                       (:field :receiver parameter_list)
                       (:field :name _field_identifier)
                       (:field :parameters parameter_list)
                       (:field :result (:choice (:choice parameter_list _simple_type) :blank))
                       (:field :body (:choice block :blank))))
  type_parameter_list (:seq
                       "["
                       (:seq
                        type_parameter_declaration
                        (:repeat (:seq "," type_parameter_declaration)))
                       (:choice "," :blank)
                       "]")
  type_parameter_declaration (:seq
                              (:seq
                               (:field :name identifier)
                               (:repeat (:seq "," (:field :name identifier))))
                              (:field :type (:alias type_elem type_constraint)))
  parameter_list (:seq
                  "("
                  (:choice
                   (:seq
                    (:choice
                     (:seq
                      (:choice parameter_declaration variadic_parameter_declaration)
                      (:repeat
                       (:seq "," (:choice parameter_declaration variadic_parameter_declaration))))
                     :blank)
                    (:choice "," :blank))
                   :blank)
                  ")")
  parameter_declaration (:seq
                         (:choice
                          (:seq
                           (:field :name identifier)
                           (:repeat (:seq "," (:field :name identifier))))
                          :blank)
                         (:field :type _type))
  variadic_parameter_declaration (:seq
                                  (:field :name (:choice identifier :blank))
                                  "..."
                                  (:field :type _type))
  type_alias (:seq
              (:field :name _type_identifier)
              (:field :type_parameters (:choice type_parameter_list :blank))
              "="
              (:field :type _type))
  type_declaration (:seq
                    "type"
                    (:choice
                     type_spec
                     type_alias
                     (:seq
                      "("
                      (:choice
                       (:seq
                        (:choice type_spec type_alias)
                        (:repeat
                         (:seq (:choice (:pattern "\\n") ";" "\0") (:choice type_spec type_alias)))
                        (:choice (:choice (:pattern "\\n") ";" "\0") :blank))
                       :blank)
                      ")")))
  type_spec (:seq
             (:field :name _type_identifier)
             (:field :type_parameters (:choice type_parameter_list :blank))
             (:field :type _type))
  field_name_list (:seq _field_identifier (:repeat (:seq "," _field_identifier)))
  expression_list (:seq _expression (:repeat (:seq "," _expression)))
  _type (:choice _simple_type parenthesized_type)
  parenthesized_type (:seq "(" _type ")")
  _simple_type (:choice
                (:prec-dynamic -1 _type_identifier)
                generic_type
                qualified_type
                pointer_type
                struct_type
                interface_type
                array_type
                slice_type
                (:prec-dynamic 3 map_type)
                channel_type
                function_type
                negated_type)
  generic_type (:prec-dynamic 1
                (:seq
                 (:field :type (:choice _type_identifier qualified_type negated_type))
                 (:field :type_arguments type_arguments)))
  type_arguments (:prec-dynamic 2
                  (:seq
                   "["
                   (:seq type_elem (:repeat (:seq "," type_elem)))
                   (:choice "," :blank)
                   "]"))
  pointer_type (:prec 6 (:seq "*" _type))
  array_type (:prec-right 0 (:seq "[" (:field :length _expression) "]" (:field :element _type)))
  implicit_length_array_type (:seq "[" "..." "]" (:field :element _type))
  slice_type (:prec-right 0 (:seq "[" "]" (:field :element _type)))
  struct_type (:seq "struct" field_declaration_list)
  negated_type (:prec-left 0 (:seq "~" _type))
  field_declaration_list (:seq
                          "{"
                          (:choice
                           (:seq
                            field_declaration
                            (:repeat (:seq (:choice (:pattern "\\n") ";" "\0") field_declaration))
                            (:choice (:choice (:pattern "\\n") ";" "\0") :blank))
                           :blank)
                          "}")
  field_declaration (:seq
                     (:choice
                      (:seq
                       (:seq
                        (:field :name _field_identifier)
                        (:repeat (:seq "," (:field :name _field_identifier))))
                       (:field :type _type))
                      (:seq
                       (:choice "*" :blank)
                       (:field :type (:choice _type_identifier qualified_type generic_type))))
                     (:field :tag (:choice _string_literal :blank)))
  interface_type (:seq
                  "interface"
                  "{"
                  (:choice
                   (:seq
                    _interface_elem
                    (:repeat (:seq (:choice (:pattern "\\n") ";" "\0") _interface_elem))
                    (:choice (:choice (:pattern "\\n") ";" "\0") :blank))
                   :blank)
                  "}")
  _interface_elem (:choice method_elem type_elem)
  method_elem (:seq
               (:field :name _field_identifier)
               (:field :parameters parameter_list)
               (:field :result (:choice (:choice parameter_list _simple_type) :blank)))
  type_elem (:seq _type (:repeat (:seq "|" _type)))
  map_type (:prec-right 0 (:seq "map" "[" (:field :key _type) "]" (:field :value _type)))
  channel_type (:prec-left 0
                (:choice
                 (:seq "chan" (:field :value _type))
                 (:seq "chan" "<-" (:field :value _type))
                 (:prec 6 (:seq "<-" "chan" (:field :value _type)))))
  function_type (:prec-right 0
                 (:seq
                  "func"
                  (:field :parameters parameter_list)
                  (:field :result (:choice (:choice parameter_list _simple_type) :blank))))
  block (:seq "{" (:choice statement_list :blank) "}")
  statement_list (:choice
                  (:seq
                   _statement
                   (:repeat (:seq (:choice (:pattern "\\n") ";" "\0") _statement))
                   (:choice
                    (:seq
                     (:choice (:pattern "\\n") ";" "\0")
                     (:choice (:alias empty_labeled_statement labeled_statement) :blank))
                    :blank))
                  (:alias empty_labeled_statement labeled_statement))
  _statement (:choice
              _declaration
              _simple_statement
              return_statement
              go_statement
              defer_statement
              if_statement
              for_statement
              expression_switch_statement
              type_switch_statement
              select_statement
              labeled_statement
              fallthrough_statement
              break_statement
              continue_statement
              goto_statement
              block
              empty_statement)
  empty_statement ";"
  _simple_statement (:choice
                     expression_statement
                     send_statement
                     inc_statement
                     dec_statement
                     assignment_statement
                     short_var_declaration)
  expression_statement _expression
  send_statement (:seq (:field :channel _expression) "<-" (:field :value _expression))
  receive_statement (:seq
                     (:choice (:seq (:field :left expression_list) (:choice "=" ":=")) :blank)
                     (:field :right _expression))
  inc_statement (:seq _expression "++")
  dec_statement (:seq _expression "--")
  assignment_statement (:seq
                        (:field :left expression_list)
                        (:field :operator
                         (:choice "*=" "/=" "%=" "<<=" ">>=" "&=" "&^=" "+=" "-=" "|=" "^=" "="))
                        (:field :right expression_list))
  short_var_declaration (:seq (:field :left expression_list) ":=" (:field :right expression_list))
  labeled_statement (:seq (:field :label (:alias identifier label_name)) ":" _statement)
  empty_labeled_statement (:seq (:field :label (:alias identifier label_name)) ":")
  fallthrough_statement (:prec-left 0 "fallthrough")
  break_statement (:seq "break" (:choice (:alias identifier label_name) :blank))
  continue_statement (:seq "continue" (:choice (:alias identifier label_name) :blank))
  goto_statement (:seq "goto" (:alias identifier label_name))
  return_statement (:seq "return" (:choice expression_list :blank))
  go_statement (:seq "go" _expression)
  defer_statement (:seq "defer" _expression)
  if_statement (:seq
                "if"
                (:choice (:seq (:field :initializer _simple_statement) ";") :blank)
                (:field :condition _expression)
                (:field :consequence block)
                (:choice (:seq "else" (:field :alternative (:choice block if_statement))) :blank))
  for_statement (:seq
                 "for"
                 (:choice (:choice _expression for_clause range_clause) :blank)
                 (:field :body block))
  for_clause (:seq
              (:field :initializer (:choice _simple_statement :blank))
              ";"
              (:field :condition (:choice _expression :blank))
              ";"
              (:field :update (:choice _simple_statement :blank)))
  range_clause (:seq
                (:choice (:seq (:field :left expression_list) (:choice "=" ":=")) :blank)
                "range"
                (:field :right _expression))
  expression_switch_statement (:seq
                               "switch"
                               (:choice (:seq (:field :initializer _simple_statement) ";") :blank)
                               (:field :value (:choice _expression :blank))
                               "{"
                               (:repeat (:choice expression_case default_case))
                               "}")
  expression_case (:seq "case" (:field :value expression_list) ":" (:choice statement_list :blank))
  default_case (:seq "default" ":" (:choice statement_list :blank))
  type_switch_statement (:seq
                         "switch"
                         _type_switch_header
                         "{"
                         (:repeat (:choice type_case default_case))
                         "}")
  _type_switch_header (:seq
                       (:choice (:seq (:field :initializer _simple_statement) ";") :blank)
                       (:choice (:seq (:field :alias expression_list) ":=") :blank)
                       (:field :value _expression)
                       "."
                       "("
                       "type"
                       ")")
  type_case (:seq
             "case"
             (:field :type (:seq _type (:repeat (:seq "," _type))))
             ":"
             (:choice statement_list :blank))
  select_statement (:seq "select" "{" (:repeat (:choice communication_case default_case)) "}")
  communication_case (:seq
                      "case"
                      (:field :communication (:choice send_statement receive_statement))
                      ":"
                      (:choice statement_list :blank))
  _expression (:choice
               unary_expression
               binary_expression
               selector_expression
               index_expression
               slice_expression
               call_expression
               type_assertion_expression
               type_conversion_expression
               type_instantiation_expression
               identifier
               (:alias (:choice "new" "make") identifier)
               composite_literal
               func_literal
               _string_literal
               int_literal
               float_literal
               imaginary_literal
               rune_literal
               (:ref "nil")
               (:ref "true")
               (:ref "false")
               iota
               parenthesized_expression)
  parenthesized_expression (:seq "(" _expression ")")
  call_expression (:prec 7
                   (:choice
                    (:seq
                     (:field :function (:alias (:choice "new" "make") identifier))
                     (:field :arguments (:alias special_argument_list argument_list)))
                    (:seq
                     (:field :function _expression)
                     (:field :type_arguments (:choice type_arguments :blank))
                     (:field :arguments argument_list))))
  variadic_argument (:prec-right 0 (:seq _expression "..."))
  special_argument_list (:seq
                         "("
                         (:choice
                          (:seq _type (:repeat (:seq "," _expression)) (:choice "," :blank))
                          :blank)
                         ")")
  argument_list (:seq
                 "("
                 (:choice
                  (:seq
                   (:choice _expression variadic_argument)
                   (:repeat (:seq "," (:choice _expression variadic_argument)))
                   (:choice "," :blank))
                  :blank)
                 ")")
  selector_expression (:prec 7
                       (:seq (:field :operand _expression) "." (:field :field _field_identifier)))
  index_expression (:prec 7
                    (:prec-dynamic 1
                     (:seq (:field :operand _expression) "[" (:field :index _expression) "]")))
  slice_expression (:prec 7
                    (:seq
                     (:field :operand _expression)
                     "["
                     (:choice
                      (:seq
                       (:field :start (:choice _expression :blank))
                       ":"
                       (:field :end (:choice _expression :blank)))
                      (:seq
                       (:field :start (:choice _expression :blank))
                       ":"
                       (:field :end _expression)
                       ":"
                       (:field :capacity _expression)))
                     "]"))
  type_assertion_expression (:prec 7
                             (:seq (:field :operand _expression) "." "(" (:field :type _type) ")"))
  type_conversion_expression (:prec-dynamic -1
                              (:seq
                               (:field :type _type)
                               "("
                               (:field :operand _expression)
                               (:choice "," :blank)
                               ")"))
  type_instantiation_expression (:prec-dynamic -1
                                 (:seq
                                  (:field :type _type)
                                  "["
                                  (:seq _type (:repeat (:seq "," _type)))
                                  (:choice "," :blank)
                                  "]"))
  composite_literal (:prec -1
                     (:seq
                      (:field :type
                       (:choice
                        map_type
                        slice_type
                        array_type
                        implicit_length_array_type
                        struct_type
                        _type_identifier
                        generic_type
                        qualified_type))
                      (:field :body literal_value)))
  literal_value (:seq
                 "{"
                 (:choice
                  (:seq
                   (:choice
                    (:seq
                     (:choice literal_element keyed_element)
                     (:repeat (:seq "," (:choice literal_element keyed_element))))
                    :blank)
                   (:choice "," :blank))
                  :blank)
                 "}")
  literal_element (:choice _expression literal_value)
  keyed_element (:seq (:field :key literal_element) ":" (:field :value literal_element))
  func_literal (:seq
                "func"
                (:field :parameters parameter_list)
                (:field :result (:choice (:choice parameter_list _simple_type) :blank))
                (:field :body block))
  unary_expression (:prec 6
                    (:seq
                     (:field :operator (:choice "+" "-" "!" "^" "*" "&" "<-"))
                     (:field :operand _expression)))
  binary_expression (:choice
                     (:prec-left 5
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "*" "/" "%" "<<" ">>" "&" "&^"))
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "+" "-" "|" "^"))
                       (:field :right _expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "==" "!=" "<" "<=" ">" ">="))
                       (:field :right _expression)))
                     (:prec-left 2
                      (:seq
                       (:field :left _expression)
                       (:field :operator "&&")
                       (:field :right _expression)))
                     (:prec-left 1
                      (:seq
                       (:field :left _expression)
                       (:field :operator "||")
                       (:field :right _expression))))
  qualified_type (:seq (:field :package _package_identifier) "." (:field :name _type_identifier))
  identifier (:pattern "[_\\p{XID_Start}][_\\p{XID_Continue}]*")
  _type_identifier (:alias identifier type_identifier)
  _field_identifier (:alias identifier field_identifier)
  _package_identifier (:alias identifier package_identifier)
  _string_literal (:choice raw_string_literal interpreted_string_literal)
  raw_string_literal (:seq
                      "`"
                      (:alias (:token (:prec 1 (:pattern "[^`]*"))) raw_string_literal_content)
                      "`")
  interpreted_string_literal (:seq
                              "\""
                              (:repeat
                               (:choice
                                (:alias
                                 (:token-immediate (:prec 1 (:pattern "[^\"\\n\\\\]+")))
                                 interpreted_string_literal_content)
                                escape_sequence))
                              (:token-immediate "\""))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xuU]")
                     (:pattern "\\d{2,3}")
                     (:pattern "x[0-9a-fA-F]{2,}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "U[0-9a-fA-F]{8}"))))
  int_literal (:token
               (:choice
                (:seq
                 "0"
                 (:choice "b" "B")
                 (:choice "_" :blank)
                 (:seq (:pattern "[01]") (:repeat (:seq (:choice "_" :blank) (:pattern "[01]")))))
                (:choice
                 "0"
                 (:seq
                  (:pattern "[1-9]")
                  (:choice
                   (:seq
                    (:choice "_" :blank)
                    (:seq
                     (:pattern "[0-9]")
                     (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]")))))
                   :blank)))
                (:seq
                 "0"
                 (:choice (:choice "o" "O") :blank)
                 (:choice "_" :blank)
                 (:seq (:pattern "[0-7]") (:repeat (:seq (:choice "_" :blank) (:pattern "[0-7]")))))
                (:seq
                 "0"
                 (:choice "x" "X")
                 (:choice "_" :blank)
                 (:seq
                  (:pattern "[0-9a-fA-F]")
                  (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]")))))))
  float_literal (:token
                 (:choice
                  (:choice
                   (:seq
                    (:seq
                     (:pattern "[0-9]")
                     (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                    "."
                    (:choice
                     (:seq
                      (:pattern "[0-9]")
                      (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                     :blank)
                    (:choice
                     (:seq
                      (:choice "e" "E")
                      (:choice (:choice "+" "-") :blank)
                      (:seq
                       (:pattern "[0-9]")
                       (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]")))))
                     :blank))
                   (:seq
                    (:seq
                     (:pattern "[0-9]")
                     (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                    (:seq
                     (:choice "e" "E")
                     (:choice (:choice "+" "-") :blank)
                     (:seq
                      (:pattern "[0-9]")
                      (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))))
                   (:seq
                    "."
                    (:seq
                     (:pattern "[0-9]")
                     (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                    (:choice
                     (:seq
                      (:choice "e" "E")
                      (:choice (:choice "+" "-") :blank)
                      (:seq
                       (:pattern "[0-9]")
                       (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]")))))
                     :blank)))
                  (:seq
                   "0"
                   (:choice "x" "X")
                   (:choice
                    (:seq
                     (:choice "_" :blank)
                     (:seq
                      (:pattern "[0-9a-fA-F]")
                      (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))
                     "."
                     (:choice
                      (:seq
                       (:pattern "[0-9a-fA-F]")
                       (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))
                      :blank))
                    (:seq
                     (:choice "_" :blank)
                     (:seq
                      (:pattern "[0-9a-fA-F]")
                      (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]")))))
                    (:seq
                     "."
                     (:seq
                      (:pattern "[0-9a-fA-F]")
                      (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))))
                   (:seq
                    (:choice "p" "P")
                    (:choice (:choice "+" "-") :blank)
                    (:seq
                     (:pattern "[0-9]")
                     (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))))))
  imaginary_literal (:token
                     (:seq
                      (:choice
                       (:seq
                        (:pattern "[0-9]")
                        (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                       (:choice
                        (:seq
                         "0"
                         (:choice "b" "B")
                         (:choice "_" :blank)
                         (:seq
                          (:pattern "[01]")
                          (:repeat (:seq (:choice "_" :blank) (:pattern "[01]")))))
                        (:choice
                         "0"
                         (:seq
                          (:pattern "[1-9]")
                          (:choice
                           (:seq
                            (:choice "_" :blank)
                            (:seq
                             (:pattern "[0-9]")
                             (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]")))))
                           :blank)))
                        (:seq
                         "0"
                         (:choice (:choice "o" "O") :blank)
                         (:choice "_" :blank)
                         (:seq
                          (:pattern "[0-7]")
                          (:repeat (:seq (:choice "_" :blank) (:pattern "[0-7]")))))
                        (:seq
                         "0"
                         (:choice "x" "X")
                         (:choice "_" :blank)
                         (:seq
                          (:pattern "[0-9a-fA-F]")
                          (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))))
                       (:choice
                        (:choice
                         (:seq
                          (:seq
                           (:pattern "[0-9]")
                           (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                          "."
                          (:choice
                           (:seq
                            (:pattern "[0-9]")
                            (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                           :blank)
                          (:choice
                           (:seq
                            (:choice "e" "E")
                            (:choice (:choice "+" "-") :blank)
                            (:seq
                             (:pattern "[0-9]")
                             (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]")))))
                           :blank))
                         (:seq
                          (:seq
                           (:pattern "[0-9]")
                           (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                          (:seq
                           (:choice "e" "E")
                           (:choice (:choice "+" "-") :blank)
                           (:seq
                            (:pattern "[0-9]")
                            (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))))
                         (:seq
                          "."
                          (:seq
                           (:pattern "[0-9]")
                           (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))
                          (:choice
                           (:seq
                            (:choice "e" "E")
                            (:choice (:choice "+" "-") :blank)
                            (:seq
                             (:pattern "[0-9]")
                             (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]")))))
                           :blank)))
                        (:seq
                         "0"
                         (:choice "x" "X")
                         (:choice
                          (:seq
                           (:choice "_" :blank)
                           (:seq
                            (:pattern "[0-9a-fA-F]")
                            (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))
                           "."
                           (:choice
                            (:seq
                             (:pattern "[0-9a-fA-F]")
                             (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))
                            :blank))
                          (:seq
                           (:choice "_" :blank)
                           (:seq
                            (:pattern "[0-9a-fA-F]")
                            (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]")))))
                          (:seq
                           "."
                           (:seq
                            (:pattern "[0-9a-fA-F]")
                            (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))))
                         (:seq
                          (:choice "p" "P")
                          (:choice (:choice "+" "-") :blank)
                          (:seq
                           (:pattern "[0-9]")
                           (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9]"))))))))
                      "i"))
  rune_literal (:token
                (:seq
                 "'"
                 (:choice
                  (:pattern "[^'\\\\]")
                  (:seq
                   "\\"
                   (:choice
                    (:seq "x" (:pattern "[0-9a-fA-F]") (:pattern "[0-9a-fA-F]"))
                    (:seq (:pattern "[0-7]") (:pattern "[0-7]") (:pattern "[0-7]"))
                    (:seq
                     "u"
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]"))
                    (:seq
                     "U"
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]")
                     (:pattern "[0-9a-fA-F]"))
                    (:seq (:choice "a" "b" "f" "n" "r" "t" "v" "\\" "'" "\"")))))
                 "'"))
  (:ref "nil") "nil"
  (:ref "true") "true"
  (:ref "false") "false"
  iota "iota"
  comment (:token
           (:choice
            (:seq "//" (:pattern ".*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))}}
