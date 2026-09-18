# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "c"
 :word identifier
 :extras [(:pattern "\\s|\\\\\\r?\\n") comment]
 :conflicts [[type_specifier _declarator]
             [type_specifier _declarator macro_type_specifier]
             [type_specifier expression]
             [type_specifier expression macro_type_specifier]
             [type_specifier macro_type_specifier]
             [type_specifier sized_type_specifier]
             [sized_type_specifier]
             [attributed_statement]
             [_declaration_modifiers attributed_statement]
             [enum_specifier]
             [type_specifier _old_style_parameter_list]
             [parameter_list _old_style_parameter_list]
             [function_declarator _function_declaration_declarator]
             [_block_item statement]
             [_top_level_item _top_level_statement]
             [type_specifier _top_level_expression_statement]
             [type_qualifier extension_expression]]
 :precedences []
 :externals []
 :inline [_type_identifier
          _field_identifier
          _statement_identifier
          _non_case_statement
          _assignment_left_expression
          _expression_not_binary]
 :supertypes [expression
              statement
              type_specifier
              _declarator
              _field_declarator
              _type_declarator
              _abstract_declarator]
 :rules
 {translation_unit (:repeat _top_level_item)
  _top_level_item (:choice
                   function_definition
                   (:alias _old_style_function_definition function_definition)
                   linkage_specification
                   declaration
                   _top_level_statement
                   attributed_statement
                   type_definition
                   _empty_declaration
                   preproc_if
                   preproc_ifdef
                   preproc_include
                   preproc_def
                   preproc_function_def
                   preproc_call)
  _block_item (:choice
               function_definition
               (:alias _old_style_function_definition function_definition)
               linkage_specification
               declaration
               statement
               attributed_statement
               type_definition
               _empty_declaration
               preproc_if
               preproc_ifdef
               preproc_include
               preproc_def
               preproc_function_def
               preproc_call)
  preproc_include (:seq
                   (:alias (:pattern "#[ \t]*include") "#include")
                   (:field :path
                    (:choice
                     string_literal
                     system_lib_string
                     identifier
                     (:alias preproc_call_expression call_expression)))
                   (:token-immediate (:pattern "\\r?\\n")))
  preproc_def (:seq
               (:alias (:pattern "#[ \t]*define") "#define")
               (:field :name identifier)
               (:field :value (:choice preproc_arg :blank))
               (:token-immediate (:pattern "\\r?\\n")))
  preproc_function_def (:seq
                        (:alias (:pattern "#[ \t]*define") "#define")
                        (:field :name identifier)
                        (:field :parameters preproc_params)
                        (:field :value (:choice preproc_arg :blank))
                        (:token-immediate (:pattern "\\r?\\n")))
  preproc_params (:seq
                  (:token-immediate "(")
                  (:choice
                   (:seq (:choice identifier "...") (:repeat (:seq "," (:choice identifier "..."))))
                   :blank)
                  ")")
  preproc_call (:seq
                (:field :directive preproc_directive)
                (:field :argument (:choice preproc_arg :blank))
                (:token-immediate (:pattern "\\r?\\n")))
  preproc_if (:prec 0
              (:seq
               (:alias (:pattern "#[ \t]*if") "#if")
               (:field :condition _preproc_expression)
               "\n"
               (:repeat _block_item)
               (:field :alternative
                (:choice (:choice preproc_else preproc_elif preproc_elifdef) :blank))
               (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_ifdef (:prec 0
                 (:seq
                  (:choice
                   (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                   (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                  (:field :name identifier)
                  (:repeat _block_item)
                  (:field :alternative
                   (:choice (:choice preproc_else preproc_elif preproc_elifdef) :blank))
                  (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else (:prec 0 (:seq (:alias (:pattern "#[ \t]*else") "#else") (:repeat _block_item)))
  preproc_elif (:prec 0
                (:seq
                 (:alias (:pattern "#[ \t]*elif") "#elif")
                 (:field :condition _preproc_expression)
                 "\n"
                 (:repeat _block_item)
                 (:field :alternative
                  (:choice (:choice preproc_else preproc_elif preproc_elifdef) :blank))))
  preproc_elifdef (:prec 0
                   (:seq
                    (:choice
                     (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                     (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                    (:field :name identifier)
                    (:repeat _block_item)
                    (:field :alternative
                     (:choice (:choice preproc_else preproc_elif preproc_elifdef) :blank))))
  preproc_if_in_field_declaration_list (:prec 0
                                        (:seq
                                         (:alias (:pattern "#[ \t]*if") "#if")
                                         (:field :condition _preproc_expression)
                                         "\n"
                                         (:repeat _field_declaration_list_item)
                                         (:field :alternative
                                          (:choice
                                           (:choice
                                            (:alias
                                             preproc_else_in_field_declaration_list
                                             preproc_else)
                                            (:alias
                                             preproc_elif_in_field_declaration_list
                                             preproc_elif)
                                            (:alias
                                             preproc_elifdef_in_field_declaration_list
                                             preproc_elifdef))
                                           :blank))
                                         (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_ifdef_in_field_declaration_list (:prec 0
                                           (:seq
                                            (:choice
                                             (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                             (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                            (:field :name identifier)
                                            (:repeat _field_declaration_list_item)
                                            (:field :alternative
                                             (:choice
                                              (:choice
                                               (:alias
                                                preproc_else_in_field_declaration_list
                                                preproc_else)
                                               (:alias
                                                preproc_elif_in_field_declaration_list
                                                preproc_elif)
                                               (:alias
                                                preproc_elifdef_in_field_declaration_list
                                                preproc_elifdef))
                                              :blank))
                                            (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else_in_field_declaration_list (:prec 0
                                          (:seq
                                           (:alias (:pattern "#[ \t]*else") "#else")
                                           (:repeat _field_declaration_list_item)))
  preproc_elif_in_field_declaration_list (:prec 0
                                          (:seq
                                           (:alias (:pattern "#[ \t]*elif") "#elif")
                                           (:field :condition _preproc_expression)
                                           "\n"
                                           (:repeat _field_declaration_list_item)
                                           (:field :alternative
                                            (:choice
                                             (:choice
                                              (:alias
                                               preproc_else_in_field_declaration_list
                                               preproc_else)
                                              (:alias
                                               preproc_elif_in_field_declaration_list
                                               preproc_elif)
                                              (:alias
                                               preproc_elifdef_in_field_declaration_list
                                               preproc_elifdef))
                                             :blank))))
  preproc_elifdef_in_field_declaration_list (:prec 0
                                             (:seq
                                              (:choice
                                               (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                               (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                              (:field :name identifier)
                                              (:repeat _field_declaration_list_item)
                                              (:field :alternative
                                               (:choice
                                                (:choice
                                                 (:alias
                                                  preproc_else_in_field_declaration_list
                                                  preproc_else)
                                                 (:alias
                                                  preproc_elif_in_field_declaration_list
                                                  preproc_elif)
                                                 (:alias
                                                  preproc_elifdef_in_field_declaration_list
                                                  preproc_elifdef))
                                                :blank))))
  preproc_if_in_enumerator_list (:prec 0
                                 (:seq
                                  (:alias (:pattern "#[ \t]*if") "#if")
                                  (:field :condition _preproc_expression)
                                  "\n"
                                  (:repeat (:seq enumerator ","))
                                  (:field :alternative
                                   (:choice
                                    (:choice
                                     (:alias preproc_else_in_enumerator_list preproc_else)
                                     (:alias preproc_elif_in_enumerator_list preproc_elif)
                                     (:alias preproc_elifdef_in_enumerator_list preproc_elifdef))
                                    :blank))
                                  (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_ifdef_in_enumerator_list (:prec 0
                                    (:seq
                                     (:choice
                                      (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                      (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                     (:field :name identifier)
                                     (:repeat (:seq enumerator ","))
                                     (:field :alternative
                                      (:choice
                                       (:choice
                                        (:alias preproc_else_in_enumerator_list preproc_else)
                                        (:alias preproc_elif_in_enumerator_list preproc_elif)
                                        (:alias preproc_elifdef_in_enumerator_list preproc_elifdef))
                                       :blank))
                                     (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else_in_enumerator_list (:prec 0
                                   (:seq
                                    (:alias (:pattern "#[ \t]*else") "#else")
                                    (:repeat (:seq enumerator ","))))
  preproc_elif_in_enumerator_list (:prec 0
                                   (:seq
                                    (:alias (:pattern "#[ \t]*elif") "#elif")
                                    (:field :condition _preproc_expression)
                                    "\n"
                                    (:repeat (:seq enumerator ","))
                                    (:field :alternative
                                     (:choice
                                      (:choice
                                       (:alias preproc_else_in_enumerator_list preproc_else)
                                       (:alias preproc_elif_in_enumerator_list preproc_elif)
                                       (:alias preproc_elifdef_in_enumerator_list preproc_elifdef))
                                      :blank))))
  preproc_elifdef_in_enumerator_list (:prec 0
                                      (:seq
                                       (:choice
                                        (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                        (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                       (:field :name identifier)
                                       (:repeat (:seq enumerator ","))
                                       (:field :alternative
                                        (:choice
                                         (:choice
                                          (:alias preproc_else_in_enumerator_list preproc_else)
                                          (:alias preproc_elif_in_enumerator_list preproc_elif)
                                          (:alias
                                           preproc_elifdef_in_enumerator_list
                                           preproc_elifdef))
                                         :blank))))
  preproc_if_in_enumerator_list_no_comma (:prec -1
                                          (:seq
                                           (:alias (:pattern "#[ \t]*if") "#if")
                                           (:field :condition _preproc_expression)
                                           "\n"
                                           (:repeat enumerator)
                                           (:field :alternative
                                            (:choice
                                             (:choice
                                              (:alias
                                               preproc_else_in_enumerator_list_no_comma
                                               preproc_else)
                                              (:alias
                                               preproc_elif_in_enumerator_list_no_comma
                                               preproc_elif)
                                              (:alias
                                               preproc_elifdef_in_enumerator_list_no_comma
                                               preproc_elifdef))
                                             :blank))
                                           (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_ifdef_in_enumerator_list_no_comma (:prec -1
                                             (:seq
                                              (:choice
                                               (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                               (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                              (:field :name identifier)
                                              (:repeat enumerator)
                                              (:field :alternative
                                               (:choice
                                                (:choice
                                                 (:alias
                                                  preproc_else_in_enumerator_list_no_comma
                                                  preproc_else)
                                                 (:alias
                                                  preproc_elif_in_enumerator_list_no_comma
                                                  preproc_elif)
                                                 (:alias
                                                  preproc_elifdef_in_enumerator_list_no_comma
                                                  preproc_elifdef))
                                                :blank))
                                              (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else_in_enumerator_list_no_comma (:prec -1
                                            (:seq
                                             (:alias (:pattern "#[ \t]*else") "#else")
                                             (:repeat enumerator)))
  preproc_elif_in_enumerator_list_no_comma (:prec -1
                                            (:seq
                                             (:alias (:pattern "#[ \t]*elif") "#elif")
                                             (:field :condition _preproc_expression)
                                             "\n"
                                             (:repeat enumerator)
                                             (:field :alternative
                                              (:choice
                                               (:choice
                                                (:alias
                                                 preproc_else_in_enumerator_list_no_comma
                                                 preproc_else)
                                                (:alias
                                                 preproc_elif_in_enumerator_list_no_comma
                                                 preproc_elif)
                                                (:alias
                                                 preproc_elifdef_in_enumerator_list_no_comma
                                                 preproc_elifdef))
                                               :blank))))
  preproc_elifdef_in_enumerator_list_no_comma (:prec -1
                                               (:seq
                                                (:choice
                                                 (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                                 (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                                (:field :name identifier)
                                                (:repeat enumerator)
                                                (:field :alternative
                                                 (:choice
                                                  (:choice
                                                   (:alias
                                                    preproc_else_in_enumerator_list_no_comma
                                                    preproc_else)
                                                   (:alias
                                                    preproc_elif_in_enumerator_list_no_comma
                                                    preproc_elif)
                                                   (:alias
                                                    preproc_elifdef_in_enumerator_list_no_comma
                                                    preproc_elifdef))
                                                  :blank))))
  preproc_arg (:token (:prec -1 (:pattern "\\S([^/\\n]|\\/[^*]|\\\\\\r?\\n)*")))
  preproc_directive (:pattern "#[ \\t]*[a-zA-Z0-9]\\w*")
  _preproc_expression (:choice
                       identifier
                       (:alias preproc_call_expression call_expression)
                       number_literal
                       char_literal
                       preproc_defined
                       (:alias preproc_unary_expression unary_expression)
                       (:alias preproc_binary_expression binary_expression)
                       (:alias preproc_parenthesized_expression parenthesized_expression))
  preproc_parenthesized_expression (:seq "(" _preproc_expression ")")
  preproc_defined (:choice
                   (:prec 15 (:seq "defined" "(" identifier ")"))
                   (:seq "defined" identifier))
  preproc_unary_expression (:prec-left 14
                            (:seq
                             (:field :operator (:choice "!" "~" "-" "+"))
                             (:field :argument _preproc_expression)))
  preproc_call_expression (:prec 15
                           (:seq
                            (:field :function identifier)
                            (:field :arguments (:alias preproc_argument_list argument_list))))
  preproc_argument_list (:seq
                         "("
                         (:choice
                          (:seq _preproc_expression (:repeat (:seq "," _preproc_expression)))
                          :blank)
                         ")")
  preproc_binary_expression (:choice
                             (:prec-left 10
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "+")
                               (:field :right _preproc_expression)))
                             (:prec-left 10
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "-")
                               (:field :right _preproc_expression)))
                             (:prec-left 11
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "*")
                               (:field :right _preproc_expression)))
                             (:prec-left 11
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "/")
                               (:field :right _preproc_expression)))
                             (:prec-left 11
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "%")
                               (:field :right _preproc_expression)))
                             (:prec-left 1
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "||")
                               (:field :right _preproc_expression)))
                             (:prec-left 2
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "&&")
                               (:field :right _preproc_expression)))
                             (:prec-left 3
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "|")
                               (:field :right _preproc_expression)))
                             (:prec-left 4
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "^")
                               (:field :right _preproc_expression)))
                             (:prec-left 5
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "&")
                               (:field :right _preproc_expression)))
                             (:prec-left 6
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "==")
                               (:field :right _preproc_expression)))
                             (:prec-left 6
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "!=")
                               (:field :right _preproc_expression)))
                             (:prec-left 7
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator ">")
                               (:field :right _preproc_expression)))
                             (:prec-left 7
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator ">=")
                               (:field :right _preproc_expression)))
                             (:prec-left 7
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "<=")
                               (:field :right _preproc_expression)))
                             (:prec-left 7
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "<")
                               (:field :right _preproc_expression)))
                             (:prec-left 9
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "<<")
                               (:field :right _preproc_expression)))
                             (:prec-left 9
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator ">>")
                               (:field :right _preproc_expression))))
  function_definition (:seq
                       (:choice ms_call_modifier :blank)
                       _declaration_specifiers
                       (:choice ms_call_modifier :blank)
                       (:field :declarator _declarator)
                       (:field :body compound_statement))
  _old_style_function_definition (:seq
                                  (:choice ms_call_modifier :blank)
                                  _declaration_specifiers
                                  (:field :declarator
                                   (:alias _old_style_function_declarator function_declarator))
                                  (:repeat1 declaration)
                                  (:field :body compound_statement))
  declaration (:seq
               _declaration_specifiers
               (:seq
                (:field :declarator
                 (:choice
                  (:seq
                   (:choice ms_call_modifier :blank)
                   _declaration_declarator
                   (:choice gnu_asm_expression :blank))
                  init_declarator))
                (:repeat
                 (:seq
                  ","
                  (:field :declarator
                   (:choice
                    (:seq
                     (:choice ms_call_modifier :blank)
                     _declaration_declarator
                     (:choice gnu_asm_expression :blank))
                    init_declarator)))))
               ";")
  type_definition (:seq
                   (:choice "__extension__" :blank)
                   "typedef"
                   _type_definition_type
                   _type_definition_declarators
                   (:repeat attribute_specifier)
                   ";")
  _type_definition_type (:seq
                         (:repeat type_qualifier)
                         (:field :type type_specifier)
                         (:repeat type_qualifier))
  _type_definition_declarators (:seq
                                (:field :declarator _type_declarator)
                                (:repeat (:seq "," (:field :declarator _type_declarator))))
  _declaration_modifiers (:choice
                          storage_class_specifier
                          type_qualifier
                          attribute_specifier
                          attribute_declaration
                          ms_declspec_modifier)
  _declaration_specifiers (:prec-right 0
                           (:seq
                            (:repeat _declaration_modifiers)
                            (:field :type type_specifier)
                            (:repeat _declaration_modifiers)))
  linkage_specification (:seq
                         "extern"
                         (:field :value string_literal)
                         (:field :body (:choice function_definition declaration declaration_list)))
  attribute_specifier (:seq (:choice "__attribute__" "__attribute") "(" argument_list ")")
  attribute (:seq
             (:choice (:seq (:field :prefix identifier) "::") :blank)
             (:field :name identifier)
             (:choice argument_list :blank))
  attribute_declaration (:seq "[[" (:seq attribute (:repeat (:seq "," attribute))) "]]")
  ms_declspec_modifier (:seq "__declspec" "(" identifier ")")
  ms_based_modifier (:seq "__based" argument_list)
  ms_call_modifier (:choice
                    "__cdecl"
                    "__clrcall"
                    "__stdcall"
                    "__fastcall"
                    "__thiscall"
                    "__vectorcall")
  ms_restrict_modifier "__restrict"
  ms_unsigned_ptr_modifier "__uptr"
  ms_signed_ptr_modifier "__sptr"
  ms_unaligned_ptr_modifier (:choice "_unaligned" "__unaligned")
  ms_pointer_modifier (:choice
                       ms_unaligned_ptr_modifier
                       ms_restrict_modifier
                       ms_unsigned_ptr_modifier
                       ms_signed_ptr_modifier)
  declaration_list (:seq "{" (:repeat _block_item) "}")
  _declarator (:choice
               attributed_declarator
               pointer_declarator
               function_declarator
               array_declarator
               parenthesized_declarator
               identifier)
  _declaration_declarator (:choice
                           attributed_declarator
                           pointer_declarator
                           (:alias _function_declaration_declarator function_declarator)
                           array_declarator
                           parenthesized_declarator
                           identifier)
  _field_declarator (:choice
                     (:alias attributed_field_declarator attributed_declarator)
                     (:alias pointer_field_declarator pointer_declarator)
                     (:alias function_field_declarator function_declarator)
                     (:alias array_field_declarator array_declarator)
                     (:alias parenthesized_field_declarator parenthesized_declarator)
                     _field_identifier)
  _type_declarator (:choice
                    (:alias attributed_type_declarator attributed_declarator)
                    (:alias pointer_type_declarator pointer_declarator)
                    (:alias function_type_declarator function_declarator)
                    (:alias array_type_declarator array_declarator)
                    (:alias parenthesized_type_declarator parenthesized_declarator)
                    _type_identifier
                    (:alias (:choice "signed" "unsigned" "long" "short") primitive_type)
                    primitive_type)
  _abstract_declarator (:choice
                        abstract_pointer_declarator
                        abstract_function_declarator
                        abstract_array_declarator
                        abstract_parenthesized_declarator)
  parenthesized_declarator (:prec-dynamic -10
                            (:seq "(" (:choice ms_call_modifier :blank) _declarator ")"))
  parenthesized_field_declarator (:prec-dynamic -10
                                  (:seq "(" (:choice ms_call_modifier :blank) _field_declarator ")"))
  parenthesized_type_declarator (:prec-dynamic -10
                                 (:seq "(" (:choice ms_call_modifier :blank) _type_declarator ")"))
  abstract_parenthesized_declarator (:prec 1
                                     (:seq
                                      "("
                                      (:choice ms_call_modifier :blank)
                                      _abstract_declarator
                                      ")"))
  attributed_declarator (:prec-right 0 (:seq _declarator (:repeat1 attribute_declaration)))
  attributed_field_declarator (:prec-right 0
                               (:seq _field_declarator (:repeat1 attribute_declaration)))
  attributed_type_declarator (:prec-right 0
                              (:seq _type_declarator (:repeat1 attribute_declaration)))
  pointer_declarator (:prec-dynamic 1
                      (:prec-right 0
                       (:seq
                        (:choice ms_based_modifier :blank)
                        "*"
                        (:repeat ms_pointer_modifier)
                        (:repeat type_qualifier)
                        (:field :declarator _declarator))))
  pointer_field_declarator (:prec-dynamic 1
                            (:prec-right 0
                             (:seq
                              (:choice ms_based_modifier :blank)
                              "*"
                              (:repeat ms_pointer_modifier)
                              (:repeat type_qualifier)
                              (:field :declarator _field_declarator))))
  pointer_type_declarator (:prec-dynamic 1
                           (:prec-right 0
                            (:seq
                             (:choice ms_based_modifier :blank)
                             "*"
                             (:repeat ms_pointer_modifier)
                             (:repeat type_qualifier)
                             (:field :declarator _type_declarator))))
  abstract_pointer_declarator (:prec-dynamic 1
                               (:prec-right 0
                                (:seq
                                 "*"
                                 (:repeat ms_pointer_modifier)
                                 (:repeat type_qualifier)
                                 (:field :declarator (:choice _abstract_declarator :blank)))))
  function_declarator (:prec-right 1
                       (:seq
                        (:field :declarator _declarator)
                        (:field :parameters parameter_list)
                        (:choice gnu_asm_expression :blank)
                        (:repeat
                         (:choice
                          attribute_specifier
                          identifier
                          (:alias preproc_call_expression call_expression)))))
  _function_declaration_declarator (:prec-right 1
                                    (:seq
                                     (:field :declarator _declarator)
                                     (:field :parameters parameter_list)
                                     (:choice gnu_asm_expression :blank)
                                     (:repeat attribute_specifier)))
  function_field_declarator (:prec 1
                             (:seq
                              (:field :declarator _field_declarator)
                              (:field :parameters parameter_list)))
  function_type_declarator (:prec 1
                            (:seq
                             (:field :declarator _type_declarator)
                             (:field :parameters parameter_list)))
  abstract_function_declarator (:prec 1
                                (:seq
                                 (:field :declarator (:choice _abstract_declarator :blank))
                                 (:field :parameters parameter_list)))
  _old_style_function_declarator (:seq
                                  (:field :declarator _declarator)
                                  (:field :parameters
                                   (:alias _old_style_parameter_list parameter_list)))
  array_declarator (:prec 1
                    (:seq
                     (:field :declarator _declarator)
                     "["
                     (:repeat (:choice type_qualifier "static"))
                     (:field :size (:choice (:choice expression "*") :blank))
                     "]"))
  array_field_declarator (:prec 1
                          (:seq
                           (:field :declarator _field_declarator)
                           "["
                           (:repeat (:choice type_qualifier "static"))
                           (:field :size (:choice (:choice expression "*") :blank))
                           "]"))
  array_type_declarator (:prec 1
                         (:seq
                          (:field :declarator _type_declarator)
                          "["
                          (:repeat (:choice type_qualifier "static"))
                          (:field :size (:choice (:choice expression "*") :blank))
                          "]"))
  abstract_array_declarator (:prec 1
                             (:seq
                              (:field :declarator (:choice _abstract_declarator :blank))
                              "["
                              (:repeat (:choice type_qualifier "static"))
                              (:field :size (:choice (:choice expression "*") :blank))
                              "]"))
  init_declarator (:seq
                   (:field :declarator _declarator)
                   "="
                   (:field :value (:choice initializer_list expression)))
  compound_statement (:seq "{" (:repeat _block_item) "}")
  storage_class_specifier (:choice
                           "extern"
                           "static"
                           "auto"
                           "register"
                           "inline"
                           "__inline"
                           "__inline__"
                           "__forceinline"
                           "thread_local"
                           "__thread")
  type_qualifier (:choice
                  "const"
                  "constexpr"
                  "volatile"
                  "restrict"
                  "__restrict__"
                  "__extension__"
                  "_Atomic"
                  "_Noreturn"
                  "noreturn"
                  "_Nonnull"
                  alignas_qualifier)
  alignas_qualifier (:seq
                     (:choice "alignas" "_Alignas")
                     "("
                     (:choice expression type_descriptor)
                     ")")
  type_specifier (:choice
                  struct_specifier
                  union_specifier
                  enum_specifier
                  macro_type_specifier
                  sized_type_specifier
                  primitive_type
                  _type_identifier)
  sized_type_specifier (:choice
                        (:seq
                         (:repeat (:choice "signed" "unsigned" "long" "short"))
                         (:field :type
                          (:choice
                           (:choice (:prec-dynamic -1 _type_identifier) primitive_type)
                           :blank))
                         (:repeat1 (:choice "signed" "unsigned" "long" "short")))
                        (:seq
                         (:repeat1 (:choice "signed" "unsigned" "long" "short"))
                         (:repeat type_qualifier)
                         (:field :type
                          (:choice
                           (:choice (:prec-dynamic -1 _type_identifier) primitive_type)
                           :blank))
                         (:repeat (:choice "signed" "unsigned" "long" "short"))))
  primitive_type (:token
                  (:choice
                   "bool"
                   "char"
                   "int"
                   "float"
                   "double"
                   "void"
                   "size_t"
                   "ssize_t"
                   "ptrdiff_t"
                   "intptr_t"
                   "uintptr_t"
                   "charptr_t"
                   "nullptr_t"
                   "max_align_t"
                   "int8_t"
                   "int16_t"
                   "int32_t"
                   "int64_t"
                   "uint8_t"
                   "uint16_t"
                   "uint32_t"
                   "uint64_t"
                   "char8_t"
                   "char16_t"
                   "char32_t"
                   "char64_t"))
  enum_specifier (:seq
                  "enum"
                  (:choice
                   (:seq
                    (:field :name _type_identifier)
                    (:choice (:seq ":" (:field :underlying_type primitive_type)) :blank)
                    (:field :body (:choice enumerator_list :blank)))
                   (:field :body enumerator_list))
                  (:choice attribute_specifier :blank))
  enumerator_list (:seq
                   "{"
                   (:repeat
                    (:choice
                     (:seq enumerator ",")
                     (:alias preproc_if_in_enumerator_list preproc_if)
                     (:alias preproc_ifdef_in_enumerator_list preproc_ifdef)
                     (:seq preproc_call ",")))
                   (:choice
                    (:seq
                     (:choice
                      enumerator
                      (:alias preproc_if_in_enumerator_list_no_comma preproc_if)
                      (:alias preproc_ifdef_in_enumerator_list_no_comma preproc_ifdef)
                      preproc_call))
                    :blank)
                   "}")
  struct_specifier (:prec-right 0
                    (:seq
                     "struct"
                     (:choice attribute_specifier :blank)
                     (:choice ms_declspec_modifier :blank)
                     (:choice
                      (:seq
                       (:field :name _type_identifier)
                       (:field :body (:choice field_declaration_list :blank)))
                      (:field :body field_declaration_list))
                     (:choice attribute_specifier :blank)))
  union_specifier (:prec-right 0
                   (:seq
                    "union"
                    (:choice ms_declspec_modifier :blank)
                    (:choice
                     (:seq
                      (:field :name _type_identifier)
                      (:field :body (:choice field_declaration_list :blank)))
                     (:field :body field_declaration_list))
                    (:choice attribute_specifier :blank)))
  field_declaration_list (:seq "{" (:repeat _field_declaration_list_item) "}")
  _field_declaration_list_item (:choice
                                field_declaration
                                preproc_def
                                preproc_function_def
                                preproc_call
                                (:alias preproc_if_in_field_declaration_list preproc_if)
                                (:alias preproc_ifdef_in_field_declaration_list preproc_ifdef))
  field_declaration (:seq
                     _declaration_specifiers
                     (:choice _field_declaration_declarator :blank)
                     (:choice attribute_specifier :blank)
                     ";")
  _field_declaration_declarator (:seq
                                 (:seq
                                  (:field :declarator _field_declarator)
                                  (:choice bitfield_clause :blank))
                                 (:repeat
                                  (:seq
                                   ","
                                   (:seq
                                    (:field :declarator _field_declarator)
                                    (:choice bitfield_clause :blank)))))
  bitfield_clause (:seq ":" expression)
  enumerator (:seq (:field :name identifier) (:choice (:seq "=" (:field :value expression)) :blank))
  variadic_parameter "..."
  parameter_list (:seq
                  "("
                  (:choice
                   (:choice
                    (:seq
                     (:choice parameter_declaration variadic_parameter)
                     (:repeat (:seq "," (:choice parameter_declaration variadic_parameter))))
                    :blank)
                   compound_statement)
                  ")")
  _old_style_parameter_list (:seq
                             "("
                             (:choice
                              (:seq
                               (:choice identifier variadic_parameter)
                               (:repeat (:seq "," (:choice identifier variadic_parameter))))
                              :blank)
                             ")")
  parameter_declaration (:seq
                         _declaration_specifiers
                         (:choice
                          (:field :declarator (:choice _declarator _abstract_declarator))
                          :blank)
                         (:repeat attribute_specifier))
  attributed_statement (:seq (:repeat1 attribute_declaration) statement)
  statement (:choice case_statement _non_case_statement)
  _non_case_statement (:choice
                       attributed_statement
                       labeled_statement
                       compound_statement
                       expression_statement
                       if_statement
                       switch_statement
                       do_statement
                       while_statement
                       for_statement
                       return_statement
                       break_statement
                       continue_statement
                       goto_statement
                       seh_try_statement
                       seh_leave_statement)
  _top_level_statement (:choice
                        case_statement
                        attributed_statement
                        labeled_statement
                        compound_statement
                        (:alias _top_level_expression_statement expression_statement)
                        if_statement
                        switch_statement
                        do_statement
                        while_statement
                        for_statement
                        return_statement
                        break_statement
                        continue_statement
                        goto_statement)
  labeled_statement (:seq (:field :label _statement_identifier) ":" (:choice declaration statement))
  _top_level_expression_statement (:seq (:choice _expression_not_binary :blank) ";")
  expression_statement (:seq (:choice (:choice expression comma_expression) :blank) ";")
  if_statement (:prec-right 0
                (:seq
                 "if"
                 (:field :condition parenthesized_expression)
                 (:field :consequence statement)
                 (:choice (:field :alternative else_clause) :blank)))
  else_clause (:seq "else" statement)
  switch_statement (:seq
                    "switch"
                    (:field :condition parenthesized_expression)
                    (:field :body compound_statement))
  case_statement (:prec-right 0
                  (:seq
                   (:choice (:seq "case" (:field :value expression)) "default")
                   ":"
                   (:repeat (:choice _non_case_statement declaration type_definition))))
  while_statement (:seq
                   "while"
                   (:field :condition parenthesized_expression)
                   (:field :body statement))
  do_statement (:seq
                "do"
                (:field :body statement)
                "while"
                (:field :condition parenthesized_expression)
                ";")
  for_statement (:seq "for" "(" _for_statement_body ")" (:field :body statement))
  _for_statement_body (:seq
                       (:choice
                        (:field :initializer declaration)
                        (:seq
                         (:field :initializer
                          (:choice (:choice expression comma_expression) :blank))
                         ";"))
                       (:field :condition (:choice (:choice expression comma_expression) :blank))
                       ";"
                       (:field :update (:choice (:choice expression comma_expression) :blank)))
  return_statement (:seq "return" (:choice (:choice expression comma_expression) :blank) ";")
  break_statement (:seq "break" ";")
  continue_statement (:seq "continue" ";")
  goto_statement (:seq "goto" (:field :label _statement_identifier) ";")
  seh_try_statement (:seq
                     "__try"
                     (:field :body compound_statement)
                     (:choice seh_except_clause seh_finally_clause))
  seh_except_clause (:seq
                     "__except"
                     (:field :filter parenthesized_expression)
                     (:field :body compound_statement))
  seh_finally_clause (:seq "__finally" (:field :body compound_statement))
  seh_leave_statement (:seq "__leave" ";")
  expression (:choice _expression_not_binary binary_expression)
  _expression_not_binary (:choice
                          conditional_expression
                          assignment_expression
                          unary_expression
                          update_expression
                          cast_expression
                          pointer_expression
                          sizeof_expression
                          alignof_expression
                          offsetof_expression
                          generic_expression
                          subscript_expression
                          call_expression
                          field_expression
                          compound_literal_expression
                          identifier
                          number_literal
                          _string
                          (:ref "true")
                          (:ref "false")
                          null
                          char_literal
                          parenthesized_expression
                          gnu_asm_expression
                          extension_expression)
  _string (:prec-left 0 (:choice string_literal concatenated_string))
  comma_expression (:seq
                    (:field :left expression)
                    ","
                    (:field :right (:choice expression comma_expression)))
  conditional_expression (:prec-right -1
                          (:seq
                           (:field :condition expression)
                           "?"
                           (:choice
                            (:field :consequence (:choice expression comma_expression))
                            :blank)
                           ":"
                           (:field :alternative expression)))
  _assignment_left_expression (:choice
                               identifier
                               call_expression
                               field_expression
                               pointer_expression
                               subscript_expression
                               parenthesized_expression)
  assignment_expression (:prec-right -2
                         (:seq
                          (:field :left _assignment_left_expression)
                          (:field :operator
                           (:choice "=" "*=" "/=" "%=" "+=" "-=" "<<=" ">>=" "&=" "^=" "|="))
                          (:field :right expression)))
  pointer_expression (:prec-left 12
                      (:seq (:field :operator (:choice "*" "&")) (:field :argument expression)))
  unary_expression (:prec-left 14
                    (:seq
                     (:field :operator (:choice "!" "~" "-" "+"))
                     (:field :argument expression)))
  binary_expression (:choice
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "+")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "-")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator "*")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator "/")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
                       (:field :right expression)))
                     (:prec-left 1
                      (:seq
                       (:field :left expression)
                       (:field :operator "||")
                       (:field :right expression)))
                     (:prec-left 2
                      (:seq
                       (:field :left expression)
                       (:field :operator "&&")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator "^")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "==")
                       (:field :right expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "!=")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator ">")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator ">=")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "<")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "<<")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>")
                       (:field :right expression))))
  update_expression (:prec-right 14
                     (:choice
                      (:seq (:field :operator (:choice "--" "++")) (:field :argument expression))
                      (:seq (:field :argument expression) (:field :operator (:choice "--" "++")))))
  cast_expression (:prec 12
                   (:seq "(" (:field :type type_descriptor) ")" (:field :value expression)))
  type_descriptor (:seq
                   (:repeat type_qualifier)
                   (:field :type type_specifier)
                   (:repeat type_qualifier)
                   (:field :declarator (:choice _abstract_declarator :blank)))
  sizeof_expression (:prec 13
                     (:seq
                      "sizeof"
                      (:choice
                       (:field :value expression)
                       (:seq "(" (:field :type type_descriptor) ")"))))
  alignof_expression (:prec 13
                      (:seq
                       (:choice "__alignof__" "__alignof" "_alignof" "alignof" "_Alignof")
                       (:seq "(" (:field :type type_descriptor) ")")))
  offsetof_expression (:prec 8
                       (:seq
                        "offsetof"
                        (:seq
                         "("
                         (:field :type type_descriptor)
                         ","
                         (:field :member _field_identifier)
                         ")")))
  generic_expression (:prec 15
                      (:seq
                       "_Generic"
                       "("
                       expression
                       ","
                       (:seq
                        (:seq type_descriptor ":" expression)
                        (:repeat (:seq "," (:seq type_descriptor ":" expression))))
                       ")"))
  subscript_expression (:prec 17
                        (:seq (:field :argument expression) "[" (:field :index expression) "]"))
  call_expression (:prec 15 (:seq (:field :function expression) (:field :arguments argument_list)))
  gnu_asm_expression (:prec 15
                      (:seq
                       (:choice "asm" "__asm__" "__asm")
                       (:repeat gnu_asm_qualifier)
                       "("
                       (:field :assembly_code _string)
                       (:choice
                        (:seq
                         (:field :output_operands gnu_asm_output_operand_list)
                         (:choice
                          (:seq
                           (:field :input_operands gnu_asm_input_operand_list)
                           (:choice
                            (:seq
                             (:field :clobbers gnu_asm_clobber_list)
                             (:choice (:field :goto_labels gnu_asm_goto_list) :blank))
                            :blank))
                          :blank))
                        :blank)
                       ")"))
  gnu_asm_qualifier (:choice "volatile" "__volatile__" "inline" "goto")
  gnu_asm_output_operand_list (:seq
                               ":"
                               (:choice
                                (:seq
                                 (:field :operand gnu_asm_output_operand)
                                 (:repeat (:seq "," (:field :operand gnu_asm_output_operand))))
                                :blank))
  gnu_asm_output_operand (:seq
                          (:choice (:seq "[" (:field :symbol identifier) "]") :blank)
                          (:field :constraint string_literal)
                          "("
                          (:field :value expression)
                          ")")
  gnu_asm_input_operand_list (:seq
                              ":"
                              (:choice
                               (:seq
                                (:field :operand gnu_asm_input_operand)
                                (:repeat (:seq "," (:field :operand gnu_asm_input_operand))))
                               :blank))
  gnu_asm_input_operand (:seq
                         (:choice (:seq "[" (:field :symbol identifier) "]") :blank)
                         (:field :constraint string_literal)
                         "("
                         (:field :value expression)
                         ")")
  gnu_asm_clobber_list (:seq
                        ":"
                        (:choice
                         (:seq
                          (:field :register _string)
                          (:repeat (:seq "," (:field :register _string))))
                         :blank))
  gnu_asm_goto_list (:seq
                     ":"
                     (:choice
                      (:seq
                       (:field :label identifier)
                       (:repeat (:seq "," (:field :label identifier))))
                      :blank))
  extension_expression (:seq "__extension__" expression)
  argument_list (:seq
                 "("
                 (:choice
                  (:seq
                   (:choice expression compound_statement)
                   (:repeat (:seq "," (:choice expression compound_statement))))
                  :blank)
                 ")")
  field_expression (:seq
                    (:prec 16
                     (:seq (:field :argument expression) (:field :operator (:choice "." "->"))))
                    (:field :field _field_identifier))
  compound_literal_expression (:seq
                               "("
                               (:field :type type_descriptor)
                               ")"
                               (:field :value initializer_list))
  parenthesized_expression (:seq "(" (:choice expression comma_expression compound_statement) ")")
  initializer_list (:seq
                    "{"
                    (:choice
                     (:seq
                      (:choice initializer_pair expression initializer_list)
                      (:repeat (:seq "," (:choice initializer_pair expression initializer_list))))
                     :blank)
                    (:choice "," :blank)
                    "}")
  initializer_pair (:choice
                    (:seq
                     (:field :designator
                      (:repeat1
                       (:choice subscript_designator field_designator subscript_range_designator)))
                     "="
                     (:field :value (:choice expression initializer_list)))
                    (:seq
                     (:field :designator _field_identifier)
                     ":"
                     (:field :value (:choice expression initializer_list))))
  subscript_designator (:seq "[" expression "]")
  subscript_range_designator (:seq
                              "["
                              (:field :start expression)
                              "..."
                              (:field :end expression)
                              "]")
  field_designator (:seq "." _field_identifier)
  number_literal (:token
                  (:seq
                   (:choice (:pattern "[-\\+]") :blank)
                   (:choice (:choice (:pattern "0[xX]") (:pattern "0[bB]")) :blank)
                   (:choice
                    (:seq
                     (:choice
                      (:seq
                       (:repeat1 (:pattern "[0-9]"))
                       (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]")))))
                      (:seq
                       (:pattern "0[bB]")
                       (:seq
                        (:repeat1 (:pattern "[0-9]"))
                        (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]"))))))
                      (:seq
                       (:pattern "0[xX]")
                       (:seq
                        (:repeat1 (:pattern "[0-9a-fA-F]"))
                        (:repeat (:seq "'" (:repeat1 (:pattern "[0-9a-fA-F]")))))))
                     (:choice
                      (:seq
                       "."
                       (:choice
                        (:seq
                         (:repeat1 (:pattern "[0-9a-fA-F]"))
                         (:repeat (:seq "'" (:repeat1 (:pattern "[0-9a-fA-F]")))))
                        :blank))
                      :blank))
                    (:seq
                     "."
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]")))))))
                   (:choice
                    (:seq
                     (:pattern "[eEpP]")
                     (:choice
                      (:seq
                       (:choice (:pattern "[-\\+]") :blank)
                       (:seq
                        (:repeat1 (:pattern "[0-9a-fA-F]"))
                        (:repeat (:seq "'" (:repeat1 (:pattern "[0-9a-fA-F]"))))))
                      :blank))
                    :blank)
                   (:pattern "[uUlLwWfFbBdD]*")))
  char_literal (:seq
                (:choice "L'" "u'" "U'" "u8'" "'")
                (:repeat1
                 (:choice
                  escape_sequence
                  (:alias (:token-immediate (:pattern "[^\\n']")) character)))
                "'")
  concatenated_string (:prec-right 0
                       (:seq
                        (:choice
                         (:seq identifier string_literal)
                         (:seq string_literal string_literal)
                         (:seq string_literal identifier))
                        (:repeat (:choice string_literal identifier))))
  string_literal (:seq
                  (:choice "L\"" "u\"" "U\"" "u8\"" "\"")
                  (:repeat
                   (:choice
                    (:alias (:token-immediate (:prec 1 (:pattern "[^\\\\\"\\n]+"))) string_content)
                    escape_sequence))
                  "\"")
  escape_sequence (:token
                   (:prec 1
                    (:seq
                     "\\"
                     (:choice
                      (:pattern "[^xuU]")
                      (:pattern "\\d{2,3}")
                      (:pattern "x[0-9a-fA-F]{1,4}")
                      (:pattern "u[0-9a-fA-F]{4}")
                      (:pattern "U[0-9a-fA-F]{8}")))))
  system_lib_string (:token (:seq "<" (:repeat (:choice (:pattern "[^>\\n]") "\\>")) ">"))
  (:ref "true") (:token (:choice "TRUE" "true"))
  (:ref "false") (:token (:choice "FALSE" "false"))
  null (:choice "NULL" "nullptr")
  identifier (:pattern "(\\p{XID_Start}|\\$|_|\\\\u[0-9A-Fa-f]{4}|\\\\U[0-9A-Fa-f]{8})(\\p{XID_Continue}|\\$|\\\\u[0-9A-Fa-f]{4}|\\\\U[0-9A-Fa-f]{8})*")
  _type_identifier (:alias identifier type_identifier)
  _field_identifier (:alias identifier field_identifier)
  _statement_identifier (:alias identifier statement_identifier)
  _empty_declaration (:seq type_specifier ";")
  macro_type_specifier (:prec-dynamic -1
                        (:seq (:field :name identifier) "(" (:field :type type_descriptor) ")"))
  comment (:token
           (:choice
            (:seq "//" (:pattern "(\\\\+(.|\\r?\\n)|[^\\\\\\n])*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))}}
