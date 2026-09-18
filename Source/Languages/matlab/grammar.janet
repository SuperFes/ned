# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "matlab"
 :extras [(:pattern "\\s") comment line_continuation]
 :conflicts [[_expression _range_element]
             [field_expression]
             [range]
             [block]
             [_index_range]
             [_index_arguments]
             [function_call _function_call_with_keywords]
             [block _functionless_block]
             [function_definition _function_definition_with_end]
             [_function_definition_with_end]]
 :precedences []
 :externals [comment
             line_continuation
             command_name
             command_argument
             _single_quote_string_start
             _single_quote_string_end
             _double_quote_string_start
             _double_quote_string_end
             formatting_sequence
             escape_sequence
             string_content
             _entry_delimiter
             _multioutput_variable_start
             _external_identifier
             _catch_identifier
             _transpose
             _ctranspose
             error_sentinel]
 :inline []
 :supertypes []
 :rules
 {source_file (:seq
               (:repeat _end_of_line)
               (:choice
                (:choice (:seq _block (:repeat function_definition)) :blank)
                (:repeat1 function_definition)))
  _block (:repeat1
          (:seq
           (:choice
            _statement
            _expression
            (:alias _function_definition_with_end function_definition))
           (:repeat1 _end_of_line)))
  _functionless_block (:prec 1
                       (:repeat1 (:seq (:choice _statement _expression) (:repeat1 _end_of_line))))
  block _block
  identifier (:choice _external_identifier _keywords)
  _statement (:choice
              assignment
              break_statement
              class_definition
              command
              continue_statement
              for_statement
              global_operator
              if_statement
              persistent_operator
              return_statement
              spmd_statement
              switch_statement
              try_statement
              while_statement)
  _expression (:choice
               binary_operator
               boolean_operator
               cell
               comparison_operator
               function_call
               handle_operator
               identifier
               lambda
               matrix
               metaclass_operator
               not_operator
               number
               parenthesis
               postfix_operator
               range
               string
               unary_operator
               field_expression)
  parenthesis (:prec -1 (:seq "(" _expression ")"))
  _binary_expression (:prec 1
                      (:choice
                       binary_operator
                       boolean_operator
                       cell
                       comparison_operator
                       function_call
                       identifier
                       matrix
                       not_operator
                       number
                       parenthesis
                       postfix_operator
                       string
                       field_expression
                       unary_operator))
  binary_operator (:choice
                   (:prec-left 18
                    (:seq (:field :left _binary_expression) "+" (:field :right _binary_expression)))
                   (:prec-left 18
                    (:seq (:field :left _binary_expression) ".+" (:field :right _binary_expression)))
                   (:prec-left 18
                    (:seq (:field :left _binary_expression) "-" (:field :right _binary_expression)))
                   (:prec-left 18
                    (:seq (:field :left _binary_expression) ".-" (:field :right _binary_expression)))
                   (:prec-left 19
                    (:seq (:field :left _binary_expression) "*" (:field :right _binary_expression)))
                   (:prec-left 19
                    (:seq (:field :left _binary_expression) ".*" (:field :right _binary_expression)))
                   (:prec-left 19
                    (:seq (:field :left _binary_expression) "/" (:field :right _binary_expression)))
                   (:prec-left 19
                    (:seq (:field :left _binary_expression) "./" (:field :right _binary_expression)))
                   (:prec-left 19
                    (:seq (:field :left _binary_expression) "\\" (:field :right _binary_expression)))
                   (:prec-left 19
                    (:seq
                     (:field :left _binary_expression)
                     ".\\"
                     (:field :right _binary_expression)))
                   (:prec-right 22
                    (:seq (:field :left _binary_expression) "^" (:field :right _binary_expression)))
                   (:prec-right 22
                    (:seq (:field :left _binary_expression) ".^" (:field :right _binary_expression)))
                   (:prec-left 14
                    (:seq (:field :left _binary_expression) "|" (:field :right _binary_expression)))
                   (:prec-left 15
                    (:seq (:field :left _binary_expression) "&" (:field :right _binary_expression))))
  unary_operator (:prec 20
                  (:seq
                   (:choice "+" "-")
                   (:field :operand
                    (:choice
                     cell
                     field_expression
                     function_call
                     identifier
                     matrix
                     not_operator
                     number
                     parenthesis
                     postfix_operator
                     string
                     unary_operator))))
  indirect_access (:prec -2
                   (:seq
                    "("
                    (:choice
                     number
                     binary_operator
                     field_expression
                     function_call
                     identifier
                     matrix
                     parenthesis
                     postfix_operator
                     string
                     unary_operator
                     not_operator)
                    ")"))
  field_expression (:prec-dynamic 24
                    (:seq
                     (:field :object (:choice identifier function_call))
                     (:repeat1
                      (:seq
                       "."
                       (:field :field
                        (:choice
                         identifier
                         (:alias _extended_keywords identifier)
                         (:alias _function_call_with_keywords function_call)
                         indirect_access))))))
  not_operator (:prec 12
                (:seq
                 "~"
                 (:choice
                  cell
                  function_call
                  handle_operator
                  identifier
                  matrix
                  metaclass_operator
                  not_operator
                  number
                  parenthesis
                  postfix_operator
                  range
                  string
                  unary_operator
                  field_expression)))
  metaclass_operator (:prec-left 0 (:seq "?" (:seq identifier (:repeat (:seq "." identifier)))))
  handle_operator (:seq "@" (:seq identifier (:repeat (:seq "." identifier))))
  comparison_operator (:prec-left 13
                       (:seq _expression (:choice "<" "<=" "==" "~=" ">=" ">") _expression))
  boolean_operator (:choice
                    (:prec-left 11
                     (:seq (:field :left _expression) "&&" (:field :right _expression)))
                    (:prec-left 10
                     (:seq (:field :left _expression) "||" (:field :right _expression))))
  postfix_operator (:prec 21
                    (:seq
                     (:field :operand
                      (:choice
                       binary_operator
                       cell
                       function_call
                       identifier
                       matrix
                       number
                       parenthesis
                       postfix_operator
                       string
                       field_expression
                       unary_operator))
                     (:choice (:alias _transpose "'") (:alias _ctranspose ".'"))))
  string (:choice
          (:seq
           (:alias _double_quote_string_start "\"")
           (:repeat (:choice string_content escape_sequence formatting_sequence))
           (:alias _double_quote_string_end "\""))
          (:seq
           (:alias _single_quote_string_start "'")
           (:repeat (:choice string_content escape_sequence formatting_sequence))
           (:alias _single_quote_string_end "'")))
  row (:seq
       (:choice "," :blank)
       (:choice _expression ignored_argument)
       (:repeat (:seq (:alias _entry_delimiter ",") (:choice _expression ignored_argument)))
       (:choice (:alias _entry_delimiter ",") :blank))
  matrix (:seq
          "["
          (:repeat (:choice (:pattern "[,]*;[ ,;]*") (:pattern "[\\r\\n]")))
          (:choice
           (:seq
            row
            (:repeat
             (:seq (:choice (:pattern "[,]*;[ ,;]*") (:pattern "[\\r\\n]")) (:choice row :blank))))
           :blank)
          "]")
  cell (:seq
        "{"
        (:repeat (:choice (:pattern "[,]*;[ ,;]*") (:pattern "[\\r\\n]")))
        (:choice
         (:seq
          row
          (:repeat
           (:seq (:choice (:pattern "[,]*;[ ,;]*") (:pattern "[\\r\\n]")) (:choice row :blank))))
         :blank)
        "}")
  ignored_argument (:prec 13 "~")
  assignment (:seq
              (:field :left
               (:choice
                identifier
                field_expression
                ignored_argument
                function_call
                multioutput_variable))
              "="
              (:field :right _expression))
  multioutput_variable (:seq
                        (:alias _multioutput_variable_start "[")
                        (:repeat "\n")
                        (:choice
                         (:seq
                          (:choice
                           identifier
                           field_expression
                           ignored_argument
                           function_call
                           ignored_argument)
                          (:repeat
                           (:seq
                            (:choice "," :blank)
                            (:choice
                             identifier
                             field_expression
                             ignored_argument
                             function_call
                             ignored_argument)))
                          (:choice "," :blank))
                         :blank)
                        "]")
  spread_operator ":"
  _index_boolean_operator (:choice
                           (:prec-left 12
                            (:seq
                             (:field :left (:alias _index_expression _expression))
                             "&&"
                             (:field :right (:alias _index_expression _expression))))
                           (:prec-left 11
                            (:seq
                             (:field :left (:alias _index_expression _expression))
                             "||"
                             (:field :right (:alias _index_expression _expression)))))
  _index_comparison_operator (:prec-left 14
                              (:seq
                               _index_expression
                               (:choice "<" "<=" "==" "~=" ">=" ">")
                               _index_expression))
  _index_not_operator (:prec 13 (:seq "~" (:alias _index_expression _expression)))
  _index_unary_operator (:prec 21
                         (:seq
                          (:choice "+" "-")
                          (:field :operand
                           (:choice
                            cell
                            field_expression
                            function_call
                            identifier
                            (:alias _index_matrix matrix)
                            (:alias _index_not_operator not_operator)
                            number
                            (:alias _index_parenthesis parenthesis)
                            (:alias _index_postfix_operator postfix_operator)
                            string
                            (:alias _index_unary_operator unary_operator)))))
  _index_postfix_operator (:prec 22
                           (:seq
                            (:field :operand
                             (:choice
                              (:alias _index_binary_operator binary_operator)
                              cell
                              function_call
                              identifier
                              (:alias _index_matrix matrix)
                              number
                              (:alias _index_parenthesis parenthesis)
                              (:alias _index_postfix_operator postfix_operator)
                              string
                              field_expression
                              (:alias _index_unary_operator unary_operator)))
                            (:choice (:alias _transpose "'") (:alias _ctranspose ".'"))))
  _index_row (:seq
              (:choice "," :blank)
              (:choice _index_expression ignored_argument)
              (:repeat
               (:seq (:alias _entry_delimiter ",") (:choice _index_expression ignored_argument)))
              (:choice (:alias _entry_delimiter ",") :blank))
  _index_matrix (:seq
                 "["
                 (:repeat "\n")
                 (:choice
                  (:seq
                   (:alias _index_row row)
                   (:repeat
                    (:seq
                     (:choice ";" (:pattern "[\\r\\n]"))
                     (:choice (:alias _index_row row) :blank))))
                  :blank)
                 "]")
  _index_range_element (:prec 1
                        (:choice
                         field_expression
                         function_call
                         identifier
                         (:alias _index_matrix matrix)
                         (:alias _index_not_operator not_operator)
                         number
                         (:alias _index_parenthesis parenthesis)
                         (:alias _index_postfix_operator postfix_operator)
                         string
                         (:prec-dynamic -1 (:alias _index_unary_operator unary_operator))
                         (:prec-dynamic 1 (:alias _index_binary_operator binary_operator))
                         end_keyword))
  _index_range (:prec-right 21
                (:seq
                 _index_range_element
                 ":"
                 _index_range_element
                 (:choice (:seq ":" _index_range_element) :blank)))
  _index_parenthesis (:prec -1 (:seq "(" _index_expression ")"))
  _index_expression (:choice
                     (:alias _index_boolean_operator boolean_operator)
                     field_expression
                     function_call
                     handle_operator
                     identifier
                     cell
                     lambda
                     (:alias _index_matrix matrix)
                     metaclass_operator
                     (:alias _index_not_operator not_operator)
                     number
                     (:alias _index_comparison_operator comparison_operator)
                     (:alias _index_parenthesis parenthesis)
                     (:alias _index_range range)
                     (:alias _index_binary_operator binary_operator)
                     (:alias _index_postfix_operator postfix_operator)
                     string
                     (:alias _index_unary_operator unary_operator)
                     end_keyword)
  _index_binary_expression (:prec 2
                            (:choice
                             (:alias _index_binary_operator binary_operator)
                             (:alias _index_boolean_operator boolean_operator)
                             cell
                             (:alias _index_comparison_operator comparison_operator)
                             function_call
                             identifier
                             (:alias _index_matrix matrix)
                             (:alias _index_not_operator not_operator)
                             number
                             (:alias _index_parenthesis parenthesis)
                             (:alias _index_postfix_operator postfix_operator)
                             string
                             field_expression
                             (:alias _index_unary_operator unary_operator)
                             end_keyword))
  _index_binary_operator (:choice
                          (:prec-left 18
                           (:seq
                            (:field :left _index_binary_expression)
                            "+"
                            (:field :right _index_binary_expression)))
                          (:prec-left 18
                           (:seq
                            (:field :left _index_binary_expression)
                            ".+"
                            (:field :right _index_binary_expression)))
                          (:prec-left 18
                           (:seq
                            (:field :left _index_binary_expression)
                            "-"
                            (:field :right _index_binary_expression)))
                          (:prec-left 18
                           (:seq
                            (:field :left _index_binary_expression)
                            ".-"
                            (:field :right _index_binary_expression)))
                          (:prec-left 19
                           (:seq
                            (:field :left _index_binary_expression)
                            "*"
                            (:field :right _index_binary_expression)))
                          (:prec-left 19
                           (:seq
                            (:field :left _index_binary_expression)
                            ".*"
                            (:field :right _index_binary_expression)))
                          (:prec-left 19
                           (:seq
                            (:field :left _index_binary_expression)
                            "/"
                            (:field :right _index_binary_expression)))
                          (:prec-left 19
                           (:seq
                            (:field :left _index_binary_expression)
                            "./"
                            (:field :right _index_binary_expression)))
                          (:prec-left 19
                           (:seq
                            (:field :left _index_binary_expression)
                            "\\"
                            (:field :right _index_binary_expression)))
                          (:prec-left 19
                           (:seq
                            (:field :left _index_binary_expression)
                            ".\\"
                            (:field :right _index_binary_expression)))
                          (:prec-right 22
                           (:seq
                            (:field :left _index_binary_expression)
                            "^"
                            (:field :right _index_binary_expression)))
                          (:prec-right 22
                           (:seq
                            (:field :left _index_binary_expression)
                            ".^"
                            (:field :right _index_binary_expression)))
                          (:prec-left 14
                           (:seq
                            (:field :left _index_binary_expression)
                            "|"
                            (:field :right _index_binary_expression)))
                          (:prec-left 15
                           (:seq
                            (:field :left _index_binary_expression)
                            "&"
                            (:field :right _index_binary_expression))))
  _index_argument (:choice spread_operator _index_expression)
  _index_arguments (:seq
                    (:field :argument _index_argument)
                    (:repeat (:seq "," (:field :argument _index_argument))))
  arguments (:choice
             (:seq
              _index_arguments
              (:choice
               (:seq
                ","
                (:seq
                 (:seq identifier "=" _expression)
                 (:repeat (:seq "," (:seq identifier "=" _expression)))))
               :blank))
             (:seq
              (:seq identifier "=" _expression)
              (:repeat (:seq "," (:seq identifier "=" _expression)))))
  _args (:choice
         (:seq "(" (:choice arguments :blank) ")")
         (:seq "{" (:choice arguments :blank) "}"))
  function_call (:choice
                 (:prec-right 23
                  (:seq
                   (:field :name
                    (:choice
                     (:alias end_keyword identifier)
                     identifier
                     function_call
                     field_expression))
                   (:choice (:seq "@" (:alias property_name superclass)) :blank)
                   _args))
                 (:prec-right 23
                  (:seq
                   (:field :name (:choice identifier function_call field_expression))
                   (:seq "@" (:alias property_name superclass))
                   (:choice _args :blank))))
  _function_call_with_keywords (:choice
                                (:prec-right 23
                                 (:seq
                                  (:field :name
                                   (:choice
                                    (:alias end_keyword identifier)
                                    (:alias _extended_keywords identifier)
                                    identifier
                                    function_call
                                    field_expression))
                                  (:choice (:seq "@" (:alias property_name superclass)) :blank)
                                  _args))
                                (:prec-right 23
                                 (:seq
                                  (:field :name (:choice identifier function_call field_expression))
                                  (:seq "@" (:alias property_name superclass))
                                  (:choice _args :blank))))
  command (:prec-right 0 (:seq command_name (:repeat command_argument)))
  _range_element (:choice
                  field_expression
                  function_call
                  identifier
                  matrix
                  not_operator
                  number
                  parenthesis
                  postfix_operator
                  string
                  (:prec-dynamic -1 unary_operator)
                  (:prec-dynamic 1 binary_operator))
  range (:prec-right 21
         (:seq _range_element ":" _range_element (:choice (:seq ":" _range_element) :blank)))
  return_statement "return"
  continue_statement "continue"
  break_statement "break"
  elseif_clause (:prec-left 0
                 (:seq
                  "elseif"
                  (:field :condition _expression)
                  (:repeat _end_of_line)
                  (:choice block :blank)))
  else_clause (:prec-left 0 (:seq "else" (:repeat _end_of_line) (:choice block :blank)))
  if_statement (:seq
                "if"
                (:field :condition _expression)
                (:repeat _end_of_line)
                (:choice block :blank)
                (:repeat elseif_clause)
                (:choice else_clause :blank)
                "end")
  iterator (:seq identifier "=" _expression)
  parfor_options (:choice number identifier field_expression function_call string)
  for_statement (:choice
                 (:seq
                  (:choice "for" "parfor")
                  (:choice iterator (:seq "(" iterator ")"))
                  (:repeat _end_of_line)
                  (:choice block :blank)
                  "end")
                 (:seq
                  "parfor"
                  "("
                  iterator
                  ","
                  parfor_options
                  ")"
                  (:repeat _end_of_line)
                  (:choice block :blank)
                  "end"))
  while_statement (:seq
                   "while"
                   (:field :condition _expression)
                   (:repeat _end_of_line)
                   (:choice block :blank)
                   "end")
  case_clause (:prec-left 0
               (:seq
                "case"
                (:field :condition _expression)
                (:repeat1 _end_of_line)
                (:choice block :blank)))
  otherwise_clause (:prec-left 0 (:seq "otherwise" (:repeat _end_of_line) (:choice block :blank)))
  switch_statement (:seq
                    "switch"
                    (:field :condition _expression)
                    (:repeat _end_of_line)
                    (:repeat case_clause)
                    (:choice otherwise_clause :blank)
                    "end")
  _lambda_arguments (:seq
                     (:choice ignored_argument identifier)
                     (:repeat (:seq "," (:choice ignored_argument identifier))))
  lambda (:prec-right 0
          (:seq
           "@"
           "("
           (:alias (:choice _lambda_arguments :blank) arguments)
           ")"
           (:field :expression _expression)))
  global_operator (:seq "global" (:repeat identifier))
  persistent_operator (:seq "persistent" (:repeat identifier))
  _argument_attributes (:seq
                        "("
                        (:field :argument identifier)
                        (:repeat (:seq "," (:field :argument identifier)))
                        ")")
  class_property (:seq identifier ".?" identifier (:repeat (:seq "." identifier)))
  arguments_statement (:prec-right 1
                       (:seq
                        "arguments"
                        (:choice (:alias _argument_attributes attributes) :blank)
                        (:repeat1 _end_of_line)
                        (:repeat (:seq (:choice property class_property) (:repeat1 _end_of_line)))
                        "end"
                        (:choice _end_of_line :blank)))
  function_output (:seq (:choice identifier multioutput_variable) "=")
  function_arguments (:seq "(" (:field :arguments (:choice _lambda_arguments :blank)) ")")
  function_definition (:prec-dynamic 0
                       (:seq
                        "function"
                        (:choice function_output :blank)
                        (:choice (:choice "get." "set.") :blank)
                        (:field :name
                         (:choice identifier property_name (:alias end_keyword identifier)))
                        (:choice function_arguments :blank)
                        _end_of_line
                        (:repeat
                         (:seq (:repeat (:choice comment _end_of_line)) arguments_statement))
                        (:repeat (:choice comment _end_of_line))
                        (:choice (:alias _functionless_block block) :blank)
                        (:choice (:seq (:choice "end" "endfunction") (:choice ";" :blank)) :blank)))
  _function_definition_with_end (:prec-dynamic 1
                                 (:seq
                                  "function"
                                  (:choice function_output :blank)
                                  (:choice (:choice "get." "set.") :blank)
                                  (:field :name
                                   (:choice
                                    identifier
                                    property_name
                                    (:alias end_keyword identifier)))
                                  (:choice function_arguments :blank)
                                  (:choice
                                   (:seq
                                    _end_of_line
                                    (:repeat
                                     (:seq
                                      (:repeat (:choice comment _end_of_line))
                                      arguments_statement))
                                    (:repeat (:choice comment _end_of_line))
                                    (:choice block :blank))
                                   :blank)
                                  (:choice "end" "endfunction")
                                  (:choice ";" :blank)))
  _negated_attribute (:seq "~" identifier)
  attribute (:choice
             (:alias _negated_attribute not_operator)
             (:seq identifier (:choice (:seq "=" _expression) :blank)))
  attributes (:seq "(" attribute (:repeat (:seq "," attribute)) ")")
  superclasses (:seq "<" property_name (:repeat (:seq "&" property_name)))
  dimensions (:seq
              "("
              (:seq
               (:choice number spread_operator)
               (:repeat (:seq "," (:choice number spread_operator))))
              ")")
  validation_functions (:seq
                        "{"
                        (:choice function_call identifier field_expression)
                        (:repeat
                         (:seq
                          (:choice "," :blank)
                          (:choice function_call identifier field_expression)))
                        "}")
  default_value (:seq "=" _expression)
  property_name (:prec-right -1
                 (:seq identifier (:repeat (:seq "." identifier)) (:choice (:seq "." "*") :blank)))
  property (:prec-right 0
            (:choice
             (:seq
              (:field :name (:choice identifier property_name ignored_argument))
              (:choice dimensions :blank)
              (:choice (:choice identifier property_name) :blank)
              (:choice validation_functions :blank)
              (:choice default_value :blank))
             (:seq
              (:field :name (:choice identifier property_name ignored_argument))
              "@"
              identifier
              (:alias (:choice (:choice "vector" "matrix" "scalar") :blank) identifier)
              (:choice default_value :blank))))
  properties (:seq
              "properties"
              (:choice attributes :blank)
              _end_of_line
              (:repeat (:choice (:seq property _end_of_line) _end_of_line comment))
              "end")
  function_signature (:seq
                      (:choice function_output :blank)
                      (:choice (:choice "get." "set.") :blank)
                      (:field :name
                       (:alias (:choice _external_identifier "get" "set" "arguments") identifier))
                      (:choice function_arguments :blank))
  methods (:seq
           "methods"
           (:choice attributes :blank)
           (:repeat _end_of_line)
           (:repeat
            (:seq
             (:choice
              comment
              (:alias
               (:seq function_output (:field :name (:alias "end" identifier)) function_arguments)
               function_signature)
              function_signature
              (:alias _function_definition_with_end function_definition))
             (:repeat _end_of_line)))
           "end")
  events (:seq
          "events"
          (:choice attributes :blank)
          _end_of_line
          (:repeat (:choice (:seq identifier _end_of_line) _end_of_line comment))
          "end")
  enum (:seq
        identifier
        (:choice (:seq "(" (:seq _expression (:repeat (:seq "," _expression))) ")") :blank))
  enumeration (:seq
               "enumeration"
               (:choice attributes :blank)
               _end_of_line
               (:repeat (:choice (:seq enum _end_of_line) _end_of_line comment))
               "end")
  class_definition (:seq
                    "classdef"
                    (:choice attributes :blank)
                    (:field :name identifier)
                    (:choice superclasses :blank)
                    _end_of_line
                    (:repeat (:choice properties methods events enumeration _end_of_line comment))
                    "end")
  catch_clause (:prec-left 0
                (:seq
                 "catch"
                 (:choice (:seq (:alias _catch_identifier identifier) _end_of_line) :blank)
                 (:repeat _end_of_line)
                 (:choice block :blank)))
  try_statement (:seq
                 "try"
                 (:repeat _end_of_line)
                 (:choice block :blank)
                 (:choice catch_clause :blank)
                 "end")
  spmd_statement (:seq
                  "spmd"
                  (:choice
                   (:seq
                    "("
                    (:choice (:choice _expression (:seq _expression "," _expression)) :blank)
                    ")")
                   :blank)
                  (:repeat _end_of_line)
                  (:choice block :blank)
                  "end")
  number (:choice
          (:pattern "(\\d+|\\d+\\.\\d*|\\.\\d+)([eEdD][+-]?\\d+)?[ij]?")
          (:pattern "0[xX][\\dA-Fa-f]+([suSU](8|16|32|64))?")
          (:pattern "0[bB][01]+([suSU](8|16|32|64))?"))
  end_keyword "end"
  _keywords (:choice "get" "set" "properties" "arguments" "enumeration" "events" "methods")
  _extended_keywords (:prec 100
                      (:choice
                       "break"
                       "case"
                       "catch"
                       "classdef"
                       "continue"
                       "else"
                       "elseif"
                       "end"
                       "for"
                       "function"
                       "global"
                       "if"
                       "otherwise"
                       "parfor"
                       "persistent"
                       "return"
                       "spmd"
                       "switch"
                       "try"
                       "while"))
  _end_of_line (:choice ";" "\n" "\r" ",")}}
