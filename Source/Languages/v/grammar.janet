# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "v"
 :word identifier
 :extras [(:pattern "\\s") line_comment block_comment]
 :conflicts [[fixed_array_type _expression_without_blocks]
             [qualified_type _expression_without_blocks]
             [fixed_array_type literal]
             [reference_expression type_reference_expression]
             [is_expression]
             [_expression_without_blocks element_list]]
 :precedences []
 :externals []
 :inline [_string_literal _top_level_declaration _array]
 :supertypes [_expression _statement _top_level_declaration _expression_with_blocks]
 :rules
 {source_file (:seq
               (:choice shebang :blank)
               (:choice module_clause :blank)
               (:repeat
                (:choice
                 (:seq import_list (:choice (:choice "\n" "\r" "\r\n") :blank))
                 (:seq _top_level_declaration (:choice (:choice "\n" "\r" "\r\n") :blank))
                 (:seq _statement (:choice (:choice "\n" "\r" "\r\n") :blank)))))
  shebang (:seq "#!" (:pattern ".*"))
  line_comment (:seq "//" (:pattern ".*"))
  block_comment (:seq
                 "/*"
                 (:repeat (:choice (:pattern "\\*") (:pattern "[^*]|[/][^*]|[^*][/]")))
                 "*/")
  comment (:choice line_comment block_comment)
  module_clause (:seq (:choice attributes :blank) "module" identifier)
  import_list (:prec-right 0 (:repeat1 import_declaration))
  import_declaration (:seq "import" import_spec (:choice (:choice "\n" "\r" "\r\n") ";"))
  import_spec (:seq
               import_path
               (:choice import_alias :blank)
               (:choice selective_import_list :blank))
  import_path (:seq import_name (:repeat (:seq "." import_name)))
  import_name identifier
  import_alias (:seq "as" import_name)
  selective_import_list (:seq
                         "{"
                         reference_expression
                         (:repeat
                          (:seq
                           (:choice "," (:choice "\n" "\r" "\r\n"))
                           (:choice reference_expression :blank)))
                         "}")
  _top_level_declaration (:choice
                          const_declaration
                          global_var_declaration
                          type_declaration
                          function_declaration
                          static_method_declaration
                          struct_declaration
                          enum_declaration
                          interface_declaration)
  const_declaration (:seq
                     (:choice (:field :attributes attributes) :blank)
                     (:choice visibility_modifiers :blank)
                     "const"
                     (:choice
                      const_definition
                      (:seq
                       "("
                       (:repeat (:seq const_definition (:choice (:choice "\n" "\r" "\r\n") ";")))
                       ")")))
  const_definition (:seq (:field :name identifier) "=" (:field :value _expression))
  global_var_declaration (:seq
                          (:choice (:field :attributes attributes) :blank)
                          "__global"
                          (:choice
                           global_var_definition
                           (:seq
                            "("
                            (:repeat
                             (:seq global_var_definition (:choice (:choice "\n" "\r" "\r\n") ";")))
                            ")")))
  global_var_definition (:seq
                         (:choice (:field :modifiers "volatile") :blank)
                         (:field :name identifier)
                         (:choice plain_type _global_var_value))
  _global_var_value (:seq "=" (:field :value _expression))
  type_declaration (:prec-right 1
                    (:seq
                     (:choice visibility_modifiers :blank)
                     "type"
                     (:field :name identifier)
                     (:choice (:field :generic_parameters generic_parameters) :blank)
                     "="
                     (:field :type (:choice sum_type plain_type))))
  function_declaration (:prec-right 1
                        (:seq
                         (:choice (:field :attributes attributes) :blank)
                         (:choice visibility_modifiers :blank)
                         "fn"
                         (:choice (:field :receiver receiver) :blank)
                         (:field :name _function_name)
                         (:choice (:field :generic_parameters generic_parameters) :blank)
                         (:field :signature signature)
                         (:choice (:field :body block) :blank)))
  static_method_declaration (:prec-right 1
                             (:seq
                              (:choice (:field :attributes attributes) :blank)
                              (:choice visibility_modifiers :blank)
                              "fn"
                              (:field :static_receiver static_receiver)
                              "."
                              (:field :name _function_name)
                              (:choice (:field :generic_parameters generic_parameters) :blank)
                              (:field :signature signature)
                              (:choice (:field :body block) :blank)))
  static_receiver reference_expression
  _function_name (:choice identifier overridable_operator)
  overridable_operator (:choice
                        (:token "+")
                        (:token "-")
                        (:token "*")
                        (:token "/")
                        (:token "%")
                        (:token "<")
                        (:token ">")
                        (:token "==")
                        (:token "!=")
                        (:token "<=")
                        (:token ">="))
  receiver (:prec 7
            (:seq
             "("
             (:seq
              (:choice (:field :mutability mutability_modifiers) :blank)
              (:field :name identifier)
              (:field :type (:alias _plain_type_without_special plain_type)))
             ")"))
  signature (:prec-right 0
             (:seq
              (:field :parameters (:choice parameter_list type_parameter_list))
              (:choice (:field :result plain_type) :blank)))
  parameter_list (:prec 1
                  (:seq
                   "("
                   (:choice
                    (:choice
                     variadic_parameter
                     (:seq
                      (:seq parameter_declaration (:repeat (:seq "," parameter_declaration)))
                      (:choice (:seq "," variadic_parameter) :blank)))
                    :blank)
                   ")"))
  parameter_declaration (:seq
                         (:choice (:field :mutability mutability_modifiers) :blank)
                         (:field :name identifier)
                         (:choice (:field :variadic "...") :blank)
                         (:field :type plain_type))
  variadic_parameter "..."
  type_parameter_list (:seq
                       "("
                       (:seq
                        type_parameter_declaration
                        (:repeat (:seq "," type_parameter_declaration)))
                       ")")
  type_parameter_declaration (:prec 7
                              (:seq
                               (:choice mutability_modifiers :blank)
                               (:choice (:field :variadic "...") :blank)
                               (:field :type plain_type)))
  generic_parameters (:seq
                      (:token-immediate "[")
                      (:seq generic_parameter (:repeat (:seq "," generic_parameter)))
                      (:choice "," :blank)
                      "]")
  generic_parameter identifier
  struct_declaration (:seq
                      (:choice (:field :attributes attributes) :blank)
                      (:choice visibility_modifiers :blank)
                      (:choice "struct" "union")
                      (:field :name identifier)
                      (:choice (:field :generic_parameters generic_parameters) :blank)
                      (:choice (:seq "implements" (:field :implements implements_clause)) :blank)
                      _struct_body)
  implements_clause (:seq
                     (:choice type_reference_expression qualified_type)
                     (:repeat (:seq "," (:choice type_reference_expression qualified_type))))
  _struct_body (:seq
                "{"
                (:repeat
                 (:choice
                  (:seq struct_field_scope (:choice (:choice "\n" "\r" "\r\n") :blank))
                  (:seq struct_field_declaration (:choice (:choice "\n" "\r" "\r\n") :blank))))
                "}")
  struct_field_scope (:seq (:choice "pub" "mut" (:seq "pub" "mut") "__global") ":")
  struct_field_declaration (:choice _struct_field_definition embedded_definition)
  _struct_field_definition (:prec-right 8
                            (:seq
                             (:field :name identifier)
                             (:field :type plain_type)
                             (:choice (:seq "=" (:field :default_value _expression)) :blank)
                             (:choice (:field :attributes attribute) :blank)))
  embedded_definition (:choice type_reference_expression qualified_type generic_type)
  enum_declaration (:seq
                    (:choice (:field :attributes attributes) :blank)
                    (:choice visibility_modifiers :blank)
                    "enum"
                    (:field :name identifier)
                    (:choice enum_backed_type :blank)
                    _enum_body)
  enum_backed_type (:seq "as" plain_type)
  _enum_body (:seq
              "{"
              (:repeat (:seq enum_field_definition (:choice (:choice "\n" "\r" "\r\n") :blank)))
              "}")
  enum_field_definition (:seq
                         (:field :name identifier)
                         (:choice (:seq "=" (:field :value _expression)) :blank)
                         (:choice (:field :attributes attribute) :blank))
  interface_declaration (:seq
                         (:choice (:field :attributes attributes) :blank)
                         (:choice visibility_modifiers :blank)
                         "interface"
                         (:field :name identifier)
                         (:choice (:field :generic_parameters generic_parameters) :blank)
                         _interface_body)
  _interface_body (:seq
                   "{"
                   (:repeat
                    (:choice
                     (:seq struct_field_scope (:choice (:choice "\n" "\r" "\r\n") :blank))
                     (:seq struct_field_declaration (:choice (:choice "\n" "\r" "\r\n") :blank))
                     (:seq interface_method_definition (:choice (:choice "\n" "\r" "\r\n") :blank))))
                   "}")
  interface_method_definition (:prec-right 0
                               (:seq
                                (:field :name identifier)
                                (:choice (:field :generic_parameters generic_parameters) :blank)
                                (:field :signature signature)
                                (:choice (:field :attributes attribute) :blank)))
  _expression (:choice _expression_without_blocks _expression_with_blocks)
  _expression_without_blocks (:choice
                              parenthesized_expression
                              go_expression
                              spawn_expression
                              call_expression
                              function_literal
                              reference_expression
                              _max_group
                              array_creation
                              fixed_array_creation
                              unary_expression
                              receive_expression
                              binary_expression
                              is_expression
                              in_expression
                              index_expression
                              slice_expression
                              as_type_cast_expression
                              selector_expression
                              enum_fetch
                              inc_expression
                              dec_expression
                              or_block_expression
                              option_propagation_expression
                              result_propagation_expression)
  _expression_with_blocks (:choice
                           type_initializer
                           anon_struct_value_expression
                           if_expression
                           match_expression
                           select_expression
                           sql_expression
                           lock_expression
                           unsafe_expression
                           compile_time_if_expression
                           map_init_expression)
  strictly_expression_list (:prec -2
                            (:seq
                             (:choice _expression mutable_expression)
                             ","
                             (:seq
                              (:choice _expression mutable_expression)
                              (:repeat (:seq "," (:choice _expression mutable_expression))))))
  inc_expression (:seq _expression "++")
  dec_expression (:seq _expression "--")
  or_block_expression (:seq _expression or_block)
  option_propagation_expression (:prec 9 (:seq _expression "?"))
  result_propagation_expression (:prec 9 (:seq _expression "!"))
  anon_struct_value_expression (:seq
                                "struct"
                                "{"
                                (:choice
                                 (:field :element_list element_list)
                                 (:field :short_element_list short_element_list))
                                "}")
  go_expression (:prec-left -1 (:seq "go" _expression))
  spawn_expression (:prec-left -1 (:seq "spawn" _expression))
  parenthesized_expression (:seq "(" (:field :expression _expression) ")")
  call_expression (:prec-right 7
                   (:choice
                    (:seq
                     (:field :function (:token "json.decode"))
                     (:field :arguments special_argument_list))
                    (:seq
                     (:field :name _expression)
                     (:choice (:field :type_parameters type_parameters) :blank)
                     (:field :arguments argument_list))))
  type_parameters (:prec-dynamic 2
                   (:seq
                    (:token-immediate "[")
                    (:seq plain_type (:repeat (:seq "," plain_type)))
                    "]"))
  argument_list (:seq
                 "("
                 (:choice
                  (:repeat
                   (:seq
                    argument
                    (:choice (:choice (:choice (:choice "\n" "\r" "\r\n") ";") ",") :blank)))
                  short_lambda)
                 ")")
  short_lambda (:seq
                "|"
                (:choice
                 (:seq reference_expression (:repeat (:seq "," reference_expression)))
                 :blank)
                "|"
                _expression_without_blocks)
  argument (:choice _expression mutable_expression keyed_element spread_expression)
  special_argument_list (:seq
                         "("
                         (:alias _plain_type_without_special plain_type)
                         (:choice (:seq "," _expression) :blank)
                         ")")
  type_initializer (:prec-right 8
                    (:seq (:field :type plain_type) (:field :body type_initializer_body)))
  type_initializer_body (:seq
                         "{"
                         (:choice
                          (:choice
                           (:field :element_list element_list)
                           (:field :short_element_list short_element_list))
                          :blank)
                         "}")
  element_list (:repeat1
                (:seq
                 (:choice spread_expression keyed_element reference_expression)
                 (:choice (:choice (:choice (:choice "\n" "\r" "\r\n") ";") ",") :blank)))
  short_element_list (:repeat1
                      (:seq
                       (:alias _expression element)
                       (:choice (:choice (:choice (:choice "\n" "\r" "\r\n") ";") ",") :blank)))
  field_name reference_expression
  keyed_element (:seq (:field :key field_name) ":" (:field :value _expression))
  function_literal (:prec-right 0
                    (:seq
                     "fn"
                     (:choice (:field :capture_list capture_list) :blank)
                     (:choice (:field :generic_parameters generic_parameters) :blank)
                     (:field :signature signature)
                     (:field :body block)))
  capture_list (:seq "[" (:seq capture (:repeat (:seq "," capture))) (:choice "," :blank) "]")
  capture (:seq (:choice mutability_modifiers :blank) reference_expression)
  reference_expression (:prec-left 0 identifier)
  type_reference_expression (:prec-left 0 identifier)
  unary_expression (:prec 6
                    (:seq
                     (:field :operator (:choice "+" "-" "!" "~" "^" "*" "&"))
                     (:field :operand _expression)))
  receive_expression (:prec-right 6 (:seq (:field :operator "<-") (:field :operand _expression)))
  binary_expression (:choice
                     (:prec-left 5
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "*" "/" "%" "<<" ">>" ">>>" "&" "&^"))
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
  as_type_cast_expression (:seq _expression "as" plain_type)
  or_block (:seq "or" (:field :block block))
  _max_group (:prec-left 1 (:choice pseudo_compile_time_identifier literal))
  escape_sequence (:token
                   (:prec 1
                    (:seq
                     "\\"
                     (:choice
                      (:pattern "u[a-fA-F\\d]{4}")
                      (:pattern "U[a-fA-F\\d]{8}")
                      (:pattern "x[a-fA-F\\d]{2}")
                      (:pattern "\\d{3}")
                      (:pattern "\\r?\\n")
                      (:pattern "['\"abfrntv$\\\\]")
                      (:pattern "\\S")))))
  literal (:choice
           int_literal
           float_literal
           _string_literal
           rune_literal
           none
           (:ref "true")
           (:ref "false")
           (:ref "nil"))
  none "none"
  (:ref "true") "true"
  (:ref "false") "false"
  (:ref "nil") "nil"
  spread_expression (:prec-right 6 (:seq "..." _expression))
  map_init_expression (:prec -1
                       (:seq
                        "{"
                        (:repeat
                         (:seq
                          map_keyed_element
                          (:choice (:choice (:choice (:choice "\n" "\r" "\r\n") ";") ",") :blank)))
                        "}"))
  map_keyed_element (:seq (:field :key _expression) ":" (:field :value _expression))
  array_creation (:prec-right 5 _array)
  fixed_array_creation (:prec-right 5 (:seq _array "!"))
  _array (:seq "[" (:repeat (:seq _expression (:choice "," :blank))) "]")
  selector_expression (:prec-dynamic -1
                       (:prec 7
                        (:seq
                         (:field :operand _expression)
                         (:choice "." "?.")
                         (:field :field
                          (:choice reference_expression compile_time_selector_expression)))))
  compile_time_selector_expression (:seq
                                    (:token-immediate "$(")
                                    (:field :field
                                     (:choice reference_expression selector_expression))
                                    ")")
  index_expression (:prec-dynamic -1
                    (:prec-right 7
                     (:seq
                      (:field :operand _expression)
                      (:choice "[" (:token-immediate "[") (:token "#["))
                      (:field :index _expression)
                      "]")))
  slice_expression (:prec 7
                    (:seq
                     (:field :operand _expression)
                     (:choice "[" (:token-immediate "[") (:token "#["))
                     range
                     "]"))
  if_expression (:seq
                 "if"
                 (:choice (:field :condition _expression) (:field :guard var_declaration))
                 (:field :block block)
                 (:choice else_branch :blank))
  else_branch (:seq "else" (:field :else_branch (:choice (:field :block block) if_expression)))
  compile_time_if_expression (:seq
                              "$if"
                              (:field :condition (:seq _expression (:choice "?" :blank)))
                              (:field :block block)
                              (:choice
                               (:seq
                                "$else"
                                (:field :else_branch (:choice block compile_time_if_expression)))
                               :blank))
  is_expression (:prec-dynamic 2
                 (:seq
                  (:field :left (:seq (:choice mutability_modifiers :blank) _expression))
                  (:choice "is" "!is")
                  (:field :right plain_type)))
  in_expression (:prec-left 3
                 (:seq (:field :left _expression) (:choice "in" "!in") (:field :right _expression)))
  enum_fetch (:prec-dynamic -1 (:seq "." reference_expression))
  match_expression (:seq
                    "match"
                    (:field :condition (:choice _expression mutable_expression))
                    "{"
                    (:choice match_arms :blank)
                    "}")
  match_arms (:repeat1 (:choice match_arm match_else_arm_clause))
  match_arm (:seq (:field :value match_expression_list) (:field :block block))
  match_expression_list (:seq
                         (:choice
                          _expression_without_blocks
                          match_arm_type
                          (:alias _definite_range range))
                         (:repeat
                          (:seq
                           ","
                           (:choice
                            _expression_without_blocks
                            match_arm_type
                            (:alias _definite_range range)))))
  match_arm_type plain_type
  match_else_arm_clause (:seq "else" (:field :block block))
  select_expression (:seq
                     "select"
                     (:choice (:field :selected_variables expression_list) :blank)
                     "{"
                     (:repeat select_arm)
                     (:choice select_else_arn_clause :blank)
                     "}")
  select_arm (:seq select_arm_statement block)
  select_arm_statement (:prec-left 0
                        (:choice
                         (:alias select_var_declaration var_declaration)
                         send_statement
                         (:seq
                          (:alias expression_without_blocks_list expression_list)
                          (:choice _select_arm_assignment_statement :blank))))
  _select_arm_assignment_statement (:seq
                                    (:choice
                                     "*="
                                     "/="
                                     "%="
                                     "<<="
                                     ">>="
                                     ">>>="
                                     "&="
                                     "&^="
                                     "+="
                                     "-="
                                     "|="
                                     "^="
                                     "=")
                                    (:alias expression_without_blocks_list expression_list))
  select_var_declaration (:prec-left 0
                          (:seq
                           (:field :var_list identifier_list)
                           ":="
                           (:field :expression_list
                            (:alias expression_without_blocks_list expression_list))))
  select_else_arn_clause (:seq "else" block)
  lock_expression (:seq
                   (:choice "lock" "rlock")
                   (:choice (:field :locked_variables expression_list) :blank)
                   (:field :body block))
  unsafe_expression (:seq "unsafe" block)
  sql_expression (:prec 1 (:seq "sql" (:choice identifier :blank) _content_block))
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
  rune_literal (:token
                (:seq
                 "`"
                 (:choice
                  (:pattern "[^'\\\\]")
                  "'"
                  "\""
                  (:seq
                   "\\"
                   (:choice
                    "0"
                    "`"
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
                    (:seq (:choice "a" "b" "e" "f" "n" "r" "t" "v" "\\" "'" "\"")))))
                 "`"))
  _string_literal (:choice interpreted_string_literal c_string_literal raw_string_literal)
  interpreted_string_literal (:choice
                              (:seq
                               "'"
                               (:repeat
                                (:choice
                                 (:token-immediate (:prec-right 1 (:pattern "[^'\\\\$]+")))
                                 "$"
                                 escape_sequence
                                 string_interpolation))
                               "'")
                              (:seq
                               "\""
                               (:repeat
                                (:choice
                                 (:token-immediate (:prec-right 1 (:pattern "[^\"\\\\$]+")))
                                 "$"
                                 escape_sequence
                                 string_interpolation))
                               "\""))
  c_string_literal (:choice
                    (:seq
                     "c'"
                     (:repeat
                      (:choice
                       (:token-immediate (:prec-right 1 (:pattern "[^'\\\\$]+")))
                       "$"
                       escape_sequence
                       string_interpolation))
                     "'")
                    (:seq
                     "c\""
                     (:repeat
                      (:choice
                       (:token-immediate (:prec-right 1 (:pattern "[^\"\\\\$]+")))
                       "$"
                       escape_sequence
                       string_interpolation))
                     "\""))
  raw_string_literal (:choice
                      (:seq
                       "r'"
                       (:repeat (:token-immediate (:prec-right 1 (:pattern "[^']+"))))
                       "'")
                      (:seq
                       "r\""
                       (:repeat (:token-immediate (:prec-right 1 (:pattern "[^\"]+"))))
                       "\""))
  string_interpolation (:seq
                        (:alias "${" interpolation_opening)
                        (:choice
                         (:repeat (:alias _expression interpolation_expression))
                         (:seq (:alias _expression interpolation_expression) format_specifier))
                        (:alias "}" interpolation_closing))
  format_specifier (:seq
                    (:token ":")
                    (:choice
                     (:token (:pattern "[bgGeEfFcdoxXpsS]"))
                     (:seq
                      (:choice (:choice (:token (:pattern "[+\\-]")) (:token "0")) :blank)
                      (:choice int_literal :blank)
                      (:choice (:seq "." int_literal) :blank)
                      (:choice (:token (:pattern "[bgGeEfFcdoxXpsS]")) :blank))))
  pseudo_compile_time_identifier (:token
                                  (:seq
                                   "@"
                                   (:alias
                                    (:token-immediate (:pattern "[A-Z][A-Z0-9_]+"))
                                    identifier)))
  identifier (:token
              (:seq
               (:choice "@" :blank)
               (:choice "$" :blank)
               (:choice "C." :blank)
               (:choice "JS." :blank)
               (:choice (:pattern "[a-zA-Zα-ωΑ-Ωµ]") "_")
               (:repeat
                (:choice (:choice (:pattern "[a-zA-Zα-ωΑ-Ωµ]") "_") (:pattern "[0-9]")))))
  visibility_modifiers (:prec-left 0 (:choice "pub" "__global"))
  mutability_modifiers (:prec-left 1
                        (:choice
                         (:seq "mut" (:choice "static" :blank) (:choice "volatile" :blank))
                         "shared"))
  mutable_identifier (:prec 1 (:seq mutability_modifiers identifier))
  mutable_expression (:prec 1 (:seq mutability_modifiers _expression))
  identifier_list (:prec 2
                   (:seq
                    (:choice mutable_identifier identifier)
                    (:repeat (:seq "," (:choice mutable_identifier identifier)))))
  expression_list (:prec 1
                   (:seq
                    (:choice _expression mutable_expression)
                    (:repeat (:seq "," (:choice _expression mutable_expression)))))
  expression_without_blocks_list (:prec 1
                                  (:seq
                                   _expression_without_blocks
                                   (:repeat (:seq "," _expression_without_blocks))))
  sum_type (:prec-right 0
            (:seq
             plain_type
             (:repeat1 (:seq (:choice (:pattern "\\s+") :blank) (:token-immediate "|") plain_type))))
  plain_type (:prec-right 7
              (:choice _plain_type_without_special option_type result_type multi_return_type))
  _plain_type_without_special (:prec-right 7
                               (:choice
                                type_reference_expression
                                qualified_type
                                pointer_type
                                wrong_pointer_type
                                array_type
                                fixed_array_type
                                function_type
                                generic_type
                                map_type
                                channel_type
                                shared_type
                                thread_type
                                atomic_type
                                anon_struct_type))
  anon_struct_type (:seq "struct" _struct_body)
  multi_return_type (:seq
                     "("
                     (:seq plain_type (:repeat (:seq "," plain_type)))
                     (:choice "," :blank)
                     ")")
  result_type (:prec-right 0 (:seq "!" (:choice plain_type :blank)))
  option_type (:prec-right 0 (:seq "?" (:choice plain_type :blank)))
  qualified_type (:seq
                  (:field :module reference_expression)
                  "."
                  (:field :name type_reference_expression))
  fixed_array_type (:seq
                    "["
                    (:field :size (:choice int_literal reference_expression selector_expression))
                    "]"
                    (:field :element plain_type))
  array_type (:prec-right 7 (:seq "[" "]" (:field :element plain_type)))
  pointer_type (:prec 9 (:seq "&" plain_type))
  wrong_pointer_type (:prec 9 (:seq "*" plain_type))
  map_type (:seq "map[" (:field :key plain_type) "]" (:field :value plain_type))
  channel_type (:prec-right 7 (:seq "chan" plain_type))
  shared_type (:seq "shared" plain_type)
  thread_type (:seq "thread" plain_type)
  atomic_type (:seq "atomic" plain_type)
  generic_type (:seq (:choice qualified_type type_reference_expression) type_parameters)
  function_type (:prec-right 0 (:seq "fn" (:field :signature signature)))
  _statement (:choice
              simple_statement
              assert_statement
              continue_statement
              break_statement
              return_statement
              asm_statement
              goto_statement
              labeled_statement
              defer_statement
              for_statement
              compile_time_for_statement
              send_statement
              block
              hash_statement
              append_statement)
  simple_statement (:choice
                    var_declaration
                    _expression
                    assignment_statement
                    (:alias strictly_expression_list expression_list))
  assert_statement (:prec-left 0
                    (:seq "assert" _expression (:choice (:seq "," _expression) :blank)))
  append_statement (:prec 6 (:seq (:field :left _expression) "<<" (:field :right _expression)))
  send_statement (:prec-right 7
                  (:seq (:field :channel _expression) "<-" (:field :value _expression)))
  var_declaration (:prec-right 0
                   (:seq
                    (:field :var_list expression_list)
                    ":="
                    (:field :expression_list expression_list)))
  var_definition_list (:seq var_definition (:repeat (:seq "," var_definition)))
  var_definition (:prec 8
                  (:seq (:choice (:field :modifiers "mut") :blank) (:field :name identifier)))
  assignment_statement (:seq
                        (:field :left expression_list)
                        (:field :operator
                         (:choice
                          "*="
                          "/="
                          "%="
                          "<<="
                          ">>="
                          ">>>="
                          "&="
                          "&^="
                          "+="
                          "-="
                          "|="
                          "^="
                          "="))
                        (:field :right expression_list))
  _block_element (:choice _statement import_list _top_level_declaration)
  block (:seq
         "{"
         (:repeat (:seq _block_element (:choice (:choice (:choice "\n" "\r" "\r\n") ";") :blank)))
         "}")
  defer_statement (:seq "defer" block)
  label_reference identifier
  goto_statement (:seq "goto" label_reference)
  break_statement (:prec-right 0 (:seq "break" (:choice label_reference :blank)))
  continue_statement (:prec-right 0 (:seq "continue" (:choice label_reference :blank)))
  return_statement (:prec-right 0
                    (:seq "return" (:choice (:field :expression_list expression_list) :blank)))
  label_definition (:seq identifier ":")
  labeled_statement (:prec-right 0 (:seq label_definition (:choice _statement :blank)))
  compile_time_for_statement (:seq "$for" range_clause (:field :body block))
  for_statement (:seq
                 "for"
                 (:choice (:choice range_clause for_clause is_clause _expression) :blank)
                 (:field :body block))
  is_clause (:prec 7 (:seq (:choice (:alias "mut" mutability_modifiers) :blank) is_expression))
  range_clause (:prec-left 7
                (:seq
                 (:field :left var_definition_list)
                 "in"
                 (:field :right (:choice (:alias _definite_range range) _expression))))
  for_clause (:prec-left 0
              (:seq
               (:choice (:field :initializer simple_statement) :blank)
               ";"
               (:choice (:field :condition _expression) :blank)
               ";"
               (:choice (:field :update simple_statement) :blank)))
  _definite_range (:prec 5
                   (:seq
                    (:field :start _expression)
                    (:field :operator (:choice ".." "..."))
                    (:field :end _expression)))
  range (:prec 5
         (:seq
          (:choice (:field :start _expression) :blank)
          (:field :operator "..")
          (:choice (:field :end _expression) :blank)))
  hash_statement (:seq "#" (:token-immediate (:repeat1 (:pattern "[^\\\\\\r\\n]"))))
  asm_statement (:seq
                 "asm"
                 (:choice (:field :modifiers (:choice "volatile" "goto")) :blank)
                 (:choice (:field :arch identifier) :blank)
                 _content_block)
  _content_block (:seq "{" (:token-immediate (:prec 1 (:pattern "[^{}]+"))) "}")
  attributes (:repeat1 (:seq attribute (:choice (:choice "\n" "\r" "\r\n") :blank)))
  attribute (:seq
             (:choice "[" "@[")
             (:seq attribute_expression (:repeat (:seq ";" attribute_expression)))
             "]")
  attribute_expression (:prec 10 (:choice if_attribute _plain_attribute))
  if_attribute (:prec 10 (:seq "if" reference_expression (:choice "?" :blank)))
  _plain_attribute (:choice literal_attribute value_attribute key_value_attribute)
  literal_attribute (:prec 10 literal)
  value_attribute (:prec 10
                   (:field :name
                    (:choice (:alias "unsafe" reference_expression) reference_expression)))
  key_value_attribute (:prec 10
                       (:seq value_attribute ":" (:field :value (:choice literal identifier))))}}
