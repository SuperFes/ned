# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "nim"
 :word identifier
 :extras [(:pattern "[\\n\\r ]+")
          _synchronize
          comment
          documentation_comment
          block_comment
          block_documentation_comment]
 :conflicts [[symbol_declaration _basic_expression]]
 :precedences [["sigil"
                "suffix"
                "unary"
                "type_modifiers"
                "binary_10"
                "binary_9"
                "binary_8"
                "binary_7"
                "binary_6"
                "binary_5"
                "binary_4"
                "binary_3"
                "binary_2"
                "binary_1"
                "binary_0"
                _expression
                _simple_expression_command_start
                type_expression
                pragma_expression]
               ["proc_type"]
               ["post_expr" _basic_expression]
               ["post_expr" _simple_expression_command_start]
               ["post_expr" _simple_expression]
               ["post_expr" _expression_statement]
               [enum_declaration enum_type]
               [object_declaration object_type]
               [_prefix_expression _simple_expression_command_start]
               [_simple_expression _command_expression]
               [_expression_with_call_do equal_expression]
               [_expression _command_expression]
               [_left_hand_side _expression]
               [_simple_expression _command_expression_argument_list]
               [_simple_expression_command_start _command_expression]
               [_call_do_argument_list _call_expression]
               [_call_do _simple_expression_command_start]
               [_call_do_argument_list _post_expression_block]
               [_call_block_argument_list _call_expression]
               [_call_block _simple_expression_command_start]
               [_command_block_argument_list _command_expression]
               [_simple_expression_command_start _command_statement]
               [_simple_expression_command_start _command_complex_expression]
               [_equal_expression_list _command_complex_expression_argument_list]
               ["proc_expr" _basic_expression]
               [_type_definition "type_modifiers"]]
 :externals [_block_comment_content
             _block_documentation_comment_content
             comment_content
             _long_string_quote
             _layout_start
             _layout_end
             _layout_terminator
             _layout_empty
             _inhibit_layout_end
             _inhibit_keyword_termination
             ","
             _synchronize
             _invalid_layout
             _sigil_operator
             _prefix_operator
             _want_export_marker
             _case_of]
 :inline []
 :supertypes []
 :rules
 {source_file (:choice _semi_statement_list :blank)
  statement_list (:choice _block_statement_list _line_statement_list _layout_empty)
  _line_statement_list (:prec-right 0
                        (:seq _simple_statement (:repeat (:seq ";" _simple_statement))))
  _block_statement_list (:seq _layout_start (:choice _semi_statement_list :blank) _layout_end)
  _semi_statement_list (:repeat1 (:seq _statement (:choice ";" _layout_terminator)))
  _statement (:choice _simple_statement _complex_statement)
  _complex_statement (:choice
                      while
                      static_statement
                      defer
                      typeof
                      (:alias _infix_typeof_expression infix_expression)
                      _declaration)
  _simple_statement (:choice _expression_statement _simple_statement_no_expression)
  _simple_statement_no_expression (:choice
                                   import_statement
                                   import_from_statement
                                   export_statement
                                   include_statement
                                   discard_statement
                                   return_statement
                                   raise_statement
                                   yield_statement
                                   break_statement
                                   continue_statement
                                   assembly_statement
                                   bind_statement
                                   mixin_statement
                                   pragma_statement)
  _expression_statement (:choice
                         _expression
                         assignment
                         (:alias _call_extended call)
                         (:alias _infix_extended infix_expression)
                         (:alias _prefix_extended prefix_expression))
  import_statement (:prec-right 0
                    (:seq
                     (:alias
                      (:token (:prec 1 (:pattern "i_?[mM]_?[pP]_?[oO]_?[rR]_?[tT]")))
                      "import")
                     _import_body))
  export_statement (:prec-right 0
                    (:seq
                     (:alias
                      (:token (:prec 1 (:pattern "e_?[xX]_?[pP]_?[oO]_?[rR]_?[tT]")))
                      "export")
                     _import_body))
  _import_body (:choice expression_list _import_except)
  _import_except (:seq _expression (:choice _inhibit_keyword_termination :blank) except_clause)
  except_clause (:seq
                 (:alias (:token (:prec 1 (:pattern "e_?[xX]_?[cC]_?[eE]_?[pP]_?[tT]"))) "except")
                 expression_list)
  include_statement (:seq
                     (:alias
                      (:token (:prec 1 (:pattern "i_?[nN]_?[cC]_?[lL]_?[uU]_?[dD]_?[eE]")))
                      "include")
                     expression_list)
  discard_statement (:prec-right 0
                     (:seq
                      (:alias
                       (:token (:prec 1 (:pattern "d_?[iI]_?[sS]_?[cC]_?[aA]_?[rR]_?[dD]")))
                       "discard")
                      (:choice _expression_with_post_block :blank)))
  return_statement (:prec-right 0
                    (:seq
                     (:alias
                      (:token (:prec 1 (:pattern "r_?[eE]_?[tT]_?[uU]_?[rR]_?[nN]")))
                      "return")
                     (:choice _expression_with_post_block :blank)))
  raise_statement (:prec-right 0
                   (:seq
                    (:alias (:token (:prec 1 (:pattern "r_?[aA]_?[iI]_?[sS]_?[eE]"))) "raise")
                    (:choice _expression_with_post_block :blank)))
  yield_statement (:prec-right 0
                   (:seq
                    (:alias (:token (:prec 1 (:pattern "y_?[iI]_?[eE]_?[lL]_?[dD]"))) "yield")
                    (:choice _expression_with_post_block :blank)))
  break_statement (:prec-right 0
                   (:seq
                    (:alias (:token (:prec 1 (:pattern "b_?[rR]_?[eE]_?[aA]_?[kK]"))) "break")
                    (:choice _expression_with_post_block :blank)))
  continue_statement (:prec-right 0
                      (:seq
                       (:alias
                        (:token (:prec 1 (:pattern "c_?[oO]_?[nN]_?[tT]_?[iI]_?[nN]_?[uU]_?[eE]")))
                        "continue")
                       (:choice _expression_with_post_block :blank)))
  assembly_statement (:seq
                      (:alias (:token (:prec 1 (:pattern "a_?[sS]_?[mM]"))) "asm")
                      (:choice (:field :pragma pragma_list) :blank)
                      _string_literal)
  bind_statement (:seq
                  (:alias (:token (:prec 1 (:pattern "b_?[iI]_?[nN]_?[dD]"))) "bind")
                  expression_list)
  mixin_statement (:seq
                   (:alias (:token (:prec 1 (:pattern "m_?[iI]_?[xX]_?[iI]_?[nN]"))) "mixin")
                   expression_list)
  import_from_statement (:seq
                         (:alias (:token (:prec 0 (:pattern "f_?[rR]_?[oO]_?[mM]"))) "from")
                         (:field :module _expression)
                         (:alias
                          (:token (:prec 1 (:pattern "i_?[mM]_?[pP]_?[oO]_?[rR]_?[tT]")))
                          "import")
                         expression_list)
  while (:seq
         (:alias (:token (:prec 1 (:pattern "w_?[hH]_?[iI]_?[lL]_?[eE]"))) "while")
         (:field :condition _simple_expression)
         ":"
         (:field :body statement_list))
  static_statement (:seq
                    (:alias
                     (:token (:prec 1 (:pattern "s_?[tT]_?[aA]_?[tT]_?[iI]_?[cC]")))
                     "static")
                    ":"
                    (:field :body statement_list))
  pragma_statement (:prec-right 0
                    (:seq pragma_list (:choice (:seq ":" (:field :body statement_list)) :blank)))
  defer (:seq
         (:alias (:token (:prec 1 (:pattern "d_?[eE]_?[fF]_?[eE]_?[rR]"))) "defer")
         ":"
         (:field :body statement_list))
  _typeof_expression (:choice typeof (:alias _infix_typeof_expression infix_expression))
  typeof (:seq
          (:alias (:token (:prec 1 (:pattern "t_?[yY]_?[pP]_?[eE]"))) "type")
          "("
          _simple_expression
          _paren_close)
  _infix_typeof_expression (:choice
                            (:prec-right "binary_10"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_10r)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_10"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_10l)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_9"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_9)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_8"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_8)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_7"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_7)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_6"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_6)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_5"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_5)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_4"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_4)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_3"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_3)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_2"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_2)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_1"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_1)
                              (:field :right (:choice _typeof_expression _simple_expression))))
                            (:prec-left "binary_0"
                             (:seq
                              (:field :left _typeof_expression)
                              (:field :operator _infix_operator_0)
                              (:field :right (:choice _typeof_expression _simple_expression)))))
  _declaration (:choice
                proc_declaration
                func_declaration
                method_declaration
                iterator_declaration
                macro_declaration
                template_declaration
                converter_declaration
                using_section
                const_section
                let_section
                var_section
                type_section)
  proc_declaration (:seq
                    (:alias (:token (:prec 1 (:pattern "p_?[rR]_?[oO]_?[cC]"))) "proc")
                    _routine_declaration)
  func_declaration (:seq
                    (:alias (:token (:prec 1 (:pattern "f_?[uU]_?[nN]_?[cC]"))) "func")
                    _routine_declaration)
  method_declaration (:seq
                      (:alias
                       (:token (:prec 1 (:pattern "m_?[eE]_?[tT]_?[hH]_?[oO]_?[dD]")))
                       "method")
                      _routine_declaration)
  iterator_declaration (:seq
                        (:alias
                         (:token (:prec 1 (:pattern "i_?[tT]_?[eE]_?[rR]_?[aA]_?[tT]_?[oO]_?[rR]")))
                         "iterator")
                        _routine_declaration)
  macro_declaration (:seq
                     (:alias (:token (:prec 1 (:pattern "m_?[aA]_?[cC]_?[rR]_?[oO]"))) "macro")
                     _routine_declaration)
  template_declaration (:seq
                        (:alias
                         (:token (:prec 1 (:pattern "t_?[eE]_?[mM]_?[pP]_?[lL]_?[aA]_?[tT]_?[eE]")))
                         "template")
                        _routine_declaration)
  converter_declaration (:seq
                         (:alias
                          (:token
                           (:prec 1 (:pattern "c_?[oO]_?[nN]_?[vV]_?[eE]_?[rR]_?[tT]_?[eE]_?[rR]")))
                          "converter")
                         _routine_declaration)
  _routine_declaration (:seq
                        (:field :name (:choice _symbol exported_symbol))
                        (:field :rewrite_pattern (:choice term_rewriting_pattern :blank))
                        (:field :generic_parameters (:choice generic_parameter_list :blank))
                        (:seq
                         (:field :parameters (:choice parameter_declaration_list :blank))
                         (:choice (:seq ":" (:field :return_type type_expression)) :blank)
                         (:field :pragmas (:choice pragma_list :blank)))
                        (:choice (:seq "=" (:field :body statement_list)) :blank))
  generic_parameter_list (:seq "[" (:choice _parameter_declaration_list :blank) _bracket_close)
  term_rewriting_pattern (:seq "{" (:alias _semi_statement_list statement_list) _curly_close)
  using_section (:seq
                 (:alias (:token (:prec 1 (:pattern "u_?[sS]_?[iI]_?[nN]_?[gG]"))) "using")
                 _variable_declaration_section)
  const_section (:seq
                 (:alias (:token (:prec 1 (:pattern "c_?[oO]_?[nN]_?[sS]_?[tT]"))) "const")
                 _variable_declaration_section)
  let_section (:seq
               (:alias (:token (:prec 1 (:pattern "l_?[eE]_?[tT]"))) "let")
               _variable_declaration_section)
  var_section (:prec-dynamic 1
               (:seq
                (:alias (:token (:prec 1 (:pattern "v_?[aA]_?[rR]"))) "var")
                _variable_declaration_section))
  _variable_declaration_section (:choice
                                 variable_declaration
                                 (:seq
                                  _layout_start
                                  (:repeat (:seq variable_declaration _layout_terminator))
                                  _layout_end))
  variable_declaration (:choice
                        (:seq
                         symbol_declaration_list
                         (:seq ":" (:field :type type_expression))
                         (:choice (:seq "=" (:field :value _expression_with_post_block)) :blank))
                        (:seq
                         symbol_declaration_list
                         (:choice (:seq "=" (:field :value _expression_with_post_block)) :blank)))
  type_section (:seq
                (:alias (:token (:prec 1 (:pattern "t_?[yY]_?[pP]_?[eE]"))) "type")
                (:choice
                 type_declaration
                 (:seq
                  _layout_start
                  (:repeat (:seq type_declaration _layout_terminator))
                  _layout_end)))
  type_declaration (:seq type_symbol_declaration (:choice (:seq "=" _type_definition) :blank))
  type_symbol_declaration (:seq
                           (:field :name (:choice _symbol exported_symbol))
                           (:choice generic_parameter_list :blank)
                           (:choice (:field :pragma pragma_list) :blank))
  _type_definition (:choice
                    type_expression
                    enum_declaration
                    object_declaration
                    concept_declaration
                    (:alias _distinct_declaration distinct_type)
                    (:alias _ref_declaration ref_type)
                    (:alias _pointer_declaration pointer_type)
                    (:alias _tuple_declaration tuple_type)
                    (:alias _call_extended call))
  _distinct_declaration (:seq
                         (:alias
                          (:token
                           (:prec 1 (:pattern "d_?[iI]_?[sS]_?[tT]_?[iI]_?[nN]_?[cC]_?[tT]")))
                          "distinct")
                         _type_definition)
  _ref_declaration (:seq
                    (:alias (:token (:prec 1 (:pattern "r_?[eE]_?[fF]"))) "ref")
                    _type_definition)
  _pointer_declaration (:seq
                        (:alias (:token (:prec 1 (:pattern "p_?[tT]_?[rR]"))) "ptr")
                        _type_definition)
  enum_declaration (:seq
                    (:alias (:token (:prec 1 (:pattern "e_?[nN]_?[uU]_?[mM]"))) "enum")
                    (:choice
                     (:repeat1 (:seq enum_field_declaration (:choice "," :blank)))
                     (:seq
                      _layout_start
                      (:repeat
                       (:seq
                        (:repeat1 (:seq enum_field_declaration (:choice "," :blank)))
                        _layout_terminator))
                      _layout_end)))
  enum_field_declaration (:prec-right 0
                          (:seq
                           symbol_declaration
                           (:choice (:seq "=" (:field :value _expression)) :blank)))
  _tuple_declaration (:seq
                      (:alias (:token (:prec 1 (:pattern "t_?[uU]_?[pP]_?[lL]_?[eE]"))) "tuple")
                      (:seq
                       _layout_start
                       (:repeat1
                        (:seq (:alias _identifier_declaration field_declaration) _layout_terminator))
                       _layout_end))
  object_declaration (:seq
                      (:alias
                       (:token (:prec 1 (:pattern "o_?[bB]_?[jJ]_?[eE]_?[cC]_?[tT]")))
                       "object")
                      (:choice (:field :pragma pragma_list) :blank)
                      (:choice
                       (:seq
                        (:alias (:token (:prec 1 (:pattern "o_?[fF]"))) "of")
                        (:field :inherits type_expression))
                       :blank)
                      (:choice
                       (:alias _object_field_declaration_list field_declaration_list)
                       :blank))
  _object_field_declaration_branch_list (:choice
                                         _object_field_declaration
                                         _object_field_declaration_list
                                         _layout_empty)
  _object_field_declaration_list (:seq
                                  _layout_start
                                  (:repeat (:seq _object_field_declaration _layout_terminator))
                                  _layout_end)
  _object_field_declaration (:choice
                             (:alias _identifier_declaration field_declaration)
                             conditional_declaration
                             variant_declaration
                             nil_literal
                             (:alias
                              (:alias
                               (:token (:prec 1 (:pattern "d_?[iI]_?[sS]_?[cC]_?[aA]_?[rR]_?[dD]")))
                               "discard")
                              discard_statement))
  conditional_declaration (:prec-right 0
                           (:seq
                            (:alias (:token (:prec 1 (:pattern "w_?[hH]_?[eE]_?[nN]"))) "when")
                            (:field :condition _expression)
                            ":"
                            (:field :consequence
                             (:alias _object_field_declaration_branch_list field_declaration_list))
                            (:repeat
                             (:field :alternative
                              (:choice
                               (:alias _elif_declaration_branch elif_branch)
                               (:alias _else_declaration_branch else_branch)
                               _inhibit_keyword_termination)))))
  _elif_declaration_branch (:seq
                            (:alias (:token (:prec 1 (:pattern "e_?[lL]_?[iI]_?[fF]"))) "elif")
                            (:field :condition _expression)
                            ":"
                            (:field :consequence
                             (:alias _object_field_declaration_branch_list field_declaration_list)))
  variant_declaration (:seq
                       (:alias (:token (:prec 1 (:pattern "c_?[aA]_?[sS]_?[eE]"))) "case")
                       _variant_declaration_body)
  _variant_declaration_body (:prec-right 0
                             (:seq
                              variant_discriminator_declaration
                              (:choice ":" :blank)
                              (:repeat
                               (:field :alternative (:alias _of_declaration_branch of_branch)))
                              (:choice _inhibit_keyword_termination :blank)
                              (:choice
                               (:field :alternative (:alias _else_declaration_branch else_branch))
                               :blank)))
  variant_discriminator_declaration _identifier_declaration
  _of_declaration_branch (:seq
                          (:alias _case_of "of")
                          (:field :values expression_list)
                          ":"
                          (:field :consequence
                           (:alias _object_field_declaration_branch_list field_declaration_list)))
  _else_declaration_branch (:seq
                            (:alias (:token (:prec 1 (:pattern "e_?[lL]_?[sS]_?[eE]"))) "else")
                            ":"
                            (:field :consequence
                             (:alias _object_field_declaration_branch_list field_declaration_list)))
  concept_declaration (:seq
                       (:alias
                        (:token (:prec 1 (:pattern "c_?[oO]_?[nN]_?[cC]_?[eE]_?[pP]_?[tT]")))
                        "concept")
                       (:field :parameters
                        (:choice (:alias _concept_parameter_list parameter_list) :blank))
                       (:choice
                        (:seq
                         (:alias (:token (:prec 1 (:pattern "o_?[fF]"))) "of")
                         (:field :refines refinement_list))
                        :blank)
                       (:choice (:field :body (:alias _block_statement_list statement_list)) :blank))
  refinement_list (:seq type_expression (:repeat (:seq "," type_expression)))
  _concept_parameter_list (:seq _concept_parameter (:repeat (:seq "," _concept_parameter)))
  _concept_parameter (:choice
                      _symbol
                      (:alias _concept_pointer_parameter pointer_parameter)
                      (:alias _concept_ref_parameter ref_parameter)
                      (:alias _concept_static_parameter static_parameter)
                      (:alias _concept_type_parameter type_parameter)
                      (:alias _concept_var_parameter var_parameter))
  _concept_pointer_parameter (:seq
                              (:alias (:token (:prec 1 (:pattern "p_?[tT]_?[rR]"))) "ptr")
                              _symbol)
  _concept_ref_parameter (:seq (:alias (:token (:prec 1 (:pattern "r_?[eE]_?[fF]"))) "ref") _symbol)
  _concept_static_parameter (:seq
                             (:alias
                              (:token (:prec 1 (:pattern "s_?[tT]_?[aA]_?[tT]_?[iI]_?[cC]")))
                              "static")
                             _symbol)
  _concept_type_parameter (:seq
                           (:alias (:token (:prec 1 (:pattern "t_?[yY]_?[pP]_?[eE]"))) "type")
                           _symbol)
  _concept_var_parameter (:seq (:alias (:token (:prec 1 (:pattern "v_?[aA]_?[rR]"))) "var") _symbol)
  _expression_with_post_block (:choice
                               _expression_with_call_do
                               (:alias _call_block call)
                               (:alias _command_block call)
                               (:alias _dot_generic_call_block dot_generic_call)
                               (:alias _infix_extended infix_expression)
                               (:alias _prefix_extended prefix_expression))
  _expression_with_call_do (:choice
                            _expression
                            (:alias _call_do call)
                            (:alias _command_complex_expression call)
                            (:alias _dot_generic_call_do dot_generic_call))
  _expression (:choice
               _simple_expression
               proc_expression
               func_expression
               iterator_expression
               block
               if
               when
               case
               try
               for)
  _simple_expression (:choice
                      _simple_expression_command_start
                      (:alias _prefix_expression prefix_expression))
  _simple_expression_command_start (:choice
                                    _basic_expression
                                    (:alias _command_expression call)
                                    (:alias _infix_expression infix_expression)
                                    (:alias _prefix_expression_command_start prefix_expression))
  _basic_expression (:choice
                     _literal
                     _symbol
                     array_construction
                     curly_construction
                     tuple_construction
                     cast
                     parenthesized
                     dot_expression
                     bracket_expression
                     curly_expression
                     generalized_string
                     pragma_expression
                     object_type
                     tuple_type
                     enum_type
                     var_type
                     out_type
                     distinct_type
                     ref_type
                     pointer_type
                     dot_generic_call
                     (:alias _proc_type proc_type)
                     (:alias _iterator_type iterator_type)
                     (:alias _call_expression call)
                     (:alias _sigil_expression prefix_expression))
  for (:seq
       (:alias (:token (:prec 1 (:pattern "f_?[oO]_?[rR]"))) "for")
       _for_body
       ":"
       (:field :body statement_list))
  _for_body (:seq
             (:field :left symbol_declaration_list)
             (:alias (:token (:prec 1 (:pattern "i_?[nN]"))) "in")
             (:field :right _expression))
  block (:seq
         (:alias (:token (:prec 1 (:pattern "b_?[lL]_?[oO]_?[cC]_?[kK]"))) "block")
         (:choice (:field :label _symbol) :blank)
         ":"
         (:field :body statement_list))
  if (:seq
      (:alias (:token (:prec 1 (:pattern "i_?[fF]"))) "if")
      (:field :condition _expression)
      _if_body)
  when (:seq
        (:alias (:token (:prec 1 (:pattern "w_?[hH]_?[eE]_?[nN]"))) "when")
        (:field :condition _expression)
        _if_body)
  _if_body (:prec-right 0
            (:seq ":" (:field :consequence statement_list) (:choice _if_alternatives :blank)))
  _if_branch (:choice elif_branch else_branch)
  _if_alternatives (:repeat1
                    (:choice _inhibit_keyword_termination (:field :alternative _if_branch)))
  case (:prec-right 0
        (:seq
         (:alias (:token (:prec 1 (:pattern "c_?[aA]_?[sS]_?[eE]"))) "case")
         (:field :value _expression)
         (:choice ":" :blank)
         (:repeat (:field :alternative of_branch))
         (:choice _if_alternatives :blank)))
  try (:prec-right 0
       (:seq
        (:alias (:token (:prec 1 (:pattern "t_?[rR]_?[yY]"))) "try")
        ":"
        (:field :body statement_list)
        (:repeat (:choice _inhibit_keyword_termination _try_branch))))
  _try_branch (:choice except_branch finally_branch)
  of_branch (:seq
             (:alias _case_of "of")
             (:field :values expression_list)
             ":"
             (:field :consequence statement_list))
  elif_branch (:seq
               (:alias (:token (:prec 1 (:pattern "e_?[lL]_?[iI]_?[fF]"))) "elif")
               (:field :condition _expression)
               ":"
               (:field :consequence statement_list))
  else_branch (:seq
               (:alias (:token (:prec 1 (:pattern "e_?[lL]_?[sS]_?[eE]"))) "else")
               ":"
               (:field :consequence statement_list))
  except_branch (:seq
                 (:alias (:token (:prec 1 (:pattern "e_?[xX]_?[cC]_?[eE]_?[pP]_?[tT]"))) "except")
                 (:choice (:field :values expression_list) :blank)
                 ":"
                 (:field :consequence statement_list))
  finally_branch (:seq
                  (:alias
                   (:token (:prec 1 (:pattern "f_?[iI]_?[nN]_?[aA]_?[lL]_?[lL]_?[yY]")))
                   "finally")
                  ":"
                  (:field :body statement_list))
  do_block (:seq
            (:alias (:token (:prec 1 (:pattern "d_?[oO]"))) "do")
            (:field :parameters (:choice parameter_declaration_list :blank))
            (:choice (:seq "->" (:field :return_type type_expression)) :blank)
            (:field :pragmas (:choice pragma_list :blank))
            ":"
            (:field :body statement_list))
  _call_extended (:choice _command_statement _call_block _dot_generic_call_block)
  _command_statement (:seq
                      (:field :function _basic_expression)
                      (:alias _command_statement_argument_list argument_list))
  _command_statement_argument_list (:prec-right 0
                                    (:seq
                                     _equal_expression_list
                                     (:choice
                                      (:seq
                                       ":"
                                       statement_list
                                       (:choice _post_expression_block_tail :blank))
                                      :blank)))
  _command_block (:seq
                  (:field :function _basic_expression)
                  (:alias _command_block_argument_list argument_list))
  _command_block_argument_list (:prec-right 0
                                (:seq
                                 _command_expression_argument_list
                                 ":"
                                 statement_list
                                 (:choice _post_expression_block_tail :blank)))
  _dot_generic_call_block (:seq _dot_generic_head (:alias _call_block_argument_list argument_list))
  _call_block (:seq
               (:field :function _basic_expression)
               (:alias _call_block_argument_list argument_list))
  _call_block_argument_list (:seq (:choice _call_argument_list :blank) _post_expression_block)
  _dot_generic_call_do (:seq _dot_generic_head (:alias _call_do_argument_list argument_list))
  _call_do (:seq (:field :function _basic_expression) (:alias _call_do_argument_list argument_list))
  _call_do_argument_list (:prec-right 0
                          (:seq
                           (:choice _call_argument_list :blank)
                           do_block
                           (:choice _post_expression_block_tail :blank)))
  _call_expression (:prec "suffix"
                    (:seq
                     (:field :function _basic_expression)
                     (:alias _call_argument_list argument_list)))
  _call_argument_list (:seq
                       (:token-immediate "(")
                       (:choice _colon_equal_expression_list :blank)
                       _paren_close)
  _command_complex_expression (:seq
                               (:field :function _basic_expression)
                               (:alias _command_complex_expression_argument_list argument_list))
  _command_complex_expression_argument_list _expression_with_call_do
  _command_expression (:seq
                       (:field :function _basic_expression)
                       (:alias _command_expression_argument_list argument_list))
  _command_expression_argument_list (:choice _simple_expression_command_start)
  dot_generic_call (:prec-right 0
                    (:seq
                     _dot_generic_head
                     (:choice (:alias _call_argument_list argument_list) :blank)))
  _dot_generic_head (:prec "suffix"
                     (:seq
                      (:field :first_argument _basic_expression)
                      "."
                      (:field :function _symbol)
                      (:field :generic_arguments
                       (:alias _dot_generic_argument_list generic_argument_list))))
  _dot_generic_argument_list (:seq
                              (:token-immediate "[:")
                              (:seq _expression (:repeat (:seq "," _expression)))
                              _bracket_close)
  _post_expression_block (:prec-right 0
                          (:seq
                           (:choice (:seq ":" statement_list) do_block)
                           (:choice _post_expression_block_tail :blank)))
  _post_expression_block_tail (:repeat1
                               (:seq
                                (:choice
                                 _if_branch
                                 of_branch
                                 _try_branch
                                 do_block
                                 _inhibit_keyword_termination)))
  proc_expression (:prec "proc_expr" (:seq _proc_type "=" (:field :body statement_list)))
  func_expression (:prec "proc_expr" (:seq _func_type "=" (:field :body statement_list)))
  iterator_expression (:prec "proc_expr" (:seq _iterator_type "=" (:field :body statement_list)))
  type_expression (:choice _simple_expression)
  object_type (:alias (:token (:prec 1 (:pattern "o_?[bB]_?[jJ]_?[eE]_?[cC]_?[tT]"))) "object")
  enum_type (:alias (:token (:prec 1 (:pattern "e_?[nN]_?[uU]_?[mM]"))) "enum")
  tuple_type (:prec-right 0
              (:seq
               (:alias (:token (:prec 1 (:pattern "t_?[uU]_?[pP]_?[lL]_?[eE]"))) "tuple")
               (:choice (:alias _tuple_field_declaration_list field_declaration_list) :blank)))
  var_type (:prec-right "type_modifiers"
            (:seq
             (:alias (:token (:prec 1 (:pattern "v_?[aA]_?[rR]"))) "var")
             (:choice type_expression :blank)))
  out_type (:prec-right "type_modifiers"
            (:seq
             (:alias (:token (:prec 1 (:pattern "o_?[uU]_?[tT]"))) "out")
             (:choice type_expression :blank)))
  distinct_type (:prec-right "type_modifiers"
                 (:seq
                  (:alias
                   (:token (:prec 1 (:pattern "d_?[iI]_?[sS]_?[tT]_?[iI]_?[nN]_?[cC]_?[tT]")))
                   "distinct")
                  (:choice type_expression :blank)))
  ref_type (:prec-right "type_modifiers"
            (:seq
             (:alias (:token (:prec 1 (:pattern "r_?[eE]_?[fF]"))) "ref")
             (:choice type_expression :blank)))
  pointer_type (:prec-right "type_modifiers"
                (:seq
                 (:alias (:token (:prec 1 (:pattern "p_?[tT]_?[rR]"))) "ptr")
                 (:choice type_expression :blank)))
  _tuple_field_declaration_list (:seq
                                 (:choice "[" (:token-immediate "["))
                                 (:choice _field_declaration_list :blank)
                                 _bracket_close)
  _proc_type (:prec-right 0
              (:seq
               (:alias (:token (:prec 1 (:pattern "p_?[rR]_?[oO]_?[cC]"))) "proc")
               (:seq
                (:field :parameters (:choice parameter_declaration_list :blank))
                (:choice (:seq ":" (:field :return_type type_expression)) :blank)
                (:field :pragmas (:choice pragma_list :blank)))))
  _iterator_type (:prec-right 0
                  (:seq
                   (:alias
                    (:token (:prec 1 (:pattern "i_?[tT]_?[eE]_?[rR]_?[aA]_?[tT]_?[oO]_?[rR]")))
                    "iterator")
                   (:seq
                    (:field :parameters (:choice parameter_declaration_list :blank))
                    (:choice (:seq ":" (:field :return_type type_expression)) :blank)
                    (:field :pragmas (:choice pragma_list :blank)))))
  _func_type (:prec-right 0
              (:seq
               (:alias (:token (:prec 1 (:pattern "f_?[uU]_?[nN]_?[cC]"))) "func")
               (:seq
                (:field :parameters (:choice parameter_declaration_list :blank))
                (:choice (:seq ":" (:field :return_type type_expression)) :blank)
                (:field :pragmas (:choice pragma_list :blank)))))
  _infix_extended (:prec "post_expr" (:seq _infix_expression _post_expression_block))
  _infix_expression (:choice
                     (:prec-right "binary_10"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_10r)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_10"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_10l)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_9"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_9)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_8"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_8)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_7"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_7)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_6"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_6)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_5"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_5)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_4"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_4)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_3"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_3)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_2"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_2)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_1"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_1)
                       (:field :right _simple_expression)))
                     (:prec-left "binary_0"
                      (:seq
                       (:field :left _simple_expression)
                       (:field :operator _infix_operator_0)
                       (:field :right _simple_expression))))
  _infix_operator_0 (:alias
                     (:token
                      (:seq
                       (:repeat
                        (:choice
                         "="
                         "+"
                         "-"
                         "*"
                         "/"
                         "<"
                         ">"
                         "@"
                         "$"
                         "~"
                         "&"
                         "%"
                         "|"
                         "!"
                         "?"
                         "^"
                         "."
                         ":"
                         "\\"
                         "∙"
                         "∘"
                         "×"
                         "★"
                         "⊗"
                         "⊘"
                         "⊙"
                         "⊛"
                         "⊠"
                         "⊡"
                         "∩"
                         "∧"
                         "⊓"
                         "±"
                         "⊕"
                         "⊖"
                         "⊞"
                         "⊟"
                         "∪"
                         "∨"
                         "⊔"))
                       (:choice "=" "-" "~")
                       ">"))
                     operator)
  _infix_operator_1 (:alias
                     (:token
                      (:seq
                       (:repeat1
                        (:choice
                         "+"
                         "-"
                         "*"
                         "/"
                         "@"
                         "$"
                         "&"
                         "%"
                         "|"
                         "^"
                         "."
                         ":"
                         "\\"
                         "∙"
                         "∘"
                         "×"
                         "★"
                         "⊗"
                         "⊘"
                         "⊙"
                         "⊛"
                         "⊠"
                         "⊡"
                         "∩"
                         "∧"
                         "⊓"
                         "±"
                         "⊕"
                         "⊖"
                         "⊞"
                         "⊟"
                         "∪"
                         "∨"
                         "⊔"))
                       "="))
                     operator)
  _infix_operator_10r (:alias
                       (:token
                        (:seq
                         "^"
                         (:repeat
                          (:choice
                           "="
                           "+"
                           "-"
                           "*"
                           "/"
                           "<"
                           ">"
                           "@"
                           "$"
                           "~"
                           "&"
                           "%"
                           "|"
                           "!"
                           "?"
                           "^"
                           "."
                           ":"
                           "\\"
                           "∙"
                           "∘"
                           "×"
                           "★"
                           "⊗"
                           "⊘"
                           "⊙"
                           "⊛"
                           "⊠"
                           "⊡"
                           "∩"
                           "∧"
                           "⊓"
                           "±"
                           "⊕"
                           "⊖"
                           "⊞"
                           "⊟"
                           "∪"
                           "∨"
                           "⊔"))))
                       operator)
  _infix_operator_10l (:alias
                       (:token
                        (:seq
                         "$"
                         (:repeat
                          (:choice
                           "="
                           "+"
                           "-"
                           "*"
                           "/"
                           "<"
                           ">"
                           "@"
                           "$"
                           "~"
                           "&"
                           "%"
                           "|"
                           "!"
                           "?"
                           "^"
                           "."
                           ":"
                           "\\"
                           "∙"
                           "∘"
                           "×"
                           "★"
                           "⊗"
                           "⊘"
                           "⊙"
                           "⊛"
                           "⊠"
                           "⊡"
                           "∩"
                           "∧"
                           "⊓"
                           "±"
                           "⊕"
                           "⊖"
                           "⊞"
                           "⊟"
                           "∪"
                           "∨"
                           "⊔"))))
                       operator)
  _infix_operator_9 (:alias
                     (:choice
                      (:token
                       (:seq
                        (:choice
                         "%"
                         "\\"
                         "/"
                         "∙"
                         "∘"
                         "×"
                         "★"
                         "⊗"
                         "⊘"
                         "⊙"
                         "⊛"
                         "⊠"
                         "⊡"
                         "∩"
                         "∧"
                         "⊓")
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:token
                       (:seq
                        "*"
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:alias (:token (:prec 1 (:pattern "d_?[iI]_?[vV]"))) "div")
                      (:alias (:token (:prec 1 (:pattern "m_?[oO]_?[dD]"))) "mod")
                      (:alias (:token (:prec 1 (:pattern "s_?[hH]_?[lL]"))) "shl")
                      (:alias (:token (:prec 1 (:pattern "s_?[hH]_?[rR]"))) "shr"))
                     operator)
  _infix_operator_8 (:alias
                     (:token
                      (:seq
                       (:choice "+" "-" "~" "|" "±" "⊕" "⊖" "⊞" "⊟" "∪" "∨" "⊔")
                       (:repeat
                        (:choice
                         "="
                         "+"
                         "-"
                         "*"
                         "/"
                         "<"
                         ">"
                         "@"
                         "$"
                         "~"
                         "&"
                         "%"
                         "|"
                         "!"
                         "?"
                         "^"
                         "."
                         ":"
                         "\\"
                         "∙"
                         "∘"
                         "×"
                         "★"
                         "⊗"
                         "⊘"
                         "⊙"
                         "⊛"
                         "⊠"
                         "⊡"
                         "∩"
                         "∧"
                         "⊓"
                         "±"
                         "⊕"
                         "⊖"
                         "⊞"
                         "⊟"
                         "∪"
                         "∨"
                         "⊔"))))
                     operator)
  _infix_operator_7 (:alias
                     (:token
                      (:seq
                       "&"
                       (:repeat
                        (:choice
                         "="
                         "+"
                         "-"
                         "*"
                         "/"
                         "<"
                         ">"
                         "@"
                         "$"
                         "~"
                         "&"
                         "%"
                         "|"
                         "!"
                         "?"
                         "^"
                         "."
                         ":"
                         "\\"
                         "∙"
                         "∘"
                         "×"
                         "★"
                         "⊗"
                         "⊘"
                         "⊙"
                         "⊛"
                         "⊠"
                         "⊡"
                         "∩"
                         "∧"
                         "⊓"
                         "±"
                         "⊕"
                         "⊖"
                         "⊞"
                         "⊟"
                         "∪"
                         "∨"
                         "⊔"))))
                     operator)
  _infix_operator_6 (:alias
                     (:token
                      (:seq
                       "."
                       (:repeat1
                        (:choice
                         "="
                         "+"
                         "-"
                         "*"
                         "/"
                         "<"
                         ">"
                         "@"
                         "$"
                         "~"
                         "&"
                         "%"
                         "|"
                         "!"
                         "?"
                         "^"
                         "."
                         ":"
                         "\\"
                         "∙"
                         "∘"
                         "×"
                         "★"
                         "⊗"
                         "⊘"
                         "⊙"
                         "⊛"
                         "⊠"
                         "⊡"
                         "∩"
                         "∧"
                         "⊓"
                         "±"
                         "⊕"
                         "⊖"
                         "⊞"
                         "⊟"
                         "∪"
                         "∨"
                         "⊔"))))
                     operator)
  _infix_operator_5 (:alias
                     (:choice
                      (:token
                       (:choice
                        (:seq
                         "="
                         (:repeat1
                          (:choice
                           "="
                           "+"
                           "-"
                           "*"
                           "/"
                           "<"
                           ">"
                           "@"
                           "$"
                           "~"
                           "&"
                           "%"
                           "|"
                           "!"
                           "?"
                           "^"
                           "."
                           ":"
                           "\\"
                           "∙"
                           "∘"
                           "×"
                           "★"
                           "⊗"
                           "⊘"
                           "⊙"
                           "⊛"
                           "⊠"
                           "⊡"
                           "∩"
                           "∧"
                           "⊓"
                           "±"
                           "⊕"
                           "⊖"
                           "⊞"
                           "⊟"
                           "∪"
                           "∨"
                           "⊔")))
                        (:seq
                         (:choice "<" ">" "!")
                         (:repeat
                          (:choice
                           "="
                           "+"
                           "-"
                           "*"
                           "/"
                           "<"
                           ">"
                           "@"
                           "$"
                           "~"
                           "&"
                           "%"
                           "|"
                           "!"
                           "?"
                           "^"
                           "."
                           ":"
                           "\\"
                           "∙"
                           "∘"
                           "×"
                           "★"
                           "⊗"
                           "⊘"
                           "⊙"
                           "⊛"
                           "⊠"
                           "⊡"
                           "∩"
                           "∧"
                           "⊓"
                           "±"
                           "⊕"
                           "⊖"
                           "⊞"
                           "⊟"
                           "∪"
                           "∨"
                           "⊔")))))
                      (:alias (:token (:prec 1 (:pattern "i_?[nN]"))) "in")
                      (:alias (:token (:prec 1 (:pattern "n_?[oO]_?[tT]_?[iI]_?[nN]"))) "notin")
                      (:alias (:token (:prec 1 (:pattern "i_?[sS]"))) "is")
                      (:alias (:token (:prec 1 (:pattern "i_?[sS]_?[nN]_?[oO]_?[tT]"))) "isnot")
                      (:alias (:token (:prec 1 (:pattern "o_?[fF]"))) "of")
                      (:alias (:token (:prec 1 (:pattern "a_?[sS]"))) "as")
                      (:alias (:token (:prec 1 (:pattern "f_?[rR]_?[oO]_?[mM]"))) "from"))
                     operator)
  _infix_operator_4 (:alias
                     (:choice (:alias (:token (:prec 1 (:pattern "a_?[nN]_?[dD]"))) "and"))
                     operator)
  _infix_operator_3 (:alias
                     (:choice
                      (:alias (:token (:prec 1 (:pattern "o_?[rR]"))) "or")
                      (:alias (:token (:prec 1 (:pattern "x_?[oO]_?[rR]"))) "xor"))
                     operator)
  _infix_operator_2 (:alias
                     (:token
                      (:choice
                       (:seq
                        ":"
                        (:repeat1
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔")))
                       (:seq
                        (:choice "@" "?")
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔")))))
                     operator)
  _prefix_extended (:prec "post_expr" (:seq _prefix_expression _post_expression_block))
  _prefix_expression (:choice _prefix_expression_command_start _prefix_expression_word)
  _prefix_expression_word (:prec-left "unary"
                           (:seq
                            (:field :operator
                             (:choice
                              (:alias (:token (:prec 1 (:pattern "o_?[rR]"))) "or")
                              (:alias (:token (:prec 1 (:pattern "x_?[oO]_?[rR]"))) "xor")
                              (:alias (:token (:prec 1 (:pattern "a_?[nN]_?[dD]"))) "and")
                              (:alias (:token (:prec 1 (:pattern "i_?[nN]"))) "in")
                              (:alias
                               (:token (:prec 1 (:pattern "n_?[oO]_?[tT]_?[iI]_?[nN]")))
                               "notin")
                              (:alias (:token (:prec 1 (:pattern "i_?[sS]"))) "is")
                              (:alias
                               (:token (:prec 1 (:pattern "i_?[sS]_?[nN]_?[oO]_?[tT]")))
                               "isnot")
                              (:alias (:token (:prec 1 (:pattern "o_?[fF]"))) "of")
                              (:alias (:token (:prec 1 (:pattern "a_?[sS]"))) "as")
                              (:alias (:token (:prec 1 (:pattern "f_?[rR]_?[oO]_?[mM]"))) "from")
                              (:alias (:token (:prec 1 (:pattern "d_?[iI]_?[vV]"))) "div")
                              (:alias (:token (:prec 1 (:pattern "m_?[oO]_?[dD]"))) "mod")
                              (:alias (:token (:prec 1 (:pattern "s_?[hH]_?[lL]"))) "shl")
                              (:alias (:token (:prec 1 (:pattern "s_?[hH]_?[rR]"))) "shr")))
                            _simple_expression))
  _prefix_expression_command_start (:choice
                                    (:prec-left "unary"
                                     (:seq
                                      (:field :operator (:alias _prefix_operator operator))
                                      _simple_expression))
                                    (:prec-left "unary"
                                     (:seq
                                      (:field :operator
                                       (:alias (:token (:prec 1 (:pattern "n_?[oO]_?[tT]"))) "not"))
                                      _simple_expression))
                                    (:prec-left "sigil"
                                     (:seq
                                      (:field :operator (:alias _sigil_operator operator))
                                      _simple_expression)))
  _sigil_expression (:prec-left "sigil"
                     (:seq (:field :operator (:alias _sigil_operator operator)) _basic_expression))
  _sigil_operator (:token
                   (:seq
                    "@"
                    (:repeat
                     (:choice
                      "="
                      "+"
                      "-"
                      "*"
                      "/"
                      "<"
                      ">"
                      "@"
                      "$"
                      "~"
                      "&"
                      "%"
                      "|"
                      "!"
                      "?"
                      "^"
                      "."
                      ":"
                      "\\"
                      "∙"
                      "∘"
                      "×"
                      "★"
                      "⊗"
                      "⊘"
                      "⊙"
                      "⊛"
                      "⊠"
                      "⊡"
                      "∩"
                      "∧"
                      "⊓"
                      "±"
                      "⊕"
                      "⊖"
                      "⊞"
                      "⊟"
                      "∪"
                      "∨"
                      "⊔"))))
  _prefix_operator (:token
                    (:prec -1
                     (:choice
                      (:token
                       (:seq
                        "^"
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:token
                       (:seq
                        "$"
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:token
                       (:seq
                        (:choice
                         "%"
                         "\\"
                         "/"
                         "∙"
                         "∘"
                         "×"
                         "★"
                         "⊗"
                         "⊘"
                         "⊙"
                         "⊛"
                         "⊠"
                         "⊡"
                         "∩"
                         "∧"
                         "⊓")
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:token
                       (:seq
                        "*"
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:token
                       (:seq
                        (:choice "+" "-" "~" "|" "±" "⊕" "⊖" "⊞" "⊟" "∪" "∨" "⊔")
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:token
                       (:seq
                        "&"
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:token
                       (:seq
                        "."
                        (:repeat1
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))))
                      (:token
                       (:choice
                        (:seq
                         "="
                         (:repeat1
                          (:choice
                           "="
                           "+"
                           "-"
                           "*"
                           "/"
                           "<"
                           ">"
                           "@"
                           "$"
                           "~"
                           "&"
                           "%"
                           "|"
                           "!"
                           "?"
                           "^"
                           "."
                           ":"
                           "\\"
                           "∙"
                           "∘"
                           "×"
                           "★"
                           "⊗"
                           "⊘"
                           "⊙"
                           "⊛"
                           "⊠"
                           "⊡"
                           "∩"
                           "∧"
                           "⊓"
                           "±"
                           "⊕"
                           "⊖"
                           "⊞"
                           "⊟"
                           "∪"
                           "∨"
                           "⊔")))
                        (:seq
                         (:choice "<" ">" "!")
                         (:repeat
                          (:choice
                           "="
                           "+"
                           "-"
                           "*"
                           "/"
                           "<"
                           ">"
                           "@"
                           "$"
                           "~"
                           "&"
                           "%"
                           "|"
                           "!"
                           "?"
                           "^"
                           "."
                           ":"
                           "\\"
                           "∙"
                           "∘"
                           "×"
                           "★"
                           "⊗"
                           "⊘"
                           "⊙"
                           "⊛"
                           "⊠"
                           "⊡"
                           "∩"
                           "∧"
                           "⊓"
                           "±"
                           "⊕"
                           "⊖"
                           "⊞"
                           "⊟"
                           "∪"
                           "∨"
                           "⊔")))))
                      (:token
                       (:choice
                        (:seq
                         ":"
                         (:repeat1
                          (:choice
                           "="
                           "+"
                           "-"
                           "*"
                           "/"
                           "<"
                           ">"
                           "@"
                           "$"
                           "~"
                           "&"
                           "%"
                           "|"
                           "!"
                           "?"
                           "^"
                           "."
                           ":"
                           "\\"
                           "∙"
                           "∘"
                           "×"
                           "★"
                           "⊗"
                           "⊘"
                           "⊙"
                           "⊛"
                           "⊠"
                           "⊡"
                           "∩"
                           "∧"
                           "⊓"
                           "±"
                           "⊕"
                           "⊖"
                           "⊞"
                           "⊟"
                           "∪"
                           "∨"
                           "⊔")))
                        (:seq
                         (:choice "@" "?")
                         (:repeat
                          (:choice
                           "="
                           "+"
                           "-"
                           "*"
                           "/"
                           "<"
                           ">"
                           "@"
                           "$"
                           "~"
                           "&"
                           "%"
                           "|"
                           "!"
                           "?"
                           "^"
                           "."
                           ":"
                           "\\"
                           "∙"
                           "∘"
                           "×"
                           "★"
                           "⊗"
                           "⊘"
                           "⊙"
                           "⊛"
                           "⊠"
                           "⊡"
                           "∩"
                           "∧"
                           "⊓"
                           "±"
                           "⊕"
                           "⊖"
                           "⊞"
                           "⊟"
                           "∪"
                           "∨"
                           "⊔")))))
                      (:token
                       (:seq
                        (:repeat1
                         (:choice
                          "+"
                          "-"
                          "*"
                          "/"
                          "@"
                          "$"
                          "&"
                          "%"
                          "|"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))
                        "="))
                      (:token
                       (:seq
                        (:repeat
                         (:choice
                          "="
                          "+"
                          "-"
                          "*"
                          "/"
                          "<"
                          ">"
                          "@"
                          "$"
                          "~"
                          "&"
                          "%"
                          "|"
                          "!"
                          "?"
                          "^"
                          "."
                          ":"
                          "\\"
                          "∙"
                          "∘"
                          "×"
                          "★"
                          "⊗"
                          "⊘"
                          "⊙"
                          "⊛"
                          "⊠"
                          "⊡"
                          "∩"
                          "∧"
                          "⊓"
                          "±"
                          "⊕"
                          "⊖"
                          "⊞"
                          "⊟"
                          "∪"
                          "∨"
                          "⊔"))
                        (:choice "=" "-" "~")
                        ">")))))
  cast (:seq
        (:alias (:token (:prec 1 (:pattern "c_?[aA]_?[sS]_?[tT]"))) "cast")
        (:choice (:seq "[" (:field :type type_expression) _bracket_close) :blank)
        (:seq "(" (:field :value (:choice _expression colon_expression)) _paren_close))
  parenthesized (:choice
                 (:seq
                  "("
                  (:choice
                   _expression_with_call_do
                   _simple_statement_no_expression
                   assignment
                   const_section
                   var_section
                   let_section
                   while)
                  (:repeat (:seq ";" _statement))
                  (:choice ";" :blank)
                  _paren_close)
                 (:seq
                  "("
                  ";"
                  (:seq _statement (:repeat (:seq ";" _statement)))
                  (:choice ";" :blank)
                  _paren_close))
  dot_expression (:prec "suffix"
                  (:seq (:field :left _basic_expression) "." (:field :right _symbol)))
  bracket_expression (:prec "suffix"
                      (:seq
                       (:field :left _basic_expression)
                       (:token-immediate "[")
                       (:field :right
                        (:choice (:alias _colon_equal_expression_list argument_list) :blank))
                       _bracket_close))
  curly_expression (:prec "suffix"
                    (:seq
                     (:field :left _basic_expression)
                     (:token-immediate "{")
                     (:field :right
                      (:choice (:alias _colon_equal_expression_list argument_list) :blank))
                     _curly_close))
  pragma_expression (:seq (:field :left _basic_expression) (:field :right pragma_list))
  array_construction (:seq "[" (:choice _colon_equal_expression_list :blank) _bracket_close)
  curly_construction (:choice
                      (:seq "{" _colon_equal_expression_list _curly_close)
                      (:seq "{" (:choice ":" :blank) _curly_close))
  tuple_construction (:choice (:seq "(" (:choice _colon_equal_expression_list :blank) _paren_close))
  generalized_string (:seq
                      (:field :function (:choice identifier dot_expression))
                      _generalized_string_literal)
  _generalized_string_literal (:choice
                               (:seq
                                (:token-immediate "\"\"\"")
                                (:choice (:alias _long_string_body string_content) :blank)
                                (:token-immediate "\"\"\""))
                               (:seq
                                (:token-immediate "\"")
                                (:choice (:alias _raw_string_body string_content) :blank)
                                (:token-immediate "\"")))
  pragma_list (:seq
               "{."
               (:choice _colon_equal_expression_list :blank)
               (:choice _dot_curly_close _curly_close))
  expression_list (:prec-right 0
                   (:seq (:choice _expression) (:repeat (:seq "," (:choice _expression)))))
  _equal_expression_list (:prec-right 0
                          (:seq
                           (:choice _expression_with_call_do equal_expression)
                           (:repeat (:seq "," (:choice _expression_with_call_do equal_expression)))))
  _colon_equal_expression_list (:prec-right 0
                                (:seq
                                 (:seq
                                  (:choice
                                   _expression_with_call_do
                                   colon_expression
                                   equal_expression)
                                  (:repeat
                                   (:seq
                                    ","
                                    (:choice
                                     _expression_with_call_do
                                     colon_expression
                                     equal_expression))))
                                 (:choice "," :blank)))
  colon_expression (:seq (:field :left _left_hand_side) ":" (:field :right _expression))
  equal_expression (:seq (:field :left _left_hand_side) "=" (:field :right _expression))
  assignment (:seq (:field :left _left_hand_side) "=" (:field :right _expression_with_post_block))
  _left_hand_side _simple_expression
  parameter_declaration_list (:seq
                              (:choice "(" (:token-immediate "("))
                              (:choice _parameter_declaration_list :blank)
                              _paren_close)
  _parameter_declaration_list (:seq
                               (:seq
                                (:alias _identifier_declaration parameter_declaration)
                                (:repeat
                                 (:seq
                                  (:choice "," ";")
                                  (:alias _identifier_declaration parameter_declaration))))
                               (:choice (:choice "," ";") :blank))
  _field_declaration_list (:seq
                           (:seq
                            (:alias _identifier_declaration field_declaration)
                            (:repeat
                             (:seq
                              (:choice "," ";")
                              (:alias _identifier_declaration field_declaration))))
                           (:choice (:choice "," ";") :blank))
  _identifier_declaration (:prec-right 0
                           (:seq
                            symbol_declaration_list
                            (:choice (:seq ":" (:field :type type_expression)) :blank)
                            (:choice (:seq "=" (:field :value _expression_with_post_block)) :blank)))
  symbol_declaration_list (:prec-right 0
                           (:seq
                            (:seq
                             (:choice symbol_declaration tuple_deconstruct_declaration)
                             (:repeat
                              (:seq "," (:choice symbol_declaration tuple_deconstruct_declaration))))
                            (:choice "," :blank)))
  tuple_deconstruct_declaration (:seq
                                 "("
                                 (:seq
                                  (:choice symbol_declaration tuple_deconstruct_declaration)
                                  (:repeat
                                   (:seq
                                    ","
                                    (:choice symbol_declaration tuple_deconstruct_declaration))))
                                 (:choice "," :blank)
                                 _paren_close)
  symbol_declaration (:seq
                      (:field :name (:choice _symbol exported_symbol))
                      (:choice pragma_list :blank))
  exported_symbol (:seq _symbol (:choice _want_export_marker :blank) "*")
  _literal (:choice
            nil_literal
            integer_literal
            float_literal
            custom_numeric_literal
            char_literal
            _string_literal)
  nil_literal (:alias (:token (:prec 1 (:pattern "n_?[iI]_?[lL]"))) "nil")
  integer_literal (:token
                   (:seq
                    (:token
                     (:seq
                      (:choice "-" :blank)
                      (:choice
                       (:pattern "[0-9](_?[0-9])*")
                       (:pattern "0[xX][0-9a-fA-F](_?[0-9a-fA-F])*")
                       (:pattern "0[oO][0-7](_?[0-7])*")
                       (:pattern "0[bB][01](_?[01])*"))))
                    (:choice (:pattern "'?[iIuU](8|16|32|64)|[uU]") :blank)))
  float_literal (:token
                 (:choice
                  (:seq
                   (:token
                    (:seq
                     (:choice "-" :blank)
                     (:choice
                      (:pattern "[0-9](_?[0-9])*")
                      (:pattern "0[xX][0-9a-fA-F](_?[0-9a-fA-F])*")
                      (:pattern "0[oO][0-7](_?[0-7])*")
                      (:pattern "0[bB][01](_?[01])*"))))
                   (:pattern "'?[fFdD](32|64|128)?"))
                  (:seq
                   (:token
                    (:seq
                     (:choice "-" :blank)
                     (:choice
                      (:seq (:pattern "[0-9](_?[0-9])*") "." (:pattern "[0-9](_?[0-9])*"))
                      (:seq
                       (:pattern "[0-9](_?[0-9])*")
                       (:pattern "[eE][+-]?")
                       (:pattern "[0-9](_?[0-9])*"))
                      (:seq
                       (:pattern "[0-9](_?[0-9])*")
                       "."
                       (:pattern "[0-9](_?[0-9])*")
                       (:pattern "[eE][+-]?")
                       (:pattern "[0-9](_?[0-9])*")))))
                   (:choice (:pattern "'?[fFdD](32|64|128)?") :blank))))
  custom_numeric_literal (:token
                          (:seq
                           (:choice
                            (:token
                             (:seq
                              (:choice "-" :blank)
                              (:choice
                               (:pattern "[0-9](_?[0-9])*")
                               (:pattern "0[xX][0-9a-fA-F](_?[0-9a-fA-F])*")
                               (:pattern "0[oO][0-7](_?[0-7])*")
                               (:pattern "0[bB][01](_?[01])*"))))
                            (:token
                             (:seq
                              (:choice "-" :blank)
                              (:choice
                               (:seq (:pattern "[0-9](_?[0-9])*") "." (:pattern "[0-9](_?[0-9])*"))
                               (:seq
                                (:pattern "[0-9](_?[0-9])*")
                                (:pattern "[eE][+-]?")
                                (:pattern "[0-9](_?[0-9])*"))
                               (:seq
                                (:pattern "[0-9](_?[0-9])*")
                                "."
                                (:pattern "[0-9](_?[0-9])*")
                                (:pattern "[eE][+-]?")
                                (:pattern "[0-9](_?[0-9])*"))))))
                           "'"
                           (:pattern "[a-zA-Z\\U00000080-\\U0010FFFF&&[^∙∘×★⊗⊘⊙⊛⊠⊡∩∧⊓±⊕⊖⊞⊟∪∨⊔]](_?[a-zA-Z0-9\\U00000080-\\U0010FFFF&&[^∙∘×★⊗⊘⊙⊛⊠⊡∩∧⊓±⊕⊖⊞⊟∪∨⊔]])*")))
  char_literal (:seq
                "'"
                (:choice
                 (:token-immediate (:prec 2 (:pattern "[^\\\\\\n\\r']")))
                 (:alias _char_escape_sequence escape_sequence))
                (:token-immediate "'"))
  _char_escape_sequence (:token-immediate
                         (:seq
                          "\\"
                          (:pattern "[rRcCnNlLfFtTvV\\\\\"'aAbBeE]|\\d+|[xX][0-9a-fA-F]{2}")))
  _string_literal (:choice interpreted_string_literal raw_string_literal long_string_literal)
  interpreted_string_literal (:seq
                              "\""
                              (:choice (:alias _interpreted_string_body string_content) :blank)
                              (:token-immediate "\""))
  _interpreted_string_body (:repeat1
                            (:choice
                             (:token-immediate (:prec 2 (:pattern "[^\\n\\r\"\\\\]+")))
                             escape_sequence))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[rRcCnNlLfFtTvV\\\\\"'aAbBeE]|\\d+|[xX][0-9a-fA-F]{2}")
                     (:pattern "[pP]")
                     (:pattern "[uU]([0-9a-fA-F]{4}|\\{[0-9a-fA-F]+\\})"))))
  raw_string_literal (:seq
                      (:choice "r\"" "R\"")
                      (:choice (:alias _raw_string_body string_content) :blank)
                      (:token-immediate "\""))
  _raw_string_body (:repeat1
                    (:choice
                     (:token-immediate (:prec 2 (:pattern "[^\\n\\r\"]")))
                     (:alias _raw_string_escape escape_sequence)))
  _raw_string_escape (:token-immediate "\"\"")
  long_string_literal (:seq
                       (:choice "\"\"\"" "r\"\"\"" "R\"\"\"")
                       (:choice (:alias _long_string_body string_content) :blank)
                       (:token-immediate "\"\"\""))
  _long_string_body (:repeat1
                     (:choice (:token-immediate (:prec 2 (:pattern "[^\"]+"))) _long_string_quote))
  _symbol (:choice accent_quoted identifier blank_identifier)
  accent_quoted (:seq "`" (:repeat1 (:alias _accent_quoted_identifier identifier)) "`")
  _accent_quoted_identifier (:pattern "[^\\x00-\\x1f\\r\\n\\t` ]+")
  blank_identifier "_"
  identifier (:pattern "[a-zA-Z\\U00000080-\\U0010FFFF&&[^∙∘×★⊗⊘⊙⊛⊠⊡∩∧⊓±⊕⊖⊞⊟∪∨⊔]](_?[a-zA-Z0-9\\U00000080-\\U0010FFFF&&[^∙∘×★⊗⊘⊙⊛⊠⊡∩∧⊓±⊕⊖⊞⊟∪∨⊔]])*")
  _paren_close (:seq (:choice _inhibit_layout_end :blank) ")")
  _bracket_close (:seq (:choice _inhibit_layout_end :blank) "]")
  _curly_close (:seq (:choice _inhibit_layout_end :blank) "}")
  _dot_curly_close (:seq (:choice _inhibit_layout_end :blank) ".}")
  block_documentation_comment (:seq
                               (:token (:prec 1 "##["))
                               (:alias _block_documentation_comment_content comment_content)
                               "]##")
  block_comment (:seq (:token (:prec 1 "#[")) (:alias _block_comment_content comment_content) "]#")
  documentation_comment (:seq "##" comment_content)
  comment (:seq "#" comment_content)}}
