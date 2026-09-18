# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "dart"
 :word identifier
 :extras [comment documentation_comment (:pattern "\\s")]
 :conflicts [[annotation _bare_annotation]
             [_record_literal_no_const record_field]
             [_record_literal_no_const record_type]
             [_record_literal_no_const _strict_formal_parameter_list]
             [getter_signature function_signature _var_or_type]
             [setter_signature function_signature _var_or_type]
             [operator_signature _var_or_type]
             [_primary _type_not_void_not_function _function_type_tail]
             [_primary _function_type_tail]
             [block set_or_map_literal]
             [_type_name _primary function_signature]
             [_primary function_signature]
             [_primary _type_name]
             [_primary _simple_formal_parameter]
             [_primary _type_name _function_formal_parameter]
             [_primary constructor_param]
             [_normal_formal_parameters]
             [_declared_identifier]
             [record_type_field _function_formal_parameter _var_or_type]
             [typed_identifier _var_or_type _function_formal_parameter]
             [_type_name _simple_formal_parameter]
             [_type_not_function _type_not_void]
             [switch_statement_case]
             [declaration _external_and_static]
             [constructor_signature _formal_parameter_part]
             [_cascade_subsection]
             [_expression]
             [_postfix_expression]
             [pattern_variable_declaration _var_or_type]
             [_final_const_var_or_type pattern_variable_declaration]
             [type_arguments relational_operator]
             [prefix_operator constant_pattern]
             [_primary constant_pattern _type_name]
             [_literal constant_pattern]
             [_primary constant_pattern]
             [_final_var_or_type]
             [_primary constant_pattern _type_name _simple_formal_parameter]
             [_parenthesized_pattern _pattern_field]
             [record_type_field _var_or_type _final_var_or_type _function_formal_parameter]
             [_var_or_type _final_var_or_type]
             [_final_const_var_or_type _final_var_or_type]
             [_var_or_type _for_loop_parts pattern_variable_declaration]
             [pattern_variable_declaration _for_loop_parts _final_const_var_or_type]
             [_var_or_type _final_var_or_type _function_formal_parameter]
             [set_or_map_literal map_pattern]
             [list_literal list_pattern]
             [constant_pattern _type_name]
             [_pattern_field label]
             [constructor_tearoff _identifier_or_new]
             [_primary constant_pattern _simple_formal_parameter]
             [record_type_field _final_var_or_type]
             [set_or_map_literal constant_pattern]
             [list_literal constant_pattern]
             [_var_or_type function_signature]
             [_var_or_type _function_formal_parameter]
             [relational_operator type_arguments type_parameters]
             [_var_or_type]
             [_final_const_var_or_type const_object_expression]
             [_final_const_var_or_type]
             [type_parameter _type_name]
             [_normal_formal_parameter]
             [_assignable_selector_part selector]
             [_assignable_selector_part _postfix_expression]
             [_primary assignable_expression]
             [_simple_formal_parameter assignable_expression]
             [assignable_expression _postfix_expression]
             [_type_name _function_formal_parameter]
             [_type_name]
             [_primary _type_name assignable_expression]
             [_type_name function_signature]
             [declaration _external]
             [_function_type_tail]
             [_type_not_void_not_function _function_type_tail]
             [_type_not_void]
             [_type_not_void_not_function]
             [super_formal_parameter unconditional_assignable_selector]]
 :precedences []
 :externals [_template_chars_double
             _template_chars_single
             _template_chars_double_single
             _template_chars_single_single
             _template_chars_raw_slash
             _block_comment
             _documentation_block_comment]
 :inline [_ambiguous_name _class_member_definition _if_null_expression]
 :supertypes [_declaration _statement _literal]
 :rules
 {program (:seq
           (:choice script_tag :blank)
           (:choice library_name :blank)
           (:repeat import_or_export)
           (:repeat part_directive)
           (:repeat part_of_directive)
           (:repeat _top_level_definition))
  _top_level_definition (:choice
                         class_definition
                         mixin_declaration
                         extension_declaration
                         extension_type_declaration
                         enum_declaration
                         type_alias
                         (:seq
                          (:choice _metadata :blank)
                          (:choice _external_builtin :blank)
                          function_signature
                          _semicolon)
                         (:seq
                          (:choice _metadata :blank)
                          (:choice _external_builtin :blank)
                          getter_signature
                          _semicolon)
                         (:seq
                          (:choice _metadata :blank)
                          (:choice _external_builtin :blank)
                          setter_signature
                          _semicolon)
                         (:seq (:choice _metadata :blank) getter_signature function_body)
                         (:seq (:choice _metadata :blank) setter_signature function_body)
                         (:seq (:choice _metadata :blank) function_signature function_body)
                         (:prec-dynamic -10
                          (:seq
                           (:repeat1 (:alias _bare_annotation annotation))
                           (:alias _record_return_function_signature function_signature)
                           function_body))
                         (:seq
                          (:choice _metadata :blank)
                          (:choice final_builtin const_builtin)
                          (:choice _type :blank)
                          static_final_declaration_list
                          _semicolon)
                         (:seq
                          (:choice _metadata :blank)
                          _late_builtin
                          final_builtin
                          (:choice _type :blank)
                          initialized_identifier_list
                          _semicolon)
                         (:seq
                          (:choice _metadata :blank)
                          (:choice _late_builtin :blank)
                          (:choice _type inferred_type)
                          initialized_identifier_list
                          _semicolon)
                         (:seq
                          (:choice _metadata :blank)
                          _external_builtin
                          (:choice
                           (:seq final_builtin (:choice _type :blank) identifier_list)
                           (:seq (:choice _late_builtin :blank) _var_or_type identifier_list))
                          _semicolon))
  _bool_literal (:choice (:ref "true") (:ref "false"))
  _numeric_literal (:choice
                    decimal_integer_literal
                    decimal_floating_point_literal
                    hex_integer_literal)
  _literal (:choice
            null_literal
            _bool_literal
            _numeric_literal
            string_literal
            symbol_literal
            set_or_map_literal
            list_literal
            record_literal)
  symbol_literal (:prec-right 0
                  (:seq
                   "#"
                   (:choice
                    (:seq identifier (:repeat (:seq "." identifier)))
                    equality_operator
                    relational_operator
                    shift_operator
                    additive_operator
                    multiplicative_operator
                    "~"
                    "|"
                    "&"
                    "^"
                    "[]"
                    "[]=")))
  decimal_integer_literal (:token
                           (:token
                            (:seq
                             (:pattern "[0-9]+")
                             (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+"))))))
  hex_integer_literal (:token
                       (:seq
                        (:choice "0x" "0X")
                        (:token
                         (:seq
                          (:pattern "[A-Fa-f0-9]+")
                          (:repeat (:seq "_" (:pattern "[A-Fa-f0-9]+")))))))
  decimal_floating_point_literal (:token
                                  (:choice
                                   (:seq
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                    "."
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                    (:choice
                                     (:seq
                                      (:pattern "[eE]")
                                      (:choice (:choice "-" "+") :blank)
                                      (:token
                                       (:seq
                                        (:pattern "[0-9]+")
                                        (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+"))))))
                                     :blank))
                                   (:seq
                                    "."
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                    (:choice
                                     (:seq
                                      (:pattern "[eE]")
                                      (:choice (:choice "-" "+") :blank)
                                      (:token
                                       (:seq
                                        (:pattern "[0-9]+")
                                        (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+"))))))
                                     :blank))
                                   (:seq
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                    (:pattern "[eE]")
                                    (:choice (:choice "-" "+") :blank)
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+"))))))
                                   (:seq
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                    (:choice
                                     (:seq
                                      (:pattern "[eE]")
                                      (:choice (:choice "-" "+") :blank)
                                      (:token
                                       (:seq
                                        (:pattern "[0-9]+")
                                        (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+"))))))
                                     :blank))))
  (:ref "true") (:prec 0 "true")
  (:ref "false") (:prec 0 "false")
  string_literal (:repeat1
                  (:choice
                   _string_literal_double_quotes
                   _string_literal_single_quotes
                   _string_literal_double_quotes_multiple
                   _string_literal_single_quotes_multiple
                   _raw_string_literal_double_quotes
                   _raw_string_literal_single_quotes
                   _raw_string_literal_double_quotes_multiple
                   _raw_string_literal_single_quotes_multiple))
  _string_literal_double_quotes (:seq
                                 "\""
                                 (:repeat
                                  (:choice
                                   _template_chars_double_single
                                   "'"
                                   escape_sequence
                                   _sub_string_test
                                   template_substitution))
                                 "\"")
  _string_literal_single_quotes (:seq
                                 "'"
                                 (:repeat
                                  (:choice
                                   _template_chars_single_single
                                   "\""
                                   escape_sequence
                                   _sub_string_test
                                   template_substitution))
                                 "'")
  _string_literal_double_quotes_multiple (:prec-left 0
                                          (:seq
                                           "\"\"\""
                                           (:repeat
                                            (:choice
                                             _template_chars_double
                                             "'"
                                             "\""
                                             escape_sequence
                                             _sub_string_test
                                             template_substitution))
                                           "\"\"\""))
  _string_literal_single_quotes_multiple (:prec-left 0
                                          (:seq
                                           "'''"
                                           (:repeat
                                            (:choice
                                             _template_chars_single
                                             "\""
                                             "'"
                                             escape_sequence
                                             _sub_string_test
                                             template_substitution))
                                           "'''"))
  _raw_string_literal_double_quotes (:seq
                                     "r\""
                                     (:repeat
                                      (:choice
                                       _template_chars_double_single
                                       "'"
                                       _template_chars_raw_slash
                                       _unused_escape_sequence
                                       _sub_string_test
                                       "$"))
                                     "\"")
  _raw_string_literal_single_quotes (:seq
                                     "r'"
                                     (:repeat
                                      (:choice
                                       _template_chars_single_single
                                       "\""
                                       _template_chars_raw_slash
                                       _unused_escape_sequence
                                       _sub_string_test
                                       "$"))
                                     "'")
  _raw_string_literal_double_quotes_multiple (:prec-left 0
                                              (:seq
                                               "r\"\"\""
                                               (:repeat
                                                (:choice
                                                 _template_chars_double
                                                 "'"
                                                 _template_chars_raw_slash
                                                 "\""
                                                 _unused_escape_sequence
                                                 _sub_string_test
                                                 "$"))
                                               "\"\"\""))
  _raw_string_literal_single_quotes_multiple (:prec-left 0
                                              (:seq
                                               "r'''"
                                               (:repeat
                                                (:choice
                                                 _template_chars_single
                                                 "\""
                                                 "'"
                                                 _template_chars_raw_slash
                                                 _unused_escape_sequence
                                                 _sub_string_test
                                                 "$"))
                                               "'''"))
  _triple_quote_end (:token "'''")
  _triple_double_quote_end (:token "\"\"\"")
  template_substitution (:seq "$" (:choice (:seq "{" _expression "}") identifier_dollar_escaped))
  _sub_string_test (:seq "$" (:pattern "[^a-zA-Z_{]"))
  _string_interp (:pattern "\\$((\\w+)|\\{([^{}]+)\\})")
  _unused_escape_sequence (:token-immediate
                           (:seq
                            "\\"
                            (:choice
                             (:pattern "[^xu0-7]")
                             (:pattern "[0-7]{1,3}")
                             (:pattern "x[0-9a-fA-F]{2}")
                             (:pattern "u[0-9a-fA-F]{4}")
                             (:pattern "u\\{[0-9a-fA-F]+\\}"))))
  escape_sequence _unused_escape_sequence
  list_literal (:seq
                (:choice const_builtin :blank)
                (:choice type_arguments :blank)
                "["
                (:choice (:seq _element (:repeat (:seq "," _element)) (:choice "," :blank)) :blank)
                "]")
  set_or_map_literal (:seq
                      (:choice const_builtin :blank)
                      (:choice type_arguments :blank)
                      "{"
                      (:choice
                       (:seq _element (:repeat (:seq "," _element)) (:choice "," :blank))
                       :blank)
                      "}")
  pair (:seq
        (:field :key (:seq (:choice "?" :blank) _expression))
        ":"
        (:field :value (:seq (:choice "?" :blank) _expression)))
  _element (:choice
            (:seq (:choice "?" :blank) _expression)
            pair
            spread_element
            if_element
            for_element)
  null_literal (:prec 0 "null")
  record_literal (:seq (:choice const_builtin :blank) _record_literal_no_const)
  _record_literal_no_const (:choice
                            (:prec-dynamic -1 (:seq "(" ")"))
                            (:seq
                             "("
                             (:choice
                              (:seq label _expression ",")
                              (:seq label _expression)
                              (:seq _expression ",")
                              (:seq
                               record_field
                               (:repeat1 (:seq "," record_field))
                               (:choice "," :blank)))
                             ")"))
  record_field (:seq (:choice label :blank) _expression)
  _expression (:choice
               pattern_assignment
               assignment_expression
               throw_expression
               rethrow_expression
               (:seq _real_expression (:repeat cascade_section)))
  _expression_without_cascade (:choice
                               assignment_expression_without_cascade
                               _real_expression
                               throw_expression_without_cascade)
  _real_expression (:choice
                    conditional_expression
                    logical_or_expression
                    if_null_expression
                    additive_expression
                    multiplicative_expression
                    relational_expression
                    equality_expression
                    logical_and_expression
                    bitwise_and_expression
                    bitwise_or_expression
                    bitwise_xor_expression
                    shift_expression
                    type_cast_expression
                    type_test_expression
                    _unary_expression)
  throw_expression (:seq "throw" _expression)
  throw_expression_without_cascade (:seq "throw" _expression_without_cascade)
  rethrow_expression rethrow_builtin
  assignment_expression (:prec-right 1
                         (:seq
                          (:field :left assignable_expression)
                          (:field :operator _assignment_operator)
                          (:field :right _expression)))
  assignment_expression_without_cascade (:prec-right 1
                                         (:seq
                                          (:field :left assignable_expression)
                                          (:field :operator _assignment_operator)
                                          (:field :right _expression_without_cascade)))
  assignable_expression (:choice
                         (:seq _primary _assignable_selector_part)
                         (:seq super unconditional_assignable_selector)
                         (:seq constructor_invocation _assignable_selector_part)
                         identifier
                         (:alias _get identifier)
                         (:alias _set identifier)
                         (:alias _function_builtin_identifier identifier))
  _assignable_selector_part (:seq (:repeat selector) _assignable_selector)
  _assignment_operator (:choice
                        "="
                        "+="
                        "-="
                        "*="
                        "/="
                        "%="
                        "~/="
                        "<<="
                        ">>="
                        ">>>="
                        "&="
                        "^="
                        "|="
                        "??=")
  lambda_expression (:seq (:field :parameters function_signature) (:field :body function_body))
  function_expression (:seq
                       (:field :parameters _formal_parameter_part)
                       (:field :body function_expression_body))
  inferred_parameters (:seq "(" (:seq identifier (:repeat (:seq "," identifier))) ")")
  if_null_expression (:prec-left 4 (:seq (:field :first _real_expression) _if_null_expression))
  _if_null_expression (:repeat1 (:seq "??" (:field :second _real_expression)))
  conditional_expression (:prec-left 3
                          (:seq
                           _real_expression
                           (:seq
                            "?"
                            (:field :consequence _expression_without_cascade)
                            ":"
                            (:field :alternative _expression_without_cascade))))
  logical_or_expression (:prec-left 5
                         (:seq
                          _real_expression
                          (:repeat1 (:seq logical_or_operator _real_expression))))
  logical_and_expression (:prec-left 6
                          (:seq
                           _real_expression
                           (:repeat1 (:seq logical_and_operator _real_expression))))
  equality_expression (:prec-left 7
                       (:choice
                        (:seq _real_expression equality_operator _real_expression)
                        (:seq super equality_operator _real_expression)))
  equality_operator (:token (:choice "==" "!="))
  type_cast_expression (:prec-left 13 (:seq _real_expression type_cast))
  type_test_expression (:prec-left 13 (:seq _real_expression type_test))
  relational_expression (:prec-left 8
                         (:choice
                          (:seq _real_expression relational_operator _real_expression)
                          (:seq super relational_operator _real_expression)))
  relational_operator (:choice "<" ">" "<=" ">=")
  bitwise_or_expression (:prec-left 10
                         (:choice
                          (:seq _real_expression "|" _real_expression)
                          (:seq super "|" _real_expression)))
  bitwise_xor_expression (:prec-left 11
                          (:choice
                           (:seq _real_expression "^" _real_expression)
                           (:seq super "^" _real_expression)))
  bitwise_and_expression (:prec-left 12
                          (:choice
                           (:seq _real_expression "&" _real_expression)
                           (:seq super "&" _real_expression)))
  shift_expression (:prec-left 13
                    (:choice
                     (:seq _real_expression shift_operator _real_expression)
                     (:seq super shift_operator _real_expression)))
  additive_expression (:prec-left 14
                       (:choice
                        (:seq _real_expression additive_operator _real_expression)
                        (:seq super additive_operator _real_expression)))
  multiplicative_expression (:prec-left 15
                             (:choice
                              (:seq _real_expression multiplicative_operator _real_expression)
                              (:seq super multiplicative_operator _real_expression)))
  bitwise_operator _bitwise_operator
  _bitwise_operator (:choice "&" "^" "|")
  shift_operator _shift_operator
  _shift_operator (:choice "<<" ">>" ">>>")
  additive_operator _additive_operator
  _additive_operator (:token (:choice "+" "-"))
  multiplicative_operator _multiplicative_operator
  _multiplicative_operator (:choice "*" "/" "%" "~/")
  _unary_expression (:prec 16 (:choice _postfix_expression unary_expression))
  unary_expression (:prec 16
                    (:choice
                     (:seq prefix_operator _unary_expression)
                     await_expression
                     (:seq (:choice minus_operator tilde_operator) super)
                     (:seq increment_operator assignable_expression)))
  _postfix_expression (:choice (:seq _primary (:repeat selector)) postfix_expression)
  postfix_expression (:prec-right 0
                      (:choice
                       (:seq assignable_expression postfix_operator)
                       (:seq constructor_invocation (:repeat selector))))
  postfix_operator increment_operator
  increment_operator (:token (:choice "++" "--"))
  spread_element (:seq "..." (:choice "?" :blank) _expression)
  selector (:prec-right 0
            (:choice _exclamation_operator _assignable_selector argument_part type_arguments))
  prefix_operator (:choice minus_operator negation_operator tilde_operator)
  minus_operator "-"
  negation_operator _exclamation_operator
  _exclamation_operator "!"
  tilde_operator "~"
  await_expression (:seq "await" _unary_expression)
  type_test (:seq is_operator _type_not_void)
  is_operator (:seq (:token "is") (:choice _exclamation_operator :blank))
  type_cast (:seq as_operator _type_not_void)
  as_operator (:token "as")
  new_expression (:seq _new_builtin _type_not_void (:choice _dot_identifier :blank) arguments)
  _dot_identifier (:prec-dynamic 19 (:seq "." identifier))
  const_object_expression (:choice
                           (:seq
                            const_builtin
                            _type_not_void
                            (:choice _dot_identifier :blank)
                            arguments)
                           (:seq const_builtin dot_shorthand arguments))
  _primary (:choice
            _literal
            identifier
            (:alias _get identifier)
            (:alias _set identifier)
            (:alias _function_builtin_identifier identifier)
            function_expression
            new_expression
            const_object_expression
            parenthesized_expression
            this
            (:seq super unconditional_assignable_selector)
            constructor_tearoff
            switch_expression
            dot_shorthand)
  dot_shorthand (:prec-right 0 (:seq "." (:choice identifier _new_builtin)))
  parenthesized_expression (:seq "(" _expression ")")
  _compound_access (:choice "." "?.")
  constructor_invocation (:prec-right 0
                          (:choice
                           (:seq _type_name type_arguments "." identifier arguments)
                           (:seq _type_name "." _new_builtin arguments)))
  constructor_tearoff (:prec-right 0
                       (:seq _type_name (:choice type_arguments :blank) "." _new_builtin))
  arguments (:seq "(" (:choice _argument_list :blank) ")")
  _argument_list (:prec-right 0
                  (:seq _any_argument (:repeat (:seq "," _any_argument)) (:choice "," :blank)))
  _any_argument (:choice argument named_argument)
  argument _expression
  named_argument (:seq label _expression)
  cascade_section (:prec-left 2
                   (:seq
                    (:choice ".." "?..")
                    cascade_selector
                    (:repeat (:choice argument_part _exclamation_operator))
                    (:repeat _cascade_subsection)
                    (:choice _cascade_assignment_section :blank)))
  _cascade_subsection (:seq
                       _assignable_selector
                       (:repeat (:choice argument_part _exclamation_operator)))
  _cascade_assignment_section (:seq _assignment_operator _expression_without_cascade)
  index_selector (:seq "[" _expression "]")
  cascade_selector (:choice (:seq (:choice nullable_selector :blank) index_selector) identifier)
  argument_part (:prec-dynamic 1 (:seq (:choice type_arguments :blank) arguments))
  unconditional_assignable_selector (:choice index_selector (:seq "." identifier))
  conditional_assignable_selector (:choice (:seq "?." identifier) (:seq "?" index_selector))
  _assignable_selector (:choice unconditional_assignable_selector conditional_assignable_selector)
  type_arguments (:choice (:seq "<" (:choice (:seq _type (:repeat (:seq "," _type))) :blank) ">"))
  wildcard (:seq (:choice _metadata :blank) "?" (:choice _wildcard_bounds :blank))
  _wildcard_bounds (:choice (:seq "extends" _type) (:seq super _type))
  dimensions (:prec-right 0 (:repeat1 (:seq (:choice _metadata :blank) "[" "]")))
  _statement (:choice
              block
              (:prec-dynamic 1 local_function_declaration)
              (:prec-dynamic 2 local_variable_declaration)
              for_statement
              while_statement
              do_statement
              switch_statement
              if_statement
              try_statement
              break_statement
              continue_statement
              return_statement
              yield_statement
              yield_each_statement
              expression_statement
              empty_statement
              assert_statement
              labeled_statement)
  local_function_declaration (:seq (:choice _metadata :blank) lambda_expression)
  block (:seq "{" (:repeat _statement) "}")
  expression_statement (:seq _expression _semicolon)
  empty_statement ";"
  labeled_statement (:prec 1 (:seq identifier ":" _statement))
  assert_statement (:seq assertion ";")
  assertion (:seq assert_builtin assertion_arguments)
  assertion_arguments (:seq
                       "("
                       _expression
                       (:choice (:seq "," _expression) :blank)
                       (:choice "," :blank)
                       ")")
  switch_statement (:seq
                    "switch"
                    (:field :condition parenthesized_expression)
                    (:field :body switch_block))
  switch_expression (:seq
                     "switch"
                     (:field :condition parenthesized_expression)
                     (:field :body
                      (:seq
                       "{"
                       (:seq
                        switch_expression_case
                        (:repeat (:seq "," switch_expression_case))
                        (:choice "," :blank))
                       "}")))
  switch_expression_case (:seq _guarded_pattern "=>" _expression)
  _guarded_pattern (:seq _pattern (:choice (:seq "when" _expression) :blank))
  _pattern (:choice _logical_or_pattern)
  _logical_or_pattern (:seq
                       _logical_and_pattern
                       (:repeat (:seq logical_or_operator _logical_and_pattern)))
  _logical_and_pattern (:seq
                        _relational_pattern
                        (:repeat (:seq logical_and_operator _relational_pattern)))
  _relational_pattern (:prec 8
                       (:choice
                        (:seq (:choice relational_operator equality_operator) _real_expression)
                        _unary_pattern))
  _unary_pattern (:choice cast_pattern null_check_pattern null_assert_pattern _primary_pattern)
  _primary_pattern (:choice
                    constant_pattern
                    variable_pattern
                    _parenthesized_pattern
                    list_pattern
                    map_pattern
                    record_pattern
                    object_pattern)
  cast_pattern (:seq _primary_pattern "as" _type)
  null_check_pattern (:seq _primary_pattern "?")
  null_assert_pattern (:seq _primary_pattern "!")
  constant_pattern (:choice
                    _bool_literal
                    null_literal
                    (:seq (:choice minus_operator :blank) _numeric_literal)
                    string_literal
                    symbol_literal
                    identifier
                    qualified
                    const_object_expression
                    (:seq
                     dot_shorthand
                     (:choice (:seq (:choice type_arguments :blank) arguments) :blank))
                    (:seq
                     const_builtin
                     (:choice type_arguments :blank)
                     "["
                     (:seq _element (:repeat (:seq "," _element)) (:choice "," :blank))
                     "]")
                    (:seq
                     const_builtin
                     (:choice type_arguments :blank)
                     "{"
                     (:seq _element (:repeat (:seq "," _element)) (:choice "," :blank))
                     "}")
                    (:seq const_builtin "(" _expression ")"))
  variable_pattern (:seq _final_var_or_type identifier)
  _parenthesized_pattern (:seq "(" _pattern ")")
  list_pattern (:seq
                (:choice type_arguments :blank)
                "["
                (:choice
                 (:seq
                  _list_pattern_element
                  (:repeat (:seq "," _list_pattern_element))
                  (:choice "," :blank))
                 :blank)
                "]")
  _list_pattern_element (:choice _pattern rest_pattern)
  rest_pattern (:seq "..." (:choice _pattern :blank))
  map_pattern (:seq
               (:choice type_arguments :blank)
               "{"
               (:choice
                (:seq
                 _map_pattern_entry
                 (:repeat (:seq "," _map_pattern_entry))
                 (:choice "," :blank))
                :blank)
               "}")
  _map_pattern_entry (:choice (:seq _expression ":" _pattern) "...")
  record_pattern (:seq
                  "("
                  (:seq _pattern_field (:repeat (:seq "," _pattern_field)) (:choice "," :blank))
                  ")")
  _pattern_field (:seq (:choice (:seq (:choice identifier :blank) ":") :blank) _pattern)
  object_pattern (:seq
                  _type_name
                  (:choice type_arguments :blank)
                  "("
                  (:choice
                   (:seq _pattern_field (:repeat (:seq "," _pattern_field)) (:choice "," :blank))
                   :blank)
                  ")")
  pattern_variable_declaration (:seq
                                (:choice final_builtin inferred_type)
                                _outer_pattern
                                "="
                                _expression)
  _outer_pattern (:choice
                  _parenthesized_pattern
                  list_pattern
                  map_pattern
                  record_pattern
                  object_pattern)
  pattern_assignment (:seq _outer_pattern "=" _expression)
  switch_block (:seq
                "{"
                (:repeat switch_statement_case)
                (:choice switch_statement_default :blank)
                "}")
  switch_statement_case (:seq
                         (:repeat label)
                         case_builtin
                         _guarded_pattern
                         ":"
                         (:repeat _statement))
  switch_statement_default (:seq (:repeat label) "default" ":" (:repeat _statement))
  switch_case (:choice
               (:seq (:repeat label) case_builtin _guarded_pattern ":" (:repeat1 _statement)))
  default_case (:choice (:seq (:repeat label) "default" ":" (:repeat1 _statement)))
  switch_label (:seq
                (:repeat label)
                (:choice (:seq case_builtin _expression ":") (:seq "default" ":")))
  do_statement (:seq
                "do"
                (:field :body _statement)
                "while"
                (:field :condition parenthesized_expression)
                _semicolon)
  break_statement (:seq break_builtin (:choice identifier :blank) _semicolon)
  continue_statement (:seq "continue" (:choice identifier :blank) _semicolon)
  yield_statement (:seq "yield" _expression _semicolon)
  yield_each_statement (:seq "yield" "*" _expression _semicolon)
  return_statement (:seq "return" (:choice _expression :blank) _semicolon)
  throw_statement (:seq "throw" _expression _semicolon)
  try_statement (:seq
                 _try_head
                 (:choice
                  (:choice
                   finally_clause
                   (:seq (:repeat1 _on_part) (:choice finally_clause :blank)))
                  :blank))
  _on_part (:choice
            (:seq catch_clause block)
            (:seq "on" _type_not_void (:choice catch_clause :blank) block))
  _try_head (:seq "try" (:field :body block))
  catch_clause (:seq "catch" catch_parameters)
  catch_parameters (:seq "(" identifier (:choice (:seq "," identifier) :blank) ")")
  catch_type (:seq _type (:repeat (:seq "|" _type)))
  finally_clause (:seq "finally" block)
  if_element (:prec-right 0
              (:seq
               "if"
               "("
               _expression
               (:choice (:seq "case" _guarded_pattern) :blank)
               ")"
               (:field :consequence _element)
               (:choice (:seq "else" (:field :alternative _element)) :blank)))
  if_statement (:prec-right 0
                (:seq
                 "if"
                 "("
                 _expression
                 (:choice (:seq "case" _guarded_pattern) :blank)
                 ")"
                 (:field :consequence _statement)
                 (:choice (:seq "else" (:field :alternative _statement)) :blank)))
  while_statement (:seq
                   "while"
                   (:field :condition parenthesized_expression)
                   (:field :body _statement))
  for_statement (:seq (:choice "await" :blank) "for" for_loop_parts (:field :body _statement))
  for_loop_parts (:seq "(" _for_loop_parts ")")
  _for_loop_parts (:choice
                   (:seq (:choice _declared_identifier identifier) "in" (:field :value _expression))
                   (:seq
                    (:choice
                     (:choice
                      (:field :init local_variable_declaration)
                      (:seq
                       (:choice
                        (:seq
                         (:field :init _expression)
                         (:repeat (:seq "," (:field :init _expression))))
                        :blank)
                       _semicolon))
                     :blank)
                    (:field :condition (:choice _expression :blank))
                    _semicolon
                    (:choice
                     (:seq
                      (:field :update _expression)
                      (:repeat (:seq "," (:field :update _expression)))
                      (:choice "," :blank))
                     :blank))
                   (:seq
                    (:choice final_builtin inferred_type)
                    _outer_pattern
                    "in"
                    (:field :value _expression)))
  for_element (:seq (:choice "await" :blank) "for" for_loop_parts (:field :body _element))
  annotation (:prec-right 0
              (:seq
               "@"
               (:field :name (:choice identifier scoped_identifier))
               (:choice (:choice (:seq type_arguments arguments) :blank) (:choice arguments :blank))))
  _bare_annotation (:prec-dynamic -1
                    (:seq "@" (:field :name (:choice identifier scoped_identifier))))
  _declaration (:prec 1 (:choice import_specification class_definition enum_declaration))
  import_or_export (:prec 19 (:choice library_import library_export))
  library_import (:seq (:choice _metadata :blank) import_specification)
  library_export (:seq
                  (:choice _metadata :blank)
                  _export
                  configurable_uri
                  (:repeat combinator)
                  _semicolon)
  import_specification (:choice
                        (:seq
                         _import
                         configurable_uri
                         (:choice (:seq _as identifier) :blank)
                         (:repeat combinator)
                         _semicolon)
                        (:seq _import uri _deferred _as identifier (:repeat combinator) _semicolon))
  part_directive (:seq (:choice _metadata :blank) "part" uri _semicolon)
  part_of_directive (:seq
                     (:choice _metadata :blank)
                     part_of_builtin
                     (:choice dotted_identifier_list uri)
                     _semicolon)
  uri string_literal
  configurable_uri (:seq uri (:repeat configuration_uri))
  configuration_uri (:seq "if" configuration_uri_condition uri)
  configuration_uri_condition (:seq "(" uri_test ")")
  uri_test (:seq dotted_identifier_list (:choice (:seq "==" string_literal) :blank))
  combinator (:choice (:seq "show" _identifier_list) (:seq "hide" _identifier_list))
  _identifier_list (:seq identifier (:repeat (:seq "," identifier)))
  asterisk "*"
  enum_declaration (:seq
                    (:choice _metadata :blank)
                    "enum"
                    (:field :name identifier)
                    (:choice type_parameters :blank)
                    (:choice mixins :blank)
                    (:choice interfaces :blank)
                    (:field :body enum_body))
  enum_body (:seq
             "{"
             (:seq enum_constant (:repeat (:seq "," enum_constant)) (:choice "," :blank))
             (:choice
              (:seq
               ";"
               (:repeat
                (:choice
                 (:seq (:choice _metadata :blank) _class_member_definition)
                 (:prec-dynamic -10 _record_return_class_member))))
              :blank)
             "}")
  enum_constant (:choice
                 (:seq
                  (:choice _metadata :blank)
                  (:field :name identifier)
                  (:choice argument_part :blank))
                 (:seq
                  (:choice _metadata :blank)
                  (:field :name identifier)
                  (:choice type_arguments :blank)
                  "."
                  (:choice identifier _new_builtin)
                  arguments))
  type_alias (:choice
              (:seq
               (:choice _metadata :blank)
               _typedef
               (:choice _type :blank)
               _type_name
               _formal_parameter_part
               ";")
              (:seq
               (:choice _metadata :blank)
               _typedef
               _type_name
               (:choice type_parameters :blank)
               "="
               _type
               ";"))
  _class_modifiers (:seq
                    (:choice
                     sealed
                     (:seq
                      (:choice abstract :blank)
                      (:choice (:choice base interface "final" "inline") :blank)))
                    "class")
  _mixin_class_modifiers (:seq (:choice abstract :blank) (:choice base :blank) mixin "class")
  class_definition (:choice
                    (:seq
                     (:choice _metadata :blank)
                     (:choice _class_modifiers _mixin_class_modifiers)
                     (:field :name identifier)
                     (:choice (:field :type_parameters type_parameters) :blank)
                     (:choice (:field :superclass superclass) :blank)
                     (:choice (:field :interfaces interfaces) :blank)
                     (:field :body class_body))
                    (:seq (:choice _metadata :blank) _class_modifiers mixin_application_class))
  extension_declaration (:choice
                         (:seq
                          (:choice _metadata :blank)
                          "extension"
                          (:choice (:field :name identifier) :blank)
                          (:choice (:field :type_parameters type_parameters) :blank)
                          "on"
                          (:field :class _type)
                          (:field :body extension_body)))
  extension_type_declaration (:seq
                              (:choice _metadata :blank)
                              "extension"
                              "type"
                              (:choice const_builtin :blank)
                              (:field :name identifier)
                              (:choice (:field :type_parameters type_parameters) :blank)
                              (:field :representation representation_declaration)
                              (:choice (:field :interfaces interfaces) :blank)
                              (:field :body class_body))
  representation_declaration (:seq
                              (:choice (:seq "." (:choice identifier _new_builtin)) :blank)
                              "("
                              (:choice _metadata :blank)
                              (:field :type _type)
                              (:field :name identifier)
                              ")")
  _metadata (:prec-right 0 (:repeat1 annotation))
  type_parameters (:seq "<" (:seq type_parameter (:repeat (:seq "," type_parameter))) ">")
  type_parameter (:seq
                  (:choice _metadata :blank)
                  (:choice (:alias identifier type_identifier) nullable_type)
                  (:choice nullable_type :blank)
                  (:choice type_bound :blank))
  type_bound (:seq "extends" _type_not_void)
  superclass (:choice (:seq "extends" _type_not_void (:choice mixins :blank)) mixins)
  mixins (:seq "with" _type_not_void_list)
  mixin_application_class (:seq
                           identifier
                           (:choice type_parameters :blank)
                           "="
                           mixin_application
                           _semicolon)
  mixin_application (:seq _type_not_void mixins (:choice interfaces :blank))
  mixin_declaration (:seq
                     (:choice _metadata :blank)
                     (:choice base :blank)
                     mixin
                     identifier
                     (:choice type_parameters :blank)
                     (:choice (:seq "on" _type_not_void_list) :blank)
                     (:choice interfaces :blank)
                     class_body)
  interfaces (:seq _implements _type_not_void_list)
  interface_type_list (:seq _type (:repeat (:seq "," _type)))
  class_body (:seq
              "{"
              (:repeat
               (:choice
                (:seq (:choice _metadata :blank) _class_member_definition)
                (:prec-dynamic -10 _record_return_class_member)))
              "}")
  _record_return_class_member (:seq
                               (:repeat1 (:alias _bare_annotation annotation))
                               (:alias _record_return_method_signature method_signature)
                               function_body)
  _record_return_method_signature (:seq
                                   (:choice _static :blank)
                                   (:alias _record_return_function_signature function_signature))
  _record_return_function_signature (:seq
                                     (:alias
                                      (:choice
                                       (:seq
                                        "("
                                        (:seq
                                         record_type_field
                                         (:repeat (:seq "," record_type_field)))
                                        ","
                                        "{"
                                        (:seq
                                         record_type_named_field
                                         (:repeat (:seq "," record_type_named_field))
                                         (:choice "," :blank))
                                        "}"
                                        ")")
                                       (:seq
                                        "("
                                        (:seq
                                         record_type_field
                                         (:repeat (:seq "," record_type_field))
                                         (:choice "," :blank))
                                        ")")
                                       (:seq
                                        "("
                                        "{"
                                        (:seq
                                         record_type_named_field
                                         (:repeat (:seq "," record_type_named_field))
                                         (:choice "," :blank))
                                        "}"
                                        ")"))
                                      record_type)
                                     (:choice nullable_type :blank)
                                     (:field :name identifier)
                                     _formal_parameter_part
                                     (:choice _native :blank))
  extension_body (:seq
                  "{"
                  (:repeat
                   (:choice
                    (:seq (:choice _metadata :blank) declaration _semicolon)
                    (:seq (:choice _metadata :blank) (:seq method_signature function_body))
                    (:prec-dynamic -10 _record_return_class_member)))
                  "}")
  _class_member_definition (:choice
                            (:seq declaration _semicolon)
                            (:seq method_signature function_body))
  getter_signature (:seq
                    (:choice _type :blank)
                    _get
                    (:field :name identifier)
                    (:choice _native :blank))
  setter_signature (:seq
                    (:choice _type :blank)
                    _set
                    (:field :name identifier)
                    _formal_parameter_part
                    (:choice _native :blank))
  method_signature (:choice
                    (:seq constructor_signature (:choice initializers :blank))
                    factory_constructor_signature
                    (:seq
                     (:choice _static :blank)
                     (:choice function_signature getter_signature setter_signature))
                    operator_signature)
  declaration (:choice
               (:seq
                constant_constructor_signature
                (:choice (:choice redirection initializers) :blank))
               (:seq constructor_signature (:choice (:choice redirection initializers) :blank))
               (:seq _external (:choice const_builtin :blank) factory_constructor_signature)
               (:seq (:choice const_builtin :blank) factory_constructor_signature _native)
               (:seq _external constant_constructor_signature)
               redirecting_factory_constructor_signature
               (:seq _external constructor_signature)
               (:seq (:choice _external_builtin :blank) (:choice _static :blank) getter_signature)
               (:seq (:choice _external_and_static :blank) setter_signature)
               (:seq (:choice _external :blank) operator_signature)
               (:seq (:choice _external_and_static :blank) function_signature)
               (:seq
                _external_and_static
                _type
                (:choice
                 identifier
                 (:alias _get identifier)
                 (:alias _set identifier)
                 (:alias _operator identifier)))
               (:seq _static function_signature)
               (:seq
                _static
                (:choice
                 (:seq _final_or_const (:choice _type :blank) static_final_declaration_list)
                 (:seq
                  _late_builtin
                  (:choice
                   (:seq final_builtin (:choice _type :blank) initialized_identifier_list)
                   (:seq (:choice _type inferred_type) initialized_identifier_list)))
                 (:seq (:choice _type inferred_type) initialized_identifier_list)))
               (:seq
                _covariant
                (:choice
                 (:seq
                  _late_builtin
                  (:choice
                   (:seq final_builtin (:choice _type :blank) identifier_list)
                   (:seq (:choice _type inferred_type) initialized_identifier_list)))
                 (:seq (:choice _type inferred_type) initialized_identifier_list)))
               (:seq
                (:choice _late_builtin :blank)
                final_builtin
                (:choice _type :blank)
                initialized_identifier_list)
               (:seq (:choice _late_builtin :blank) _var_or_type initialized_identifier_list)
               (:seq
                _external
                (:choice
                 (:seq final_builtin (:choice _type :blank) identifier_list)
                 (:seq _covariant _var_or_type identifier_list)))
               (:seq
                abstract
                (:choice
                 (:seq final_builtin (:choice _type :blank) identifier_list)
                 (:seq _covariant _var_or_type identifier_list)
                 (:seq _var_or_type identifier_list))))
  identifier_list (:seq identifier (:repeat (:seq "," identifier)))
  initialized_identifier_list (:seq
                               initialized_identifier
                               (:repeat (:seq "," initialized_identifier)))
  initialized_identifier (:seq
                          (:choice
                           identifier
                           (:alias _get identifier)
                           (:alias _set identifier)
                           (:alias _operator identifier))
                          (:choice (:seq "=" _expression) :blank))
  static_final_declaration_list (:seq
                                 static_final_declaration
                                 (:repeat (:seq "," static_final_declaration)))
  binary_operator (:choice
                   multiplicative_operator
                   additive_operator
                   shift_operator
                   relational_operator
                   "=="
                   bitwise_operator)
  operator_signature (:seq
                      (:choice _type :blank)
                      _operator
                      (:choice "~" binary_operator "[]" "[]=")
                      formal_parameter_list
                      (:choice _native :blank))
  static_final_declaration (:seq identifier "=" _expression)
  _external_and_static (:seq _external (:choice _static :blank))
  _static_or_covariant (:choice _covariant _static)
  _final_or_const (:choice final_builtin const_builtin)
  static_initializer (:seq _static block)
  initializers (:seq ":" (:seq initializer_list_entry (:repeat (:seq "," initializer_list_entry))))
  initializer_list_entry (:choice
                          (:seq super arguments)
                          (:seq super (:seq "." (:choice identifier _new_builtin) arguments))
                          field_initializer
                          assertion)
  field_initializer (:seq (:choice (:seq this ".") :blank) identifier "=" _expression)
  factory_constructor_signature (:seq
                                 _factory
                                 (:seq identifier (:repeat (:seq "." identifier)))
                                 formal_parameter_list)
  redirecting_factory_constructor_signature (:seq
                                             (:choice const_builtin :blank)
                                             _factory
                                             (:seq identifier (:repeat (:seq "." identifier)))
                                             formal_parameter_list
                                             "="
                                             _type_not_void
                                             (:choice (:seq "." identifier) :blank))
  redirection (:seq ":" this (:choice (:seq "." _identifier_or_new) :blank) arguments)
  constructor_signature (:seq
                         (:field :name
                          (:seq identifier (:choice (:seq "." _identifier_or_new) :blank)))
                         (:field :parameters formal_parameter_list))
  constant_constructor_signature (:seq
                                  const_builtin
                                  (:seq identifier (:choice (:seq "." _identifier_or_new) :blank))
                                  formal_parameter_list)
  constructor_body (:seq
                    "{"
                    (:choice explicit_constructor_invocation :blank)
                    (:repeat _statement)
                    "}")
  explicit_constructor_invocation (:seq
                                   (:choice
                                    (:seq
                                     (:field :type_arguments (:choice type_arguments :blank))
                                     (:field :constructor (:choice this super)))
                                    (:seq
                                     (:field :object (:choice _ambiguous_name _primary))
                                     "."
                                     (:field :type_arguments (:choice type_arguments :blank))
                                     (:field :constructor super)))
                                   (:field :arguments arguments)
                                   _semicolon)
  _ambiguous_name (:choice identifier scoped_identifier)
  scoped_identifier (:seq
                     (:field :scope (:choice identifier scoped_identifier))
                     "."
                     (:field :name identifier))
  variable_declaration (:seq
                        _declared_identifier
                        (:choice
                         (:seq "," (:seq identifier (:repeat (:seq "," identifier))))
                         :blank))
  initialized_variable_definition (:seq
                                   _declared_identifier
                                   (:choice (:seq (:prec 0 "=") (:field :value _expression)) :blank)
                                   (:repeat (:seq "," initialized_identifier)))
  _declared_identifier (:seq
                        (:choice _metadata :blank)
                        (:choice _covariant :blank)
                        _final_const_var_or_type
                        (:field :name
                         (:choice
                          identifier
                          (:alias _get identifier)
                          (:alias _set identifier)
                          (:alias _operator identifier))))
  _final_const_var_or_type (:choice
                            (:seq
                             (:choice _late_builtin :blank)
                             final_builtin
                             (:choice _type :blank))
                            (:seq const_builtin (:choice _type :blank))
                            (:seq (:choice _late_builtin :blank) _var_or_type))
  _type (:choice (:seq function_type (:choice nullable_type :blank)) _type_not_function)
  _type_not_function (:choice
                      _type_not_void_not_function
                      (:seq record_type (:choice nullable_type :blank))
                      void_type)
  _type_not_void_not_function (:choice
                               (:seq
                                _type_name
                                (:choice type_arguments :blank)
                                (:choice nullable_type :blank))
                               (:seq _function_builtin_identifier (:choice nullable_type :blank)))
  function_type (:choice _function_type_tails (:seq _type_not_function _function_type_tails))
  _function_type_tails (:repeat1 _function_type_tail)
  _function_type_tail (:seq
                       _function_builtin_identifier
                       (:choice type_parameters :blank)
                       (:choice nullable_type :blank)
                       (:choice parameter_type_list :blank)
                       (:choice nullable_type :blank))
  parameter_type_list (:seq
                       "("
                       (:choice
                        (:choice
                         (:seq
                          normal_parameter_type
                          (:repeat (:seq "," normal_parameter_type))
                          (:choice "," :blank))
                         (:seq
                          (:seq normal_parameter_type (:repeat (:seq "," normal_parameter_type)))
                          ","
                          optional_parameter_types)
                         optional_parameter_types)
                        :blank)
                       ")")
  normal_parameter_type (:seq (:choice _metadata :blank) (:choice typed_identifier _type))
  optional_parameter_types (:choice optional_positional_parameter_types named_parameter_types)
  optional_positional_parameter_types (:seq
                                       "["
                                       (:seq
                                        normal_parameter_type
                                        (:repeat (:seq "," normal_parameter_type))
                                        (:choice "," :blank))
                                       "]")
  named_parameter_types (:seq
                         "{"
                         (:seq
                          _named_parameter_type
                          (:repeat (:seq "," _named_parameter_type))
                          (:choice "," :blank))
                         "}")
  _named_parameter_type (:seq
                         (:choice _metadata :blank)
                         (:choice _required :blank)
                         typed_identifier)
  _type_not_void (:choice
                  (:seq function_type (:choice nullable_type :blank))
                  (:seq record_type (:choice nullable_type :blank))
                  _type_not_void_not_function)
  record_type (:choice
               (:prec-dynamic -20 (:seq "(" ")"))
               (:seq
                "("
                (:seq record_type_field (:repeat (:seq "," record_type_field)))
                ","
                "{"
                (:seq
                 record_type_named_field
                 (:repeat (:seq "," record_type_named_field))
                 (:choice "," :blank))
                "}"
                ")")
               (:seq
                "("
                (:seq record_type_field (:repeat (:seq "," record_type_field)) (:choice "," :blank))
                ")")
               (:seq
                "("
                "{"
                (:seq
                 record_type_named_field
                 (:repeat (:seq "," record_type_named_field))
                 (:choice "," :blank))
                "}"
                ")"))
  record_type_field (:seq (:choice _metadata :blank) _type (:choice identifier :blank))
  record_type_named_field (:seq (:choice _metadata :blank) typed_identifier)
  _type_not_void_list (:seq _type_not_void (:repeat (:seq "," _type_not_void)))
  _type_name (:seq (:alias identifier type_identifier) (:choice _type_dot_identifier :blank))
  _type_dot_identifier (:prec-right 19 (:seq "." (:alias identifier type_identifier)))
  typed_identifier (:seq _type identifier)
  nullable_type (:prec 0 "?")
  nullable_selector (:prec 0 "?")
  floating_point_type (:token "double")
  boolean_type (:prec 0 "bool")
  void_type (:token "void")
  _var_or_type (:choice _type (:seq inferred_type (:choice _type :blank)))
  _final_var_or_type (:choice
                      inferred_type
                      final_builtin
                      (:seq (:choice final_builtin :blank) _type))
  inferred_type (:prec 0 "var")
  function_body (:choice
                 (:seq (:choice "async" :blank) "=>" _expression _semicolon)
                 (:seq (:choice (:choice "async" "async*" "sync*") :blank) block))
  function_expression_body (:choice
                            (:seq (:choice "async" :blank) "=>" _expression)
                            (:seq (:choice (:choice "async" "async*" "sync*") :blank) block))
  function_signature (:seq
                      (:choice _type :blank)
                      (:field :name
                       (:choice (:alias _get identifier) (:alias _set identifier) identifier))
                      _formal_parameter_part
                      (:choice _native :blank))
  _formal_parameter_part (:seq (:choice type_parameters :blank) formal_parameter_list)
  formal_parameter_list _strict_formal_parameter_list
  _strict_formal_parameter_list (:choice
                                 (:seq "(" ")")
                                 (:seq "(" _normal_formal_parameters (:choice "," :blank) ")")
                                 (:seq
                                  "("
                                  _normal_formal_parameters
                                  ","
                                  optional_formal_parameters
                                  ")")
                                 (:seq "(" optional_formal_parameters ")"))
  _normal_formal_parameters (:seq formal_parameter (:repeat (:seq "," formal_parameter)))
  optional_formal_parameters (:choice
                              _optional_postional_formal_parameters
                              _named_formal_parameters)
  positional_parameters (:seq
                         "["
                         (:seq
                          _default_formal_parameter
                          (:repeat (:seq "," _default_formal_parameter)))
                         "]")
  _optional_postional_formal_parameters (:seq
                                         "["
                                         (:seq
                                          _default_formal_parameter
                                          (:repeat (:seq "," _default_formal_parameter))
                                          (:choice "," :blank))
                                         "]")
  _named_formal_parameters (:seq
                            "{"
                            (:seq
                             _default_named_parameter
                             (:repeat (:seq "," _default_named_parameter))
                             (:choice "," :blank))
                            "}")
  formal_parameter _normal_formal_parameter
  _default_formal_parameter (:seq formal_parameter (:choice (:seq "=" _expression) :blank))
  _default_named_parameter (:choice
                            (:seq
                             (:choice _metadata :blank)
                             (:choice _required :blank)
                             formal_parameter
                             (:choice (:seq "=" _expression) :blank))
                            (:seq
                             (:choice _metadata :blank)
                             (:choice _required :blank)
                             formal_parameter
                             (:choice (:seq ":" _expression) :blank)))
  _normal_formal_parameter (:seq
                            (:choice _metadata :blank)
                            (:choice
                             _function_formal_parameter
                             _simple_formal_parameter
                             constructor_param
                             super_formal_parameter))
  _function_formal_parameter (:seq
                              (:choice _covariant :blank)
                              (:choice _type :blank)
                              identifier
                              _formal_parameter_part
                              (:choice nullable_type :blank))
  _simple_formal_parameter (:choice
                            _declared_identifier
                            (:seq
                             (:choice _covariant :blank)
                             (:choice identifier (:alias _get identifier) (:alias _set identifier))))
  super_formal_parameter (:seq
                          (:choice _final_const_var_or_type :blank)
                          super
                          "."
                          identifier
                          (:choice _formal_parameter_part :blank))
  constructor_param (:seq
                     (:choice _final_const_var_or_type :blank)
                     this
                     "."
                     identifier
                     (:choice _formal_parameter_part :blank))
  local_variable_declaration (:choice
                              (:seq
                               (:choice _metadata :blank)
                               initialized_variable_definition
                               _semicolon)
                              (:seq
                               (:choice _metadata :blank)
                               pattern_variable_declaration
                               _semicolon))
  script_tag (:seq "#!" (:pattern ".+") "\n")
  library_name (:seq
                (:choice _metadata :blank)
                "library"
                (:choice dotted_identifier_list :blank)
                _semicolon)
  dotted_identifier_list (:seq identifier (:repeat (:seq "." identifier)))
  _identifier_or_new (:choice identifier _new_builtin)
  qualified (:choice
             (:seq _type_name "." _identifier_or_new)
             (:seq _type_name "." _type_name "." _identifier_or_new))
  _as (:prec 0 "as")
  break_builtin (:token "break")
  assert_builtin (:token "assert")
  case_builtin (:token "case")
  rethrow_builtin (:token "rethrow")
  part_of_builtin (:token "part of")
  _covariant (:prec 0 "covariant")
  _deferred (:prec 0 "deferred")
  _dynamic (:prec 0 "dynamic")
  _export (:prec 0 "export")
  _external _external_builtin
  _factory (:prec 0 "factory")
  _function_builtin_identifier (:prec 0 "Function")
  _get (:prec 0 "get")
  _native (:seq "native" (:choice string_literal :blank))
  _implements (:prec 0 "implements")
  _import (:prec 0 "import")
  interface (:prec 0 "interface")
  base (:prec 0 "base")
  abstract (:prec 0 "abstract")
  sealed (:prec 0 "sealed")
  _library (:prec 0 "library")
  _operator (:prec 0 "operator")
  mixin (:prec 0 "mixin")
  _part (:prec 0 "part")
  _required (:prec 0 "required")
  _set (:prec 0 "set")
  _static (:prec 0 "static")
  _typedef (:prec 0 "typedef")
  _new_builtin (:prec 0 "new")
  logical_and_operator (:prec 0 "&&")
  logical_or_operator (:prec 0 "||")
  const_builtin (:token "const")
  final_builtin (:token "final")
  _late_builtin (:prec 0 "late")
  _external_builtin (:prec 0 "external")
  this (:prec 0 "this")
  super (:prec 0 "super")
  label (:seq
         (:choice
          identifier
          (:alias _get identifier)
          (:alias _set identifier)
          (:alias _function_builtin_identifier identifier))
         ":")
  _semicolon (:token ";")
  identifier (:pattern "[a-zA-Z_$][\\w$]*")
  identifier_dollar_escaped (:pattern "([a-zA-Z_]|(\\\\\\$))([\\w]|(\\\\\\$))*")
  comment (:choice
           _block_comment
           (:seq "//" (:pattern "([^/\\n].*)?"))
           (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/"))
  documentation_comment (:choice _documentation_block_comment (:seq "///" (:pattern ".*")))}}
