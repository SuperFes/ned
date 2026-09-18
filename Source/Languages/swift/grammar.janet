# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "swift"
 :extras [comment multiline_comment (:pattern "\\s+")]
 :conflicts [[attribute]
             [_attribute_argument]
             [_simple_user_type _expression]
             [user_type]
             [value_argument]
             [_expression lambda_parameter]
             [_primary_expression lambda_parameter]
             [_tuple_type_item_identifier tuple_expression]
             [modifiers]
             [_additive_operator _prefix_unary_operator]
             [_referenceable_operator _prefix_unary_operator]
             [capture_list_item _expression]
             [capture_list_item _expression _simple_user_type]
             [_primary_expression capture_list_item]
             [call_suffix expr_hack_at_ternary_binary_call_suffix]
             [try_expression _unary_expression]
             [try_expression _expression]
             [await_expression _unary_expression]
             [await_expression _expression]
             [consume_expression _unary_expression]
             [consume_expression _expression]
             [_local_property_declaration
              _local_typealias_declaration
              _local_function_declaration
              _local_class_declaration
              computed_getter
              computed_modify
              computed_setter]
             [_bodyless_function_declaration property_modifier]
             [init_declaration property_modifier]
             [_navigable_type_expression _case_pattern]
             [_no_expr_pattern_already_bound _binding_pattern_no_expr]
             [_lambda_type_declaration
              _local_property_declaration
              _local_typealias_declaration
              _local_function_declaration
              _local_class_declaration]
             [constructor_suffix]
             [call_suffix]
             [_modifierless_class_declaration property_modifier]
             [_fn_call_lambda_arguments]
             [parameter_modifiers]
             [_contextual_simple_identifier _modifierless_class_declaration]
             [_contextual_simple_identifier property_behavior_modifier]
             [_contextual_simple_identifier parameter_modifier]
             [_contextual_simple_identifier type_parameter_pack]
             [_contextual_simple_identifier type_pack_expansion]
             [_contextual_simple_identifier visibility_modifier]
             [_contextual_simple_identifier _consume_operator]]
 :precedences []
 :externals [multiline_comment
             raw_str_part
             raw_str_continuing_indicator
             raw_str_end_part
             _implicit_semi
             _explicit_semi
             _arrow_operator_custom
             _dot_custom
             _conjunction_operator_custom
             _disjunction_operator_custom
             _nil_coalescing_operator_custom
             _eq_custom
             _eq_eq_custom
             _plus_then_ws
             _minus_then_ws
             _bang_custom
             _throws_keyword
             _rethrows_keyword
             default_keyword
             where_keyword
             else
             catch_keyword
             _as_custom
             _as_quest_custom
             _as_bang_custom
             _async_keyword_custom
             _custom_operator
             _hash_symbol_custom
             _directive_if
             _directive_elseif
             _directive_else
             _directive_endif
             _fake_try_bang]
 :inline [_locally_permitted_modifiers]
 :supertypes []
 :rules
 {source_file (:seq
               (:choice shebang_line :blank)
               (:choice
                (:seq
                 _top_level_statement
                 (:repeat (:seq _semi _top_level_statement))
                 (:choice _semi :blank))
                :blank))
  _semi (:choice _implicit_semi _explicit_semi)
  shebang_line (:seq _hash_symbol "!" (:pattern "[^\\r\\n]*"))
  comment (:token (:prec -3 (:seq "//" (:pattern ".*"))))
  simple_identifier (:choice
                     (:pattern "[_\\p{XID_Start}\\p{Emoji}&&[^0-9#*]](\\p{EMod}|\\x{FE0F}\\x{20E3}?)?([_\\p{XID_Continue}\\p{Emoji}\\x{200D}](\\p{EMod}|\\x{FE0F}\\x{20E3}?)?)*")
                     (:pattern "`[^\\r\\n` ]*`")
                     (:pattern "\\$[0-9]+")
                     (:token
                      (:seq
                       "$"
                       (:pattern "[_\\p{XID_Start}\\p{Emoji}&&[^0-9#*]](\\p{EMod}|\\x{FE0F}\\x{20E3}?)?([_\\p{XID_Continue}\\p{Emoji}\\x{200D}](\\p{EMod}|\\x{FE0F}\\x{20E3}?)?)*")))
                     _contextual_simple_identifier)
  _contextual_simple_identifier (:choice
                                 "actor"
                                 "async"
                                 "consume"
                                 "discard"
                                 "each"
                                 "lazy"
                                 "repeat"
                                 "package"
                                 _parameter_ownership_modifier)
  identifier (:seq simple_identifier (:repeat (:seq _dot simple_identifier)))
  _basic_literal (:choice
                  integer_literal
                  hex_literal
                  oct_literal
                  bin_literal
                  real_literal
                  boolean_literal
                  _string_literal
                  regex_literal
                  "nil")
  real_literal (:token
                (:choice
                 (:seq
                  (:token
                   (:seq (:pattern "[0-9]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                  (:token
                   (:seq
                    (:pattern "[eE]")
                    (:choice (:pattern "[+-]") :blank)
                    (:token
                     (:seq (:pattern "[0-9]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+"))))))))
                 (:seq
                  (:choice
                   (:token
                    (:seq (:pattern "[0-9]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                   :blank)
                  "."
                  (:token
                   (:seq (:pattern "[0-9]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                  (:choice
                   (:token
                    (:seq
                     (:pattern "[eE]")
                     (:choice (:pattern "[+-]") :blank)
                     (:token
                      (:seq
                       (:pattern "[0-9]+")
                       (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))))
                   :blank))
                 (:seq
                  "0x"
                  (:token
                   (:seq
                    (:pattern "[0-9a-fA-F]+")
                    (:repeat (:seq (:pattern "_+") (:pattern "[0-9a-fA-F]+")))))
                  (:choice
                   (:seq
                    "."
                    (:token
                     (:seq
                      (:pattern "[0-9a-fA-F]+")
                      (:repeat (:seq (:pattern "_+") (:pattern "[0-9a-fA-F]+"))))))
                   :blank)
                  (:token
                   (:seq
                    (:pattern "[pP]")
                    (:choice (:pattern "[+-]") :blank)
                    (:token
                     (:seq (:pattern "[0-9]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+"))))))))))
  integer_literal (:token
                   (:seq
                    (:choice (:pattern "[1-9]") :blank)
                    (:token
                     (:seq (:pattern "[0-9]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))))
  hex_literal (:token
               (:seq
                "0"
                (:pattern "[xX]")
                (:token
                 (:seq
                  (:pattern "[0-9a-fA-F]+")
                  (:repeat (:seq (:pattern "_+") (:pattern "[0-9a-fA-F]+")))))))
  oct_literal (:token
               (:seq
                "0"
                (:pattern "[oO]")
                (:token
                 (:seq (:pattern "[0-7]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-7]+")))))))
  bin_literal (:token
               (:seq
                "0"
                (:pattern "[bB]")
                (:token
                 (:seq (:pattern "[01]+") (:repeat (:seq (:pattern "_+") (:pattern "[01]+")))))))
  boolean_literal (:choice "true" "false")
  _string_literal (:choice line_string_literal multi_line_string_literal raw_string_literal)
  line_string_literal (:seq
                       "\""
                       (:repeat (:choice (:field :text _line_string_content) _interpolation))
                       "\"")
  _line_string_content (:choice line_str_text str_escaped_char)
  line_str_text (:pattern "[^\\\\\"]+")
  str_escaped_char (:choice _escaped_identifier _uni_character_literal)
  _uni_character_literal (:seq "\\" "u" (:pattern "\\{[0-9a-fA-F]+\\}"))
  multi_line_string_literal (:seq
                             "\"\"\""
                             (:repeat
                              (:choice (:field :text _multi_line_string_content) _interpolation))
                             "\"\"\"")
  raw_string_literal (:seq
                      (:repeat
                       (:seq
                        (:field :text raw_str_part)
                        (:field :interpolation raw_str_interpolation)
                        (:choice raw_str_continuing_indicator :blank)))
                      (:field :text raw_str_end_part))
  raw_str_interpolation (:seq raw_str_interpolation_start _interpolation_contents ")")
  raw_str_interpolation_start (:pattern "\\\\#*\\(")
  _multi_line_string_content (:choice multi_line_str_text str_escaped_char "\"")
  _interpolation (:seq "\\(" _interpolation_contents ")")
  _interpolation_contents (:seq
                           (:field :interpolation (:alias value_argument interpolated_expression))
                           (:repeat
                            (:seq
                             ","
                             (:field :interpolation (:alias value_argument interpolated_expression))))
                           (:choice "," :blank))
  _escaped_identifier (:pattern "\\\\[0\\\\tnr\"'\\n]")
  multi_line_str_text (:pattern "[^\\\\\"]+")
  regex_literal (:choice _extended_regex_literal _multiline_regex_literal _oneline_regex_literal)
  _extended_regex_literal (:seq _hash_symbol (:pattern "\\/((\\/[^#])|[^\\n])+\\/#"))
  _multiline_regex_literal (:seq
                            _hash_symbol
                            (:pattern "\\/\\n")
                            (:pattern "(\\/[^#]|[^/])*?\\n\\/#"))
  _oneline_regex_literal (:token
                          (:prec -4
                           (:seq
                            "/"
                            (:token-immediate (:pattern "[^ \\t\\n]?[^/\\n]*[^ \\t\\n/]"))
                            (:token-immediate "/"))))
  type_annotation (:seq ":" (:field :type _possibly_implicitly_unwrapped_type))
  _possibly_implicitly_unwrapped_type (:seq _type (:choice (:token-immediate "!") :blank))
  _type (:prec-right -1 (:seq (:choice type_modifiers :blank) (:field :name _unannotated_type)))
  _unannotated_type (:prec-right -1
                     (:choice
                      user_type
                      tuple_type
                      function_type
                      array_type
                      dictionary_type
                      optional_type
                      metatype
                      bracket_qualified_type
                      opaque_type
                      existential_type
                      protocol_composition_type
                      type_parameter_pack
                      type_pack_expansion
                      suppressed_constraint))
  user_type (:seq _simple_user_type (:repeat (:seq _dot _simple_user_type)))
  _simple_user_type (:prec-right -1
                     (:seq
                      (:alias simple_identifier type_identifier)
                      (:choice type_arguments :blank)))
  tuple_type (:choice
              (:seq
               "("
               (:choice
                (:seq
                 (:field :element tuple_type_item)
                 (:repeat (:seq "," (:field :element tuple_type_item)))
                 (:choice "," :blank))
                :blank)
               ")")
              (:alias _parenthesized_type tuple_type_item))
  tuple_type_item (:prec -1
                   (:seq
                    (:choice _tuple_type_item_identifier :blank)
                    (:choice parameter_modifiers :blank)
                    (:field :type _type)))
  _tuple_type_item_identifier (:prec -1
                               (:seq
                                (:choice wildcard_pattern :blank)
                                (:field :name simple_identifier)
                                ":"))
  function_type (:seq
                 (:field :params (:choice tuple_type _unannotated_type))
                 (:choice _async_keyword :blank)
                 (:choice (:choice throws_clause throws) :blank)
                 _arrow_operator
                 (:field :return_type _type))
  array_type (:seq "[" (:field :element _type) "]")
  dictionary_type (:seq "[" (:field :key _type) ":" (:field :value _type) "]")
  optional_type (:prec-left 0
                 (:seq
                  (:field :wrapped (:choice user_type tuple_type array_type dictionary_type))
                  (:repeat1
                   (:choice (:alias _immediate_quest "?") (:alias _nil_coalescing_operator "??")))))
  metatype (:seq _unannotated_type "." (:choice "Type" "Protocol"))
  bracket_qualified_type (:prec-left -1
                          (:seq
                           (:choice array_type dictionary_type)
                           (:repeat1 (:seq "." (:alias simple_identifier type_identifier)))))
  _quest "?"
  _immediate_quest (:token-immediate "?")
  opaque_type (:prec-right 0 (:seq "some" _unannotated_type))
  existential_type (:prec-right 0 (:seq "any" _unannotated_type))
  type_parameter_pack (:prec-left 0 (:seq "each" _unannotated_type))
  type_pack_expansion (:prec-left 0 (:seq "repeat" _unannotated_type))
  protocol_composition_type (:prec-left 0
                             (:seq
                              _unannotated_type
                              (:repeat1 (:seq "&" (:prec-right 0 _unannotated_type)))))
  suppressed_constraint (:prec-right 0
                         (:seq "~" (:field :suppressed (:alias simple_identifier type_identifier))))
  _expression (:prec -1
               (:choice
                simple_identifier
                _unary_expression
                _binary_expression
                ternary_expression
                _primary_expression
                if_statement
                switch_statement
                assignment
                value_parameter_pack
                value_pack_expansion
                (:seq _expression (:alias _immediate_quest "?"))))
  _unary_expression (:choice
                     postfix_expression
                     call_expression
                     macro_invocation
                     constructor_expression
                     navigation_expression
                     prefix_expression
                     as_expression
                     selector_expression
                     open_start_range_expression
                     open_end_range_expression
                     directive
                     diagnostic)
  postfix_expression (:prec-left 6
                      (:seq
                       (:field :target _expression)
                       (:field :operation _postfix_unary_operator)))
  constructor_expression (:prec -2
                          (:seq
                           (:field :constructed_type (:choice array_type dictionary_type user_type))
                           constructor_suffix))
  _parenthesized_type (:seq
                       "("
                       (:field :element (:choice opaque_type existential_type dictionary_type))
                       ")")
  navigation_expression (:prec-left -1
                         (:seq
                          (:field :target
                           (:choice _navigable_type_expression _expression _parenthesized_type))
                          (:field :suffix navigation_suffix)))
  _navigable_type_expression (:choice user_type array_type dictionary_type)
  open_start_range_expression (:prec-right -1
                               (:seq _range_operator (:prec-right -2 (:field :end _expression))))
  _range_operator (:choice _open_ended_range_operator _three_dot_operator)
  open_end_range_expression (:prec-right -1 (:seq (:field :start _expression) _three_dot_operator))
  prefix_expression (:prec-left 7
                     (:seq
                      (:field :operation _prefix_unary_operator)
                      (:field :target
                       (:choice _expression (:alias (:choice "async" "if" "switch") _expression)))))
  as_expression (:prec-left -1 (:seq (:field :expr _expression) as_operator (:field :type _type)))
  selector_expression (:seq
                       _hash_symbol
                       "selector"
                       "("
                       (:choice (:choice "getter:" "setter:") :blank)
                       _expression
                       ")")
  _binary_expression (:choice
                      multiplicative_expression
                      additive_expression
                      range_expression
                      infix_expression
                      nil_coalescing_expression
                      check_expression
                      equality_expression
                      comparison_expression
                      conjunction_expression
                      disjunction_expression
                      bitwise_operation)
  multiplicative_expression (:prec-left 11
                             (:seq
                              (:field :lhs _expression)
                              (:field :op _multiplicative_operator)
                              (:field :rhs _expression)))
  additive_expression (:prec-left 10
                       (:seq
                        (:field :lhs _expression)
                        (:field :op _additive_operator)
                        (:field :rhs _expression)))
  range_expression (:prec-right -1
                    (:seq
                     (:field :start _expression)
                     (:field :op _range_operator)
                     (:field :end _expr_hack_at_ternary_binary_suffix)))
  infix_expression (:prec-left 9
                    (:seq
                     (:field :lhs _expression)
                     (:field :op custom_operator)
                     (:field :rhs _expr_hack_at_ternary_binary_suffix)))
  nil_coalescing_expression (:prec-right 8
                             (:seq
                              (:field :value _expression)
                              _nil_coalescing_operator
                              (:field :if_nil _expr_hack_at_ternary_binary_suffix)))
  check_expression (:prec-left 7
                    (:seq
                     (:field :target _expression)
                     (:field :op _is_operator)
                     (:field :type _type)))
  comparison_expression (:prec-left 0
                         (:seq
                          (:field :lhs _expression)
                          (:field :op _comparison_operator)
                          (:field :rhs _expr_hack_at_ternary_binary_suffix)))
  equality_expression (:prec-left 5
                       (:seq
                        (:field :lhs _expression)
                        (:field :op _equality_operator)
                        (:field :rhs _expr_hack_at_ternary_binary_suffix)))
  conjunction_expression (:prec-left 4
                          (:seq
                           (:field :lhs _expression)
                           (:field :op _conjunction_operator)
                           (:field :rhs _expr_hack_at_ternary_binary_suffix)))
  disjunction_expression (:prec-left 3
                          (:seq
                           (:field :lhs _expression)
                           (:field :op _disjunction_operator)
                           (:field :rhs _expr_hack_at_ternary_binary_suffix)))
  bitwise_operation (:prec-left 0
                     (:seq
                      (:field :lhs _expression)
                      (:field :op _bitwise_binary_operator)
                      (:field :rhs _expr_hack_at_ternary_binary_suffix)))
  custom_operator (:choice (:token (:pattern "[\\/]+[*]+")) _custom_operator)
  navigation_suffix (:seq _dot (:field :suffix (:choice simple_identifier integer_literal)))
  call_suffix (:prec -2
               (:choice
                value_arguments
                (:prec-dynamic -1 _fn_call_lambda_arguments)
                (:seq value_arguments _fn_call_lambda_arguments)))
  constructor_suffix (:prec -2
                      (:choice
                       (:alias _constructor_value_arguments value_arguments)
                       (:prec-dynamic -1 _fn_call_lambda_arguments)
                       (:seq
                        (:alias _constructor_value_arguments value_arguments)
                        _fn_call_lambda_arguments)))
  _constructor_value_arguments (:seq
                                "("
                                (:choice
                                 (:seq
                                  value_argument
                                  (:repeat (:seq "," value_argument))
                                  (:choice "," :blank))
                                 :blank)
                                ")")
  _fn_call_lambda_arguments (:seq
                             lambda_literal
                             (:repeat
                              (:seq (:seq (:field :name simple_identifier) ":") lambda_literal)))
  type_arguments (:prec-left 0
                  (:seq "<" (:seq _type (:repeat (:seq "," _type)) (:choice "," :blank)) ">"))
  value_arguments (:seq
                   (:choice
                    (:seq
                     "("
                     (:choice
                      (:seq value_argument (:repeat (:seq "," value_argument)) (:choice "," :blank))
                      :blank)
                     ")")
                    (:seq
                     "["
                     (:choice
                      (:seq value_argument (:repeat (:seq "," value_argument)) (:choice "," :blank))
                      :blank)
                     "]")))
  value_argument_label (:prec-left 0
                        (:choice
                         simple_identifier
                         (:alias "if" simple_identifier)
                         (:alias "switch" simple_identifier)))
  value_argument (:prec-left 0
                  (:seq
                   (:choice type_modifiers :blank)
                   (:choice
                    (:repeat1 (:seq (:field :reference_specifier value_argument_label) ":"))
                    (:seq
                     (:choice (:seq (:field :name value_argument_label) ":") :blank)
                     (:field :value _expression)))))
  try_expression (:prec-right -2
                  (:seq
                   try_operator
                   (:field :expr
                    (:choice
                     (:prec-right -2 _expression)
                     (:prec-left 0 _binary_expression)
                     (:prec-left 0 call_expression)
                     (:prec-dynamic 1 (:prec-left -1 ternary_expression))))))
  await_expression (:prec-right -2
                    (:seq
                     _await_operator
                     (:field :expr
                      (:choice
                       (:prec-right -2 _expression)
                       (:prec-left 0 call_expression)
                       (:prec-dynamic 1 (:prec-left -1 ternary_expression))))))
  _await_operator (:alias "await" "await")
  consume_expression (:prec-right -2
                      (:seq
                       _consume_operator
                       (:field :expr
                        (:choice
                         (:prec-right -2 _expression)
                         (:prec-left 0 call_expression)
                         (:prec-dynamic 1 (:prec-left -1 ternary_expression))))))
  _consume_operator (:alias "consume" "consume")
  ternary_expression (:prec-right -2
                      (:seq
                       (:field :condition _expression)
                       _quest
                       (:field :if_true _expression)
                       ":"
                       (:field :if_false _expr_hack_at_ternary_binary_suffix)))
  _expr_hack_at_ternary_binary_suffix (:prec-left -2
                                       (:choice
                                        _expression
                                        (:alias expr_hack_at_ternary_binary_call call_expression)))
  expr_hack_at_ternary_binary_call (:seq
                                    _expression
                                    (:alias expr_hack_at_ternary_binary_call_suffix call_suffix))
  expr_hack_at_ternary_binary_call_suffix (:prec -2 value_arguments)
  call_expression (:prec -2 (:prec-dynamic 1 (:seq _expression call_suffix)))
  macro_invocation (:prec -2
                    (:prec-dynamic 1
                     (:seq
                      _hash_symbol
                      simple_identifier
                      (:choice type_parameters :blank)
                      call_suffix)))
  _primary_expression (:choice
                       tuple_expression
                       _basic_literal
                       lambda_literal
                       special_literal
                       playground_literal
                       array_literal
                       dictionary_literal
                       self_expression
                       super_expression
                       try_expression
                       await_expression
                       consume_expression
                       discard_statement
                       _referenceable_operator
                       key_path_expression
                       key_path_string_expression
                       (:prec-right -1 (:alias _three_dot_operator fully_open_range)))
  tuple_expression (:prec-right -1
                    (:seq
                     "("
                     (:seq
                      (:seq
                       (:choice (:seq (:field :name simple_identifier) ":") :blank)
                       (:field :value _expression))
                      (:repeat
                       (:seq
                        ","
                        (:seq
                         (:choice (:seq (:field :name simple_identifier) ":") :blank)
                         (:field :value _expression))))
                      (:choice "," :blank))
                     ")"))
  array_literal (:seq
                 "["
                 (:choice
                  (:seq
                   (:field :element _expression)
                   (:repeat (:seq "," (:field :element _expression)))
                   (:choice "," :blank))
                  :blank)
                 "]")
  dictionary_literal (:seq
                      "["
                      (:choice
                       ":"
                       (:seq
                        _dictionary_literal_item
                        (:repeat (:seq "," _dictionary_literal_item))
                        (:choice "," :blank)))
                      (:choice "," :blank)
                      "]")
  _dictionary_literal_item (:seq (:field :key _expression) ":" (:field :value _expression))
  special_literal (:seq
                   _hash_symbol
                   (:choice "file" "fileID" "filePath" "line" "column" "function" "dsohandle"))
  playground_literal (:seq
                      _hash_symbol
                      (:choice "colorLiteral" "fileLiteral" "imageLiteral")
                      "("
                      (:seq
                       (:seq simple_identifier ":" _expression)
                       (:repeat (:seq "," (:seq simple_identifier ":" _expression)))
                       (:choice "," :blank))
                      ")")
  lambda_literal (:prec-left -3
                  (:seq
                   (:choice "{" "^{")
                   (:choice _lambda_type_declaration :blank)
                   (:choice statements :blank)
                   "}"))
  _lambda_type_declaration (:seq
                            (:repeat attribute)
                            (:prec -1 (:choice (:field :captures capture_list) :blank))
                            (:choice (:field :type lambda_function_type) :blank)
                            "in")
  capture_list (:seq
                "["
                (:seq capture_list_item (:repeat (:seq "," capture_list_item)) (:choice "," :blank))
                "]")
  capture_list_item (:choice
                     (:field :name self_expression)
                     (:prec -1
                      (:seq
                       (:choice ownership_modifier :blank)
                       (:field :name simple_identifier)
                       (:choice (:seq _equal_sign (:field :value _expression)) :blank))))
  lambda_function_type (:prec -1
                        (:seq
                         (:choice
                          lambda_function_type_parameters
                          (:seq "(" (:choice lambda_function_type_parameters :blank) ")"))
                         (:choice _async_keyword :blank)
                         (:choice (:choice throws_clause throws) :blank)
                         (:choice
                          (:seq
                           _arrow_operator
                           (:field :return_type _possibly_implicitly_unwrapped_type))
                          :blank)))
  lambda_function_type_parameters (:seq
                                   lambda_parameter
                                   (:repeat (:seq "," lambda_parameter))
                                   (:choice "," :blank))
  lambda_parameter (:seq
                    (:choice
                     self_expression
                     (:prec -1 (:field :name simple_identifier))
                     (:prec -1
                      (:seq
                       (:choice (:field :external_name simple_identifier) :blank)
                       (:field :name simple_identifier)
                       ":"
                       (:choice parameter_modifiers :blank)
                       (:field :type _possibly_implicitly_unwrapped_type)))))
  self_expression "self"
  super_expression (:seq "super")
  _else_options (:choice _block if_statement)
  if_statement (:prec-right -1
                (:seq
                 "if"
                 (:seq
                  (:field :condition _if_condition_sequence_item)
                  (:repeat (:seq "," (:field :condition _if_condition_sequence_item))))
                 _block
                 (:choice (:seq else _else_options) :blank)))
  _if_condition_sequence_item (:choice _if_let_binding _expression availability_condition)
  _if_let_binding (:seq
                   _direct_or_indirect_binding
                   (:choice (:seq _equal_sign _expression) :blank)
                   (:choice where_clause :blank))
  guard_statement (:prec-right -1
                   (:seq
                    "guard"
                    (:seq
                     (:field :condition _if_condition_sequence_item)
                     (:repeat (:seq "," (:field :condition _if_condition_sequence_item))))
                    else
                    _block))
  switch_statement (:prec-right -1
                    (:seq "switch" (:field :expr _expression) "{" (:repeat switch_entry) "}"))
  switch_entry (:seq
                (:choice modifiers :blank)
                (:choice
                 (:seq
                  "case"
                  (:seq switch_pattern (:choice (:seq where_keyword _expression) :blank))
                  (:repeat (:seq "," switch_pattern)))
                 default_keyword)
                ":"
                statements
                (:choice "fallthrough" :blank))
  switch_pattern (:alias _binding_pattern_with_expr pattern)
  do_statement (:prec-right -1
                (:seq
                 "do"
                 (:choice (:choice throws_clause throws) :blank)
                 _block
                 (:repeat catch_block)))
  catch_block (:seq
               catch_keyword
               (:field :error (:choice (:alias _binding_pattern_no_expr pattern) :blank))
               (:choice where_clause :blank)
               _block)
  where_clause (:prec-left 0 (:seq where_keyword _expression))
  key_path_expression (:prec-right 1
                       (:seq
                        "\\"
                        (:choice (:choice _simple_user_type array_type dictionary_type) :blank)
                        (:repeat (:seq "." _key_path_component))))
  key_path_string_expression (:prec-left 0 (:seq _hash_symbol "keyPath" "(" _expression ")"))
  _key_path_component (:prec-left 0
                       (:choice
                        (:seq simple_identifier (:repeat _key_path_postfixes))
                        (:repeat1 _key_path_postfixes)))
  _key_path_postfixes (:choice
                       "?"
                       bang
                       "self"
                       (:seq
                        "["
                        (:choice (:seq value_argument (:repeat (:seq "," value_argument))) :blank)
                        "]"))
  try_operator (:prec-right 0
                (:seq "try" (:choice (:choice _try_operator_type :blank) _fake_try_bang)))
  _try_operator_type (:choice (:token-immediate "!") (:token-immediate "?"))
  _assignment_and_operator (:choice "+=" "-=" "*=" "/=" "%=" _equal_sign)
  _equality_operator (:choice "!=" "!==" _eq_eq "===")
  _comparison_operator (:choice "<" ">" "<=" ">=")
  _three_dot_operator (:alias "..." "...")
  _open_ended_range_operator (:alias "..<" "..<")
  _is_operator "is"
  _additive_operator (:choice (:alias _plus_then_ws "+") (:alias _minus_then_ws "-") "+" "-")
  _multiplicative_operator (:choice "*" (:alias (:token (:prec -4 "/")) "/") "%")
  as_operator (:choice _as _as_quest _as_bang)
  _prefix_unary_operator (:prec-right 0
                          (:choice "++" "--" "-" "+" bang "&" "~" _dot custom_operator))
  _bitwise_binary_operator (:choice "&" "|" "^" "<<" ">>")
  _postfix_unary_operator (:choice "++" "--" bang)
  directly_assignable_expression _expression
  statements (:prec-left 0
              (:seq _local_statement (:repeat (:seq _semi _local_statement)) (:choice _semi :blank)))
  _local_statement (:choice
                    _expression
                    _local_declaration
                    _labeled_statement
                    control_transfer_statement)
  _top_level_statement (:choice _expression _global_declaration _labeled_statement _throw_statement)
  _block (:prec 2 (:seq "{" (:choice statements :blank) "}"))
  _labeled_statement (:seq
                      (:choice statement_label :blank)
                      (:choice
                       for_statement
                       while_statement
                       repeat_while_statement
                       do_statement
                       if_statement
                       guard_statement
                       switch_statement))
  statement_label (:token (:pattern "[a-zA-Z_][a-zA-Z_0-9]*:"))
  for_statement (:prec 1
                 (:seq
                  "for"
                  (:choice try_operator :blank)
                  (:choice _await_operator :blank)
                  (:field :item (:alias _binding_pattern_no_expr pattern))
                  (:choice type_annotation :blank)
                  "in"
                  (:field :collection _for_statement_collection)
                  (:choice where_clause :blank)
                  _block))
  _for_statement_collection (:choice _expression (:alias for_statement_await await_expression))
  for_statement_await (:seq _await_operator _expression)
  while_statement (:prec 1
                   (:seq
                    "while"
                    (:seq
                     (:field :condition _if_condition_sequence_item)
                     (:repeat (:seq "," (:field :condition _if_condition_sequence_item))))
                    "{"
                    (:choice statements :blank)
                    "}"))
  repeat_while_statement (:prec 1
                          (:seq
                           "repeat"
                           "{"
                           (:choice statements :blank)
                           "}"
                           (:repeat _implicit_semi)
                           "while"
                           (:seq
                            (:field :condition _if_condition_sequence_item)
                            (:repeat (:seq "," (:field :condition _if_condition_sequence_item))))))
  control_transfer_statement (:choice
                              (:prec-right 0 _throw_statement)
                              (:prec-right 0
                               (:seq
                                _optionally_valueful_control_keyword
                                (:field :result (:choice _expression :blank)))))
  _throw_statement (:seq throw_keyword _expression)
  throw_keyword "throw"
  _optionally_valueful_control_keyword (:choice "return" "continue" "break" "yield")
  discard_statement (:prec-right -2 (:seq _discard_operator self_expression))
  _discard_operator (:alias "discard" "discard")
  assignment (:prec-left -3
              (:seq
               (:field :target directly_assignable_expression)
               (:field :operator _assignment_and_operator)
               (:field :result _expression)))
  value_parameter_pack (:prec-left 1 (:seq "each" _expression))
  value_pack_expansion (:prec-left 1 (:seq "repeat" _expression))
  availability_condition (:seq
                          _hash_symbol
                          (:choice "available" "unavailable")
                          "("
                          (:seq
                           _availability_argument
                           (:repeat (:seq "," _availability_argument))
                           (:choice "," :blank))
                          ")")
  _availability_argument (:choice
                          (:seq
                           identifier
                           (:seq integer_literal (:repeat (:seq "." integer_literal))))
                          "*")
  _global_declaration (:choice
                       import_declaration
                       property_declaration
                       typealias_declaration
                       function_declaration
                       init_declaration
                       class_declaration
                       protocol_declaration
                       operator_declaration
                       precedence_group_declaration
                       associatedtype_declaration
                       macro_declaration)
  _type_level_declaration (:choice
                           import_declaration
                           property_declaration
                           typealias_declaration
                           function_declaration
                           init_declaration
                           class_declaration
                           protocol_declaration
                           deinit_declaration
                           subscript_declaration
                           operator_declaration
                           precedence_group_declaration
                           associatedtype_declaration)
  _local_declaration (:choice
                      (:alias _local_property_declaration property_declaration)
                      (:alias _local_typealias_declaration typealias_declaration)
                      (:alias _local_function_declaration function_declaration)
                      (:alias _local_class_declaration class_declaration))
  _local_property_declaration (:seq
                               (:choice _locally_permitted_modifiers :blank)
                               _modifierless_property_declaration)
  _local_typealias_declaration (:seq
                                (:choice _locally_permitted_modifiers :blank)
                                _modifierless_typealias_declaration)
  _local_function_declaration (:seq
                               (:choice _locally_permitted_modifiers :blank)
                               _modifierless_function_declaration)
  _local_class_declaration (:seq
                            (:choice _locally_permitted_modifiers :blank)
                            _modifierless_class_declaration)
  import_declaration (:seq
                      (:choice modifiers :blank)
                      "import"
                      (:choice _import_kind :blank)
                      identifier)
  _import_kind (:choice "typealias" "struct" "class" "enum" "protocol" "let" "var" "func")
  protocol_property_declaration (:prec-right 0
                                 (:seq
                                  (:choice modifiers :blank)
                                  (:field :name (:alias _binding_kind_and_pattern pattern))
                                  (:choice type_annotation :blank)
                                  (:choice type_constraints :blank)
                                  protocol_property_requirements))
  protocol_property_requirements (:seq
                                  "{"
                                  (:repeat (:choice getter_specifier setter_specifier))
                                  "}")
  property_declaration (:seq (:choice modifiers :blank) _modifierless_property_declaration)
  _modifierless_property_declaration (:prec-right 0
                                      (:seq
                                       _possibly_async_binding_pattern_kind
                                       (:seq
                                        _single_modifierless_property_declaration
                                        (:repeat
                                         (:seq "," _single_modifierless_property_declaration)))))
  _single_modifierless_property_declaration (:prec-left 0
                                             (:seq
                                              (:field :name
                                               (:alias _no_expr_pattern_already_bound pattern))
                                              (:choice type_annotation :blank)
                                              (:choice type_constraints :blank)
                                              (:choice
                                               (:choice
                                                _expression_with_willset_didset
                                                _expression_without_willset_didset
                                                willset_didset_block
                                                (:field :computed_value computed_property))
                                               :blank)))
  _expression_with_willset_didset (:prec-dynamic 1
                                   (:seq
                                    _equal_sign
                                    (:field :value _expression)
                                    willset_didset_block))
  _expression_without_willset_didset (:seq _equal_sign (:field :value _expression))
  willset_didset_block (:choice
                        (:seq "{" willset_clause (:choice didset_clause :blank) "}")
                        (:seq "{" didset_clause (:choice willset_clause :blank) "}"))
  willset_clause (:seq
                  (:choice modifiers :blank)
                  "willSet"
                  (:choice (:seq "(" simple_identifier ")") :blank)
                  _block)
  didset_clause (:seq
                 (:choice modifiers :blank)
                 "didSet"
                 (:choice (:seq "(" simple_identifier ")") :blank)
                 _block)
  typealias_declaration (:seq (:choice modifiers :blank) _modifierless_typealias_declaration)
  _modifierless_typealias_declaration (:seq
                                       "typealias"
                                       (:field :name (:alias simple_identifier type_identifier))
                                       (:choice type_parameters :blank)
                                       _equal_sign
                                       (:field :value _type))
  function_declaration (:prec-right 0
                        (:seq _bodyless_function_declaration (:field :body function_body)))
  _modifierless_function_declaration (:prec-right 0
                                      (:seq
                                       _modifierless_function_declaration_no_body
                                       (:field :body function_body)))
  _bodyless_function_declaration (:seq
                                  (:choice modifiers :blank)
                                  (:choice "class" :blank)
                                  _modifierless_function_declaration_no_body)
  _modifierless_function_declaration_no_body (:prec-right 0
                                              (:seq
                                               _non_constructor_function_decl
                                               (:choice type_parameters :blank)
                                               _function_value_parameters
                                               (:choice _async_keyword :blank)
                                               (:choice (:choice throws_clause throws) :blank)
                                               (:choice
                                                (:seq
                                                 _arrow_operator
                                                 (:field :return_type
                                                  _possibly_implicitly_unwrapped_type))
                                                :blank)
                                               (:choice type_constraints :blank)))
  function_body _block
  macro_declaration (:seq
                     _macro_head
                     simple_identifier
                     (:choice type_parameters :blank)
                     _macro_signature
                     (:choice (:field :definition macro_definition) :blank)
                     (:choice type_constraints :blank))
  _macro_head (:seq (:choice modifiers :blank) "macro")
  _macro_signature (:seq
                    _function_value_parameters
                    (:choice (:seq _arrow_operator _unannotated_type) :blank))
  macro_definition (:seq _equal_sign (:field :body (:choice _expression external_macro_definition)))
  external_macro_definition (:seq _hash_symbol "externalMacro" value_arguments)
  class_declaration (:seq (:choice modifiers :blank) _modifierless_class_declaration)
  _modifierless_class_declaration (:prec-right 0
                                   (:choice
                                    (:seq
                                     (:field :declaration_kind (:choice "class" "struct" "actor"))
                                     (:field :name (:alias simple_identifier type_identifier))
                                     (:choice type_parameters :blank)
                                     (:choice (:seq ":" _inheritance_specifiers) :blank)
                                     (:choice type_constraints :blank)
                                     (:field :body class_body))
                                    (:seq
                                     (:field :declaration_kind "extension")
                                     (:field :name _unannotated_type)
                                     (:choice type_parameters :blank)
                                     (:choice (:seq ":" _inheritance_specifiers) :blank)
                                     (:choice type_constraints :blank)
                                     (:field :body class_body))
                                    (:seq
                                     (:choice "indirect" :blank)
                                     (:field :declaration_kind "enum")
                                     (:field :name (:alias simple_identifier type_identifier))
                                     (:choice type_parameters :blank)
                                     (:choice (:seq ":" _inheritance_specifiers) :blank)
                                     (:choice type_constraints :blank)
                                     (:field :body enum_class_body))))
  class_body (:seq "{" (:choice _class_member_declarations :blank) "}")
  _inheritance_specifiers (:prec-left 0
                           (:seq
                            _annotated_inheritance_specifier
                            (:repeat (:seq (:choice "," "&") _annotated_inheritance_specifier))))
  inheritance_specifier (:prec-left 0
                         (:field :inherits_from
                          (:choice user_type function_type suppressed_constraint)))
  _annotated_inheritance_specifier (:seq (:repeat attribute) inheritance_specifier)
  type_parameters (:seq
                   "<"
                   (:seq type_parameter (:repeat (:seq "," type_parameter)) (:choice "," :blank))
                   (:choice type_constraints :blank)
                   ">")
  type_parameter (:seq
                  (:choice type_parameter_modifiers :blank)
                  _type_parameter_possibly_packed
                  (:choice (:seq ":" _type) :blank))
  _type_parameter_possibly_packed (:choice
                                   (:alias simple_identifier type_identifier)
                                   type_parameter_pack)
  type_constraints (:prec-right 0
                    (:seq
                     where_keyword
                     (:seq
                      type_constraint
                      (:repeat (:seq "," type_constraint))
                      (:choice "," :blank))))
  type_constraint (:choice inheritance_constraint equality_constraint)
  inheritance_constraint (:seq
                          (:repeat attribute)
                          (:field :constrained_type _constrained_type)
                          ":"
                          (:field :inherits_from _possibly_implicitly_unwrapped_type))
  equality_constraint (:seq
                       (:repeat attribute)
                       (:field :constrained_type _constrained_type)
                       (:choice _equal_sign _eq_eq)
                       (:field :must_equal _type))
  _constrained_type (:choice
                     identifier
                     (:seq
                      _unannotated_type
                      (:choice
                       (:seq "." (:seq simple_identifier (:repeat (:seq "." simple_identifier))))
                       :blank)))
  _class_member_separator (:choice _semi multiline_comment)
  _class_member_declarations (:seq
                              (:seq
                               (:choice _type_level_declaration directive)
                               (:repeat
                                (:seq
                                 _class_member_separator
                                 (:choice _type_level_declaration directive))))
                              (:choice _class_member_separator :blank))
  _function_value_parameters (:repeat1
                              (:seq
                               "("
                               (:choice
                                (:seq
                                 _function_value_parameter
                                 (:repeat (:seq "," _function_value_parameter))
                                 (:choice "," :blank))
                                :blank)
                               ")"))
  _function_value_parameter (:seq
                             (:choice attribute :blank)
                             parameter
                             (:choice (:seq _equal_sign (:field :default_value _expression)) :blank))
  parameter (:seq
             (:choice (:field :external_name simple_identifier) :blank)
             (:field :name simple_identifier)
             ":"
             (:choice parameter_modifiers :blank)
             (:field :type _possibly_implicitly_unwrapped_type)
             (:choice _three_dot_operator :blank))
  _non_constructor_function_decl (:seq
                                  "func"
                                  (:field :name (:choice simple_identifier _referenceable_operator)))
  _referenceable_operator (:choice
                           custom_operator
                           _comparison_operator
                           _additive_operator
                           _multiplicative_operator
                           _equality_operator
                           _comparison_operator
                           _assignment_and_operator
                           "++"
                           "--"
                           bang
                           "~"
                           "|"
                           "^"
                           "<<"
                           ">>"
                           "&")
  _equal_sign (:alias _eq_custom "=")
  _eq_eq (:alias _eq_eq_custom "==")
  _dot (:alias _dot_custom ".")
  _arrow_operator (:alias _arrow_operator_custom "->")
  _conjunction_operator (:alias _conjunction_operator_custom "&&")
  _disjunction_operator (:alias _disjunction_operator_custom "||")
  _nil_coalescing_operator (:alias _nil_coalescing_operator_custom "??")
  _as (:alias _as_custom "as")
  _as_quest (:alias _as_quest_custom "as?")
  _as_bang (:alias _as_bang_custom "as!")
  _hash_symbol (:alias _hash_symbol_custom "#")
  bang (:choice _bang_custom "!")
  _async_keyword (:alias _async_keyword_custom "async")
  _async_modifier (:token "async")
  throws (:choice _throws_keyword _rethrows_keyword)
  throws_clause (:seq _throws_keyword "(" (:field :type _unannotated_type) ")")
  enum_class_body (:seq "{" (:repeat (:choice enum_entry _type_level_declaration directive)) "}")
  enum_entry (:seq
              (:choice modifiers :blank)
              (:choice "indirect" :blank)
              "case"
              (:seq
               (:seq (:field :name simple_identifier) (:choice _enum_entry_suffix :blank))
               (:repeat
                (:seq
                 ","
                 (:seq (:field :name simple_identifier) (:choice _enum_entry_suffix :blank)))))
              (:choice ";" :blank))
  _enum_entry_suffix (:choice
                      (:field :data_contents enum_type_parameters)
                      (:seq _equal_sign (:field :raw_value _expression)))
  enum_type_parameters (:seq
                        "("
                        (:choice
                         (:seq
                          (:seq
                           (:choice
                            (:seq (:choice wildcard_pattern :blank) simple_identifier ":")
                            :blank)
                           _type
                           (:choice (:seq _equal_sign _expression) :blank))
                          (:repeat
                           (:seq
                            ","
                            (:seq
                             (:choice
                              (:seq (:choice wildcard_pattern :blank) simple_identifier ":")
                              :blank)
                             _type
                             (:choice (:seq _equal_sign _expression) :blank)))))
                         :blank)
                        ")")
  protocol_declaration (:prec-right 0
                        (:seq
                         (:choice modifiers :blank)
                         (:field :declaration_kind "protocol")
                         (:field :name (:alias simple_identifier type_identifier))
                         (:choice type_parameters :blank)
                         (:choice (:seq ":" _inheritance_specifiers) :blank)
                         (:choice type_constraints :blank)
                         (:field :body protocol_body)))
  protocol_body (:seq "{" (:choice _protocol_member_declarations :blank) "}")
  _protocol_member_declarations (:seq
                                 (:seq
                                  (:choice _protocol_member_declaration directive)
                                  (:repeat
                                   (:seq _semi (:choice _protocol_member_declaration directive))))
                                 (:choice _semi :blank))
  _protocol_member_declaration (:choice
                                (:alias
                                 (:seq
                                  _bodyless_function_declaration
                                  (:choice (:field :body function_body) :blank))
                                 protocol_function_declaration)
                                init_declaration
                                deinit_declaration
                                protocol_property_declaration
                                typealias_declaration
                                associatedtype_declaration
                                subscript_declaration)
  init_declaration (:prec-right 0
                    (:seq
                     (:choice modifiers :blank)
                     (:choice "class" :blank)
                     (:field :name "init")
                     (:choice (:choice _quest bang) :blank)
                     (:choice type_parameters :blank)
                     _function_value_parameters
                     (:choice _async_keyword :blank)
                     (:choice (:choice throws_clause throws) :blank)
                     (:choice type_constraints :blank)
                     (:choice (:field :body function_body) :blank)))
  deinit_declaration (:prec-right 0
                      (:seq (:choice modifiers :blank) "deinit" (:field :body function_body)))
  subscript_declaration (:prec-right 0
                         (:seq
                          (:choice modifiers :blank)
                          "subscript"
                          (:choice type_parameters :blank)
                          _function_value_parameters
                          (:choice
                           (:seq
                            _arrow_operator
                            (:field :return_type _possibly_implicitly_unwrapped_type))
                           :blank)
                          (:choice type_constraints :blank)
                          computed_property))
  computed_property (:seq
                     "{"
                     (:choice
                      (:choice statements :blank)
                      (:repeat (:choice computed_getter computed_setter computed_modify)))
                     "}")
  computed_getter (:seq (:repeat attribute) getter_specifier (:choice _block :blank))
  computed_modify (:seq (:repeat attribute) modify_specifier (:choice _block :blank))
  computed_setter (:seq
                   (:repeat attribute)
                   setter_specifier
                   (:choice (:seq "(" simple_identifier ")") :blank)
                   (:choice _block :blank))
  getter_specifier (:seq (:choice mutation_modifier :blank) "get" (:choice _getter_effects :blank))
  setter_specifier (:seq (:choice mutation_modifier :blank) "set")
  modify_specifier (:seq (:choice mutation_modifier :blank) "_modify")
  _getter_effects (:repeat1 (:choice _async_keyword throws_clause throws))
  operator_declaration (:seq
                        (:choice "prefix" "infix" "postfix")
                        "operator"
                        _referenceable_operator
                        (:choice (:seq ":" simple_identifier) :blank)
                        (:choice deprecated_operator_declaration_body :blank))
  deprecated_operator_declaration_body (:seq
                                        "{"
                                        (:repeat (:choice simple_identifier _basic_literal))
                                        "}")
  precedence_group_declaration (:seq
                                "precedencegroup"
                                simple_identifier
                                "{"
                                (:choice precedence_group_attributes :blank)
                                "}")
  precedence_group_attributes (:repeat1 precedence_group_attribute)
  precedence_group_attribute (:seq
                              simple_identifier
                              ":"
                              (:choice simple_identifier boolean_literal))
  associatedtype_declaration (:seq
                              (:choice modifiers :blank)
                              "associatedtype"
                              (:field :name (:alias simple_identifier type_identifier))
                              (:choice (:seq ":" (:field :must_inherit _type)) :blank)
                              (:choice type_constraints :blank)
                              (:choice (:seq _equal_sign (:field :default_value _type)) :blank))
  attribute (:seq
             "@"
             user_type
             (:choice
              (:seq
               "("
               (:seq
                _attribute_argument
                (:repeat (:seq "," _attribute_argument))
                (:choice "," :blank))
               ")")
              :blank))
  _attribute_argument (:choice
                       (:seq simple_identifier ":" _expression)
                       _expression
                       (:repeat1 (:seq simple_identifier ":"))
                       (:seq
                        (:repeat1 simple_identifier)
                        (:seq integer_literal (:repeat (:seq "." integer_literal)))))
  _universally_allowed_pattern (:choice
                                wildcard_pattern
                                _tuple_pattern
                                _type_casting_pattern
                                _case_pattern)
  _bound_identifier (:field :bound_identifier simple_identifier)
  _binding_pattern_no_expr (:seq
                            (:choice
                             _universally_allowed_pattern
                             _binding_pattern
                             _bound_identifier)
                            (:choice _quest :blank))
  _no_expr_pattern_already_bound (:seq
                                  (:choice _universally_allowed_pattern _bound_identifier)
                                  (:choice _quest :blank))
  _binding_pattern_with_expr (:seq
                              (:choice _universally_allowed_pattern _binding_pattern _expression)
                              (:choice _quest :blank))
  _non_binding_pattern_with_expr (:seq
                                  (:choice _universally_allowed_pattern _expression)
                                  (:choice _quest :blank))
  _direct_or_indirect_binding (:seq
                               (:choice
                                _binding_kind_and_pattern
                                (:seq "case" _binding_pattern_no_expr))
                               (:choice type_annotation :blank))
  value_binding_pattern (:field :mutability (:choice "var" "let"))
  _possibly_async_binding_pattern_kind (:seq (:choice _async_modifier :blank) value_binding_pattern)
  _binding_kind_and_pattern (:seq
                             _possibly_async_binding_pattern_kind
                             _no_expr_pattern_already_bound)
  wildcard_pattern "_"
  _tuple_pattern_item (:choice
                       (:seq
                        simple_identifier
                        (:seq ":" (:alias _binding_pattern_with_expr pattern)))
                       (:alias _binding_pattern_with_expr pattern))
  _tuple_pattern (:seq
                  "("
                  (:seq
                   _tuple_pattern_item
                   (:repeat (:seq "," _tuple_pattern_item))
                   (:choice "," :blank))
                  ")")
  _case_pattern (:seq
                 (:choice "case" :blank)
                 (:choice user_type :blank)
                 _dot
                 simple_identifier
                 (:choice _tuple_pattern :blank))
  _type_casting_pattern (:choice
                         (:seq "is" _type)
                         (:seq (:alias _binding_pattern_no_expr pattern) _as _type))
  _binding_pattern (:seq
                    (:seq (:choice "case" :blank) value_binding_pattern)
                    _no_expr_pattern_already_bound)
  modifiers (:repeat1
             (:prec-left 0 (:choice _non_local_scope_modifier _locally_permitted_modifiers)))
  _locally_permitted_modifiers (:repeat1 (:choice attribute _locally_permitted_modifier))
  parameter_modifiers (:repeat1 parameter_modifier)
  _modifier (:choice _non_local_scope_modifier _locally_permitted_modifier)
  _non_local_scope_modifier (:choice
                             member_modifier
                             visibility_modifier
                             function_modifier
                             mutation_modifier
                             property_modifier
                             parameter_modifier)
  _locally_permitted_modifier (:choice
                               ownership_modifier
                               inheritance_modifier
                               property_behavior_modifier)
  property_behavior_modifier "lazy"
  type_modifiers (:repeat1 attribute)
  member_modifier (:choice
                   "override"
                   "convenience"
                   "required"
                   (:seq
                    "nonisolated"
                    (:choice (:seq "(" (:choice "unsafe" "nonsending") ")") :blank)))
  visibility_modifier (:seq
                       (:choice "public" "private" "internal" "fileprivate" "open" "package")
                       (:choice (:seq "(" "set" ")") :blank))
  type_parameter_modifiers (:repeat1 attribute)
  function_modifier (:choice "infix" "postfix" "prefix")
  mutation_modifier (:choice "mutating" "nonmutating")
  property_modifier (:choice "static" "dynamic" "optional" "class" "distributed")
  inheritance_modifier (:choice "final")
  parameter_modifier (:choice "inout" "@escaping" "@autoclosure" _parameter_ownership_modifier)
  ownership_modifier (:choice "weak" "unowned" "unowned(safe)" "unowned(unsafe)")
  _parameter_ownership_modifier (:choice "borrowing" "consuming")
  use_site_target (:seq
                   (:choice "property" "get" "set" "receiver" "param" "setparam" "delegate")
                   ":")
  directive (:prec-right -3
             (:choice
              (:seq (:alias _directive_if "#if") _compilation_condition)
              (:seq (:alias _directive_elseif "#elseif") _compilation_condition)
              (:seq (:alias _directive_else "#else"))
              (:seq (:alias _directive_endif "#endif"))))
  _compilation_condition (:prec-right 0
                          (:choice
                           (:seq "os" "(" simple_identifier ")")
                           (:seq "arch" "(" simple_identifier ")")
                           (:seq
                            "swift"
                            "("
                            _comparison_operator
                            (:seq integer_literal (:repeat (:seq "." integer_literal)))
                            ")")
                           (:seq
                            "compiler"
                            "("
                            _comparison_operator
                            (:seq integer_literal (:repeat (:seq "." integer_literal)))
                            ")")
                           (:seq
                            "canImport"
                            "("
                            (:seq simple_identifier (:repeat (:seq "." simple_identifier)))
                            ")")
                           (:seq "targetEnvironment" "(" simple_identifier ")")
                           boolean_literal
                           simple_identifier
                           (:seq "(" _compilation_condition ")")
                           (:seq "!" _compilation_condition)
                           (:seq
                            _compilation_condition
                            _conjunction_operator
                            _compilation_condition)
                           (:seq
                            _compilation_condition
                            _disjunction_operator
                            _compilation_condition)))
  diagnostic (:prec -3
              (:seq
               _hash_symbol
               (:choice
                (:seq (:pattern "error([^\\r\\n]*)"))
                (:seq (:pattern "warning([^\\r\\n]*)"))
                (:seq (:pattern "sourceLocation([^\\r\\n]*)")))))
  unused_for_backward_compatibility (:choice (:alias "unused1" "try?") (:alias "unused2" "try!"))}}
