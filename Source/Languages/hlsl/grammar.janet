# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "hlsl"
 :word identifier
 :inherits "cpp"
 :extras [(:pattern "\\s|\\\\\\r?\\n") comment]
 :conflicts [[type_specifier _declarator]
             [type_specifier expression]
             [sized_type_specifier]
             [attributed_statement]
             [_declaration_modifiers attributed_statement]
             [_declaration_modifiers using_declaration]
             [_declaration_modifiers attributed_statement using_declaration]
             [_top_level_item _top_level_statement]
             [_block_item statement]
             [type_qualifier extension_expression]
             [template_function template_type]
             [template_function template_type expression]
             [template_function template_type qualified_identifier]
             [template_type qualified_type_identifier]
             [qualified_type_identifier qualified_identifier]
             [comma_expression initializer_list]
             [expression _declarator]
             [expression structured_binding_declarator]
             [expression _declarator type_specifier]
             [expression identifier_parameter_pack_expansion]
             [expression _lambda_capture_identifier]
             [expression _lambda_capture]
             [expression structured_binding_declarator _lambda_capture_identifier]
             [structured_binding_declarator _lambda_capture_identifier]
             [parameter_list argument_list]
             [type_specifier call_expression]
             [_declaration_specifiers _constructor_specifiers]
             [_binary_fold_operator _fold_operator]
             [_function_declarator_seq]
             [type_specifier sized_type_specifier]
             [initializer_pair comma_expression]
             [expression_statement _for_statement_body]
             [init_statement _for_statement_body]
             [field_expression template_method template_type]
             [qualified_field_identifier template_method template_type]
             [function_declarator]]
 :precedences [[argument_list type_qualifier] [_expression_not_binary _class_name]]
 :externals [raw_string_delimiter raw_string_content]
 :inline [_type_identifier
          _field_identifier
          _statement_identifier
          _non_case_statement
          _assignment_left_expression
          _expression_not_binary
          _namespace_identifier]
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
                   preproc_call
                   namespace_definition
                   concept_definition
                   namespace_alias_definition
                   using_declaration
                   alias_declaration
                   static_assert_declaration
                   template_declaration
                   template_instantiation
                   module_declaration
                   export_declaration
                   import_declaration
                   global_module_fragment_declaration
                   private_module_fragment_declaration
                   (:alias constructor_or_destructor_definition function_definition)
                   (:alias operator_cast_definition function_definition)
                   (:alias operator_cast_declaration declaration))
  _block_item (:choice
               function_definition
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
               preproc_call
               namespace_definition
               concept_definition
               namespace_alias_definition
               using_declaration
               alias_declaration
               static_assert_declaration
               template_declaration
               template_instantiation
               (:alias constructor_or_destructor_definition function_definition)
               (:alias operator_cast_definition function_definition)
               (:alias operator_cast_declaration declaration))
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
                       (:choice hlsl_attribute :blank)
                       (:seq
                        (:choice ms_call_modifier :blank)
                        _declaration_specifiers
                        (:choice ms_call_modifier :blank)
                        (:field :declarator _declarator)
                        (:field :body (:choice compound_statement try_statement))))
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
                  (:seq _declarator (:choice (:alias (:seq ":" expression) semantics) :blank))
                  init_declarator))
                (:repeat
                 (:seq
                  ","
                  (:field :declarator
                   (:choice
                    (:seq _declarator (:choice (:alias (:seq ":" expression) semantics) :blank))
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
                          "in"
                          "out"
                          "inout"
                          qualifiers
                          (:choice
                           (:choice
                            storage_class_specifier
                            type_qualifier
                            attribute_specifier
                            attribute_declaration
                            ms_declspec_modifier)
                           "virtual"))
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
             (:choice (:seq "using" (:field :namespace identifier) ":") :blank)
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
               (:choice
                attributed_declarator
                pointer_declarator
                function_declarator
                array_declarator
                parenthesized_declarator
                identifier)
               reference_declarator
               qualified_identifier
               template_function
               operator_name
               destructor_name
               structured_binding_declarator)
  _declaration_declarator (:choice
                           attributed_declarator
                           pointer_declarator
                           (:alias _function_declaration_declarator function_declarator)
                           array_declarator
                           parenthesized_declarator
                           identifier)
  _field_declarator (:choice
                     (:choice
                      (:alias attributed_field_declarator attributed_declarator)
                      (:alias pointer_field_declarator pointer_declarator)
                      (:alias function_field_declarator function_declarator)
                      (:alias array_field_declarator array_declarator)
                      (:alias parenthesized_field_declarator parenthesized_declarator)
                      _field_identifier)
                     (:alias reference_field_declarator reference_declarator)
                     template_method
                     operator_name)
  _type_declarator (:choice
                    (:choice
                     (:alias attributed_type_declarator attributed_declarator)
                     (:alias pointer_type_declarator pointer_declarator)
                     (:alias function_type_declarator function_declarator)
                     (:alias array_type_declarator array_declarator)
                     (:alias parenthesized_type_declarator parenthesized_declarator)
                     _type_identifier
                     (:alias (:choice "signed" "unsigned" "long" "short") primitive_type)
                     primitive_type)
                    (:alias reference_type_declarator reference_declarator))
  _abstract_declarator (:choice
                        (:choice
                         abstract_pointer_declarator
                         abstract_function_declarator
                         abstract_array_declarator
                         abstract_parenthesized_declarator)
                        abstract_reference_declarator)
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
  function_declarator (:seq
                       (:prec-dynamic 1
                        (:seq (:field :declarator _declarator) _function_declarator_seq))
                       (:choice semantics :blank))
  _function_declaration_declarator (:prec-right 1
                                    (:seq
                                     (:field :declarator _declarator)
                                     (:field :parameters parameter_list)
                                     (:choice gnu_asm_expression :blank)
                                     (:repeat attribute_specifier)))
  function_field_declarator (:prec-dynamic 1
                             (:seq (:field :declarator _field_declarator) _function_declarator_seq))
  function_type_declarator (:prec 1
                            (:seq
                             (:field :declarator _type_declarator)
                             (:field :parameters parameter_list)))
  abstract_function_declarator (:seq
                                (:field :declarator (:choice _abstract_declarator :blank))
                                _function_declarator_seq)
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
  init_declarator (:choice
                   (:seq
                    (:field :declarator _declarator)
                    "="
                    (:field :value (:choice initializer_list expression)))
                   (:seq
                    (:field :declarator _declarator)
                    (:field :value (:choice argument_list initializer_list))))
  compound_statement (:prec -1 (:seq "{" (:repeat _block_item) "}"))
  storage_class_specifier (:choice
                           "extern"
                           "static"
                           "register"
                           "inline"
                           "__inline"
                           "__inline__"
                           "__forceinline"
                           "thread_local"
                           "__thread"
                           "thread_local")
  type_qualifier (:choice
                  (:choice
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
                  "mutable"
                  "constinit"
                  "consteval")
  alignas_qualifier (:seq
                     (:choice "alignas" "_Alignas")
                     "("
                     (:choice expression type_descriptor)
                     ")")
  type_specifier (:choice
                  struct_specifier
                  union_specifier
                  enum_specifier
                  class_specifier
                  sized_type_specifier
                  primitive_type
                  template_type
                  dependent_type
                  placeholder_type_specifier
                  decltype
                  (:prec-right 0
                   (:choice
                    (:alias qualified_type_identifier qualified_identifier)
                    _type_identifier)))
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
  enum_specifier (:prec-right 0
                  (:seq
                   "enum"
                   (:choice (:choice "class" "struct") :blank)
                   (:choice
                    (:seq
                     (:field :name _class_name)
                     (:choice _enum_base_clause :blank)
                     (:choice (:field :body enumerator_list) :blank))
                    (:field :body enumerator_list))
                   (:choice attribute_specifier :blank)))
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
  struct_specifier (:seq "struct" _class_declaration)
  union_specifier (:seq "union" _class_declaration)
  field_declaration_list (:seq "{" (:repeat _field_declaration_list_item) "}")
  _field_declaration_list_item (:choice
                                (:choice
                                 field_declaration
                                 preproc_def
                                 preproc_function_def
                                 preproc_call
                                 (:alias preproc_if_in_field_declaration_list preproc_if)
                                 (:alias preproc_ifdef_in_field_declaration_list preproc_ifdef))
                                template_declaration
                                (:alias inline_method_definition function_definition)
                                (:alias constructor_or_destructor_definition function_definition)
                                (:alias constructor_or_destructor_declaration declaration)
                                (:alias operator_cast_definition function_definition)
                                (:alias operator_cast_declaration declaration)
                                friend_declaration
                                (:seq access_specifier ":")
                                alias_declaration
                                using_declaration
                                type_definition
                                static_assert_declaration
                                ";")
  field_declaration (:seq
                     _declaration_specifiers
                     (:choice
                      (:seq
                       (:seq
                        (:field :declarator _field_declarator)
                        (:choice
                         (:choice
                          bitfield_clause
                          (:field :default_value initializer_list)
                          (:seq "=" (:field :default_value (:choice expression initializer_list))))
                         :blank))
                       (:repeat
                        (:seq
                         ","
                         (:seq
                          (:field :declarator _field_declarator)
                          (:choice
                           (:choice
                            bitfield_clause
                            (:field :default_value initializer_list)
                            (:seq "=" (:field :default_value (:choice expression initializer_list))))
                           :blank)))))
                      :blank)
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
                   (:seq
                    (:choice
                     parameter_declaration
                     optional_parameter_declaration
                     variadic_parameter_declaration
                     "...")
                    (:repeat
                     (:seq
                      ","
                      (:choice
                       parameter_declaration
                       optional_parameter_declaration
                       variadic_parameter_declaration
                       "..."))))
                   :blank)
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
                         (:seq
                          _declaration_specifiers
                          (:choice
                           (:field :declarator (:choice _declarator _abstract_declarator))
                           :blank)
                          (:repeat attribute_specifier))
                         (:choice semantics :blank))
  attributed_statement (:seq (:repeat1 attribute_declaration) statement)
  statement (:choice case_statement _non_case_statement)
  _non_case_statement (:choice
                       discard_statement
                       cbuffer_specifier
                       (:choice
                        (:choice
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
                        co_return_statement
                        co_yield_statement
                        for_range_loop
                        try_statement
                        throw_statement))
  _top_level_statement (:choice
                        (:choice
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
                        co_return_statement
                        co_yield_statement
                        for_range_loop
                        try_statement
                        throw_statement)
  labeled_statement (:seq (:field :label _statement_identifier) ":" (:choice declaration statement))
  _top_level_expression_statement (:seq (:choice _expression_not_binary :blank) ";")
  expression_statement (:seq (:choice (:choice expression comma_expression) :blank) ";")
  if_statement (:seq
                (:choice hlsl_attribute :blank)
                (:prec-right 0
                 (:seq
                  "if"
                  (:choice "constexpr" :blank)
                  (:field :condition condition_clause)
                  (:field :consequence statement)
                  (:choice (:field :alternative else_clause) :blank))))
  else_clause (:seq "else" statement)
  switch_statement (:seq
                    "switch"
                    (:field :condition condition_clause)
                    (:field :body compound_statement))
  case_statement (:prec-right 0
                  (:seq
                   (:choice (:seq "case" (:field :value expression)) "default")
                   ":"
                   (:repeat (:choice _non_case_statement declaration type_definition))))
  while_statement (:seq "while" (:field :condition condition_clause) (:field :body statement))
  do_statement (:seq
                "do"
                (:field :body statement)
                "while"
                (:field :condition parenthesized_expression)
                ";")
  for_statement (:seq
                 (:choice hlsl_attribute :blank)
                 (:seq "for" "(" _for_statement_body ")" (:field :body statement)))
  _for_statement_body (:prec-dynamic 1
                       (:seq
                        (:choice
                         (:field :initializer declaration)
                         (:seq
                          (:field :initializer
                           (:choice (:choice expression comma_expression) :blank))
                          ";"))
                        (:field :condition (:choice (:choice expression comma_expression) :blank))
                        ";"
                        (:field :update (:choice (:choice expression comma_expression) :blank))))
  return_statement (:seq
                    (:choice
                     (:seq "return" (:choice (:choice expression comma_expression) :blank) ";")
                     (:seq "return" initializer_list ";")))
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
                          (:choice
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
                          co_await_expression
                          requires_expression
                          requires_clause
                          template_function
                          qualified_identifier
                          new_expression
                          delete_expression
                          lambda_expression
                          parameter_pack_expansion
                          this
                          user_defined_literal
                          fold_expression)
  _string (:choice string_literal raw_string_literal concatenated_string)
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
                               (:choice
                                identifier
                                call_expression
                                field_expression
                                pointer_expression
                                subscript_expression
                                parenthesized_expression)
                               qualified_identifier
                               user_defined_literal)
  assignment_expression (:prec-right -2
                         (:seq
                          (:field :left _assignment_left_expression)
                          (:field :operator
                           (:choice
                            "="
                            "*="
                            "/="
                            "%="
                            "+="
                            "-="
                            "<<="
                            ">>="
                            "&="
                            "^="
                            "|="
                            "and_eq"
                            "or_eq"
                            "xor_eq"))
                          (:field :right (:choice expression initializer_list))))
  pointer_expression (:prec-left 12
                      (:seq (:field :operator (:choice "*" "&")) (:field :argument expression)))
  unary_expression (:choice
                    (:prec-left 14
                     (:seq
                      (:field :operator (:choice "!" "~" "-" "+"))
                      (:field :argument expression)))
                    (:prec-left 14
                     (:seq (:field :operator (:choice "not" "compl")) (:field :argument expression))))
  binary_expression (:choice
                     (:choice
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
                     (:prec-left 8
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=>")
                       (:field :right expression)))
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
                       (:field :operator "bitor")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator "xor")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator "bitand")
                       (:field :right expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "not_eq")
                       (:field :right expression))))
  update_expression (:prec-right 14
                     (:choice
                      (:seq (:field :operator (:choice "--" "++")) (:field :argument expression))
                      (:seq (:field :argument expression) (:field :operator (:choice "--" "++")))))
  cast_expression (:prec 12
                   (:seq "(" (:field :type type_descriptor) ")" (:field :value expression)))
  type_descriptor (:prec-right 0
                   (:seq
                    (:repeat type_qualifier)
                    (:field :type type_specifier)
                    (:repeat type_qualifier)
                    (:field :declarator (:choice _abstract_declarator :blank))))
  sizeof_expression (:prec-right 13
                     (:choice
                      (:prec 13
                       (:seq
                        "sizeof"
                        (:choice
                         (:field :value expression)
                         (:seq "(" (:field :type type_descriptor) ")"))))
                      (:seq "sizeof" "..." "(" (:field :value identifier) ")")))
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
                        (:seq
                         (:field :argument expression)
                         (:field :indices subscript_argument_list)))
  call_expression (:choice
                   (:prec 15 (:seq (:field :function expression) (:field :arguments argument_list)))
                   (:seq (:field :function primitive_type) (:field :arguments argument_list)))
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
                   (:choice expression initializer_list compound_statement)
                   (:repeat (:seq "," (:choice expression initializer_list compound_statement))))
                  :blank)
                 ")")
  field_expression (:seq
                    (:prec 16
                     (:seq (:field :argument expression) (:field :operator (:choice "." ".*" "->"))))
                    (:field :field
                     (:choice
                      (:prec-dynamic 1 _field_identifier)
                      (:alias qualified_field_identifier qualified_identifier)
                      destructor_name
                      template_method
                      (:alias dependent_field_identifier dependent_name))))
  compound_literal_expression (:choice
                               (:seq
                                "("
                                (:field :type type_descriptor)
                                ")"
                                (:field :value initializer_list))
                               (:seq
                                (:field :type (:choice _class_name primitive_type))
                                (:field :value initializer_list)))
  parenthesized_expression (:choice
                            (:seq "(" (:choice expression comma_expression compound_statement) ")")
                            (:seq "(" (:alias _assignment_expression_lhs assignment_expression) ")"))
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
                   (:choice
                    (:seq
                     (:choice
                      (:seq
                       (:choice "0b" "0B")
                       (:seq
                        (:repeat1 (:pattern "[01]"))
                        (:repeat (:seq "'" (:repeat1 (:pattern "[01]"))))))
                      (:seq
                       (:pattern "[1-9]")
                       (:repeat (:pattern "[0-9]"))
                       (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]")))))
                      (:seq
                       (:choice "0x" "0X")
                       (:seq
                        (:repeat1 (:pattern "[0-9a-fA-F]"))
                        (:repeat (:seq "'" (:repeat1 (:pattern "[0-9a-fA-F]"))))))
                      (:seq
                       "0"
                       (:repeat (:pattern "[0-7]"))
                       (:repeat (:seq "'" (:repeat1 (:pattern "[0-7]"))))))
                     (:choice
                      (:pattern "(ll|LL)[uU]?|[uU](ll|LL)?|[uU][lL]?|[uU][zZ]?|[lL][uU]?|[zZ][uU]?")
                      :blank))
                    (:seq
                     (:choice
                      (:seq
                       (:seq
                        (:repeat1 (:pattern "[0-9]"))
                        (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]")))))
                       (:seq
                        (:pattern "[eE]")
                        (:choice (:pattern "[-\\+]") :blank)
                        (:seq
                         (:repeat1 (:pattern "[0-9]"))
                         (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]")))))))
                      (:seq
                       (:seq
                        (:repeat1 (:pattern "[0-9]"))
                        (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]")))))
                       "."
                       (:choice
                        (:seq
                         (:repeat1 (:pattern "[0-9]"))
                         (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]")))))
                        :blank)
                       (:choice
                        (:seq
                         (:pattern "[eE]")
                         (:choice (:pattern "[-\\+]") :blank)
                         (:seq
                          (:repeat1 (:pattern "[0-9]"))
                          (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]"))))))
                        :blank))
                      (:seq
                       "."
                       (:seq
                        (:repeat1 (:pattern "[0-9]"))
                        (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]")))))
                       (:choice
                        (:seq
                         (:pattern "[eE]")
                         (:choice (:pattern "[-\\+]") :blank)
                         (:seq
                          (:repeat1 (:pattern "[0-9]"))
                          (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]"))))))
                        :blank))
                      (:seq
                       (:choice "0x" "0X")
                       (:choice
                        (:seq
                         (:repeat1 (:pattern "[0-9a-fA-F]"))
                         (:repeat (:seq "'" (:repeat1 (:pattern "[0-9a-fA-F]")))))
                        (:seq
                         (:seq
                          (:repeat1 (:pattern "[0-9a-fA-F]"))
                          (:repeat (:seq "'" (:repeat1 (:pattern "[0-9a-fA-F]")))))
                         "."
                         (:choice
                          (:seq
                           (:repeat1 (:pattern "[0-9a-fA-F]"))
                           (:repeat (:seq "'" (:repeat1 (:pattern "[0-9a-fA-F]")))))
                          :blank))
                        (:seq
                         "."
                         (:seq
                          (:repeat1 (:pattern "[0-9a-fA-F]"))
                          (:repeat (:seq "'" (:repeat1 (:pattern "[0-9a-fA-F]")))))))
                       (:seq
                        (:pattern "[pP]")
                        (:choice (:pattern "[-\\+]") :blank)
                        (:seq
                         (:repeat1 (:pattern "[0-9]"))
                         (:repeat (:seq "'" (:repeat1 (:pattern "[0-9]"))))))))
                     (:choice (:pattern "([fF](16|32|64|128)?)|[lL]|(bf16|BF16)") :blank)))))
  char_literal (:seq
                (:choice "L'" "u'" "U'" "u8'" "'")
                (:repeat1
                 (:choice
                  escape_sequence
                  (:alias (:token-immediate (:pattern "[^\\n']")) character)))
                "'")
  concatenated_string (:prec-right 0
                       (:seq
                        (:choice identifier string_literal raw_string_literal)
                        (:choice string_literal raw_string_literal)
                        (:repeat (:choice identifier string_literal raw_string_literal))))
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
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))
  placeholder_type_specifier (:prec 1
                              (:seq
                               (:field :constraint (:choice type_specifier :blank))
                               (:choice auto (:alias decltype_auto decltype))))
  auto "auto"
  decltype_auto (:seq "decltype" "(" auto ")")
  decltype (:seq "decltype" "(" expression ")")
  _class_declaration (:seq
                      (:repeat (:choice attribute_specifier alignas_qualifier))
                      (:choice ms_declspec_modifier :blank)
                      (:repeat attribute_declaration)
                      _class_declaration_item)
  _class_declaration_item (:prec-right 0
                           (:seq
                            (:choice
                             (:field :name _class_name)
                             (:seq
                              (:choice (:field :name _class_name) :blank)
                              (:choice virtual_specifier :blank)
                              (:choice base_class_clause :blank)
                              (:field :body field_declaration_list)))
                            (:choice attribute_specifier :blank)))
  class_specifier (:seq "class" _class_declaration)
  _class_name (:prec-right 0
               (:choice
                _type_identifier
                template_type
                (:alias qualified_type_identifier qualified_identifier)))
  virtual_specifier (:choice "final" "override")
  explicit_function_specifier (:choice "explicit" (:prec 15 (:seq "explicit" "(" expression ")")))
  base_class_clause (:seq
                     ":"
                     (:seq
                      (:seq
                       (:repeat attribute_declaration)
                       (:choice
                        (:choice
                         access_specifier
                         (:seq access_specifier (:choice "virtual" :blank))
                         (:seq "virtual" (:choice access_specifier :blank)))
                        :blank)
                       _class_name
                       (:choice "..." :blank))
                      (:repeat
                       (:seq
                        ","
                        (:seq
                         (:repeat attribute_declaration)
                         (:choice
                          (:choice
                           access_specifier
                           (:seq access_specifier (:choice "virtual" :blank))
                           (:seq "virtual" (:choice access_specifier :blank)))
                          :blank)
                         _class_name
                         (:choice "..." :blank))))))
  _enum_base_clause (:prec-left 0
                     (:seq
                      ":"
                      (:field :base
                       (:choice
                        (:alias qualified_type_identifier qualified_identifier)
                        _type_identifier
                        primitive_type
                        sized_type_specifier))))
  dependent_type (:prec-dynamic -1 (:prec-right 0 (:seq "typename" type_specifier)))
  module_name (:seq identifier (:repeat (:seq "." identifier)))
  module_partition (:seq ":" module_name)
  module_declaration (:seq
                      (:choice "export" :blank)
                      "module"
                      (:field :name module_name)
                      (:field :partition (:choice module_partition :blank))
                      (:choice attribute_declaration :blank)
                      ";")
  export_declaration (:seq "export" (:choice _block_item (:seq "{" (:repeat _block_item) "}")))
  import_declaration (:seq
                      (:choice "export" :blank)
                      "import"
                      (:choice
                       (:field :name module_name)
                       (:field :partition module_partition)
                       (:field :header (:choice string_literal system_lib_string)))
                      (:choice attribute_declaration :blank)
                      ";")
  global_module_fragment_declaration (:seq "module" ";")
  private_module_fragment_declaration (:seq "module" ":" "private" ";")
  template_declaration (:seq
                        "template"
                        (:field :parameters template_parameter_list)
                        (:choice requires_clause :blank)
                        (:choice
                         _empty_declaration
                         alias_declaration
                         declaration
                         template_declaration
                         function_definition
                         concept_definition
                         friend_declaration
                         (:alias constructor_or_destructor_declaration declaration)
                         (:alias constructor_or_destructor_definition function_definition)
                         (:alias operator_cast_declaration declaration)
                         (:alias operator_cast_definition function_definition)))
  template_instantiation (:seq
                          "template"
                          (:choice _declaration_specifiers :blank)
                          (:field :declarator _declarator)
                          ";")
  template_parameter_list (:seq
                           "<"
                           (:choice
                            (:seq
                             (:choice
                              parameter_declaration
                              optional_parameter_declaration
                              type_parameter_declaration
                              variadic_parameter_declaration
                              variadic_type_parameter_declaration
                              optional_type_parameter_declaration
                              template_template_parameter_declaration)
                             (:repeat
                              (:seq
                               ","
                               (:choice
                                parameter_declaration
                                optional_parameter_declaration
                                type_parameter_declaration
                                variadic_parameter_declaration
                                variadic_type_parameter_declaration
                                optional_type_parameter_declaration
                                template_template_parameter_declaration))))
                            :blank)
                           (:alias (:token (:prec 1 ">")) ">"))
  type_parameter_declaration (:prec 1
                              (:seq (:choice "typename" "class") (:choice _type_identifier :blank)))
  variadic_type_parameter_declaration (:prec 1
                                       (:seq
                                        (:choice "typename" "class")
                                        "..."
                                        (:choice _type_identifier :blank)))
  optional_type_parameter_declaration (:seq
                                       (:choice "typename" "class")
                                       (:choice (:field :name _type_identifier) :blank)
                                       "="
                                       (:field :default_type type_specifier))
  template_template_parameter_declaration (:seq
                                           "template"
                                           (:field :parameters template_parameter_list)
                                           (:choice
                                            type_parameter_declaration
                                            variadic_type_parameter_declaration
                                            optional_type_parameter_declaration))
  optional_parameter_declaration (:seq
                                  _declaration_specifiers
                                  (:field :declarator
                                   (:choice
                                    (:choice _declarator abstract_reference_declarator)
                                    :blank))
                                  "="
                                  (:field :default_value expression))
  variadic_parameter_declaration (:seq
                                  _declaration_specifiers
                                  (:field :declarator
                                   (:choice
                                    variadic_declarator
                                    (:alias variadic_reference_declarator reference_declarator))))
  variadic_declarator (:seq "..." (:choice identifier :blank))
  variadic_reference_declarator (:seq (:choice "&&" "&") variadic_declarator)
  operator_cast (:prec-right 1
                 (:seq "operator" _declaration_specifiers (:field :declarator _abstract_declarator)))
  field_initializer_list (:seq ":" (:seq field_initializer (:repeat (:seq "," field_initializer))))
  field_initializer (:prec 1
                     (:seq
                      (:choice
                       _field_identifier
                       template_method
                       (:alias qualified_field_identifier qualified_identifier))
                      (:choice initializer_list argument_list)
                      (:choice "..." :blank)))
  inline_method_definition (:seq
                            _declaration_specifiers
                            (:field :declarator _field_declarator)
                            (:choice
                             (:field :body (:choice compound_statement try_statement))
                             default_method_clause
                             delete_method_clause
                             pure_virtual_clause))
  _constructor_specifiers (:choice _declaration_modifiers explicit_function_specifier)
  operator_cast_definition (:seq
                            (:repeat _constructor_specifiers)
                            (:field :declarator
                             (:choice
                              operator_cast
                              (:alias qualified_operator_cast_identifier qualified_identifier)))
                            (:field :body (:choice compound_statement try_statement)))
  operator_cast_declaration (:prec 1
                             (:seq
                              (:repeat _constructor_specifiers)
                              (:field :declarator
                               (:choice
                                operator_cast
                                (:alias qualified_operator_cast_identifier qualified_identifier)))
                              (:choice (:seq "=" (:field :default_value expression)) :blank)
                              ";"))
  constructor_try_statement (:seq
                             "try"
                             (:choice field_initializer_list :blank)
                             (:field :body compound_statement)
                             (:repeat1 catch_clause))
  constructor_or_destructor_definition (:seq
                                        (:repeat _constructor_specifiers)
                                        (:field :declarator function_declarator)
                                        (:choice
                                         (:seq
                                          (:choice field_initializer_list :blank)
                                          (:field :body compound_statement))
                                         (:alias constructor_try_statement try_statement)
                                         default_method_clause
                                         delete_method_clause
                                         pure_virtual_clause))
  constructor_or_destructor_declaration (:seq
                                         (:repeat _constructor_specifiers)
                                         (:field :declarator function_declarator)
                                         ";")
  default_method_clause (:seq "=" "default" ";")
  delete_method_clause (:seq "=" "delete" ";")
  pure_virtual_clause (:seq "=" (:pattern "0") ";")
  friend_declaration (:seq
                      (:choice "constexpr" :blank)
                      "friend"
                      (:choice
                       declaration
                       function_definition
                       (:seq (:choice (:choice "class" "struct" "union") :blank) _class_name ";")))
  access_specifier (:choice "public" "private" "protected")
  reference_declarator (:prec-dynamic 1 (:prec-right 0 (:seq (:choice "&" "&&") _declarator)))
  reference_field_declarator (:prec-dynamic 1
                              (:prec-right 0 (:seq (:choice "&" "&&") _field_declarator)))
  reference_type_declarator (:prec-dynamic 1
                             (:prec-right 0 (:seq (:choice "&" "&&") _type_declarator)))
  abstract_reference_declarator (:prec-right 0
                                 (:seq (:choice "&" "&&") (:choice _abstract_declarator :blank)))
  structured_binding_declarator (:prec-dynamic -1
                                 (:seq "[" (:seq identifier (:repeat (:seq "," identifier))) "]"))
  ref_qualifier (:choice "&" "&&")
  _function_declarator_seq (:seq
                            (:field :parameters parameter_list)
                            (:choice _function_attributes_start :blank)
                            (:choice ref_qualifier :blank)
                            (:choice _function_exception_specification :blank)
                            (:choice _function_attributes_end :blank)
                            (:choice trailing_return_type :blank)
                            (:choice _function_postfix :blank))
  _function_attributes_start (:prec 1
                              (:choice
                               (:seq (:repeat1 attribute_specifier) (:repeat type_qualifier))
                               (:seq (:repeat attribute_specifier) (:repeat1 type_qualifier))))
  _function_exception_specification (:choice noexcept throw_specifier)
  _function_attributes_end (:prec-right 0
                            (:seq
                             (:choice gnu_asm_expression :blank)
                             (:choice
                              (:seq (:repeat1 attribute_specifier) (:repeat attribute_declaration))
                              (:seq (:repeat attribute_specifier) (:repeat1 attribute_declaration)))))
  _function_postfix (:prec-right 0 (:choice (:repeat1 virtual_specifier) requires_clause))
  trailing_return_type (:seq "->" type_descriptor)
  noexcept (:prec-right 0
            (:seq "noexcept" (:choice (:seq "(" (:choice expression :blank) ")") :blank)))
  throw_specifier (:seq
                   "throw"
                   (:seq
                    "("
                    (:choice (:seq type_descriptor (:repeat (:seq "," type_descriptor))) :blank)
                    ")"))
  template_type (:seq (:field :name _type_identifier) (:field :arguments template_argument_list))
  template_method (:seq
                   (:field :name (:choice _field_identifier operator_name))
                   (:field :arguments template_argument_list))
  template_function (:seq (:field :name identifier) (:field :arguments template_argument_list))
  template_argument_list (:seq
                          "<"
                          (:choice
                           (:seq
                            (:choice
                             (:prec-dynamic 3 type_descriptor)
                             (:prec-dynamic 2
                              (:alias type_parameter_pack_expansion parameter_pack_expansion))
                             (:prec-dynamic 1 expression))
                            (:repeat
                             (:seq
                              ","
                              (:choice
                               (:prec-dynamic 3 type_descriptor)
                               (:prec-dynamic 2
                                (:alias type_parameter_pack_expansion parameter_pack_expansion))
                               (:prec-dynamic 1 expression)))))
                           :blank)
                          (:alias (:token (:prec 1 ">")) ">"))
  namespace_definition (:seq
                        (:choice "inline" :blank)
                        "namespace"
                        (:choice attribute_declaration :blank)
                        (:field :name
                         (:choice (:choice _namespace_identifier nested_namespace_specifier) :blank))
                        (:field :body declaration_list))
  namespace_alias_definition (:seq
                              "namespace"
                              (:field :name _namespace_identifier)
                              "="
                              (:choice _namespace_identifier nested_namespace_specifier)
                              ";")
  _namespace_specifier (:seq (:choice "inline" :blank) _namespace_identifier)
  nested_namespace_specifier (:prec 1
                              (:seq
                               (:choice _namespace_specifier :blank)
                               "::"
                               (:choice nested_namespace_specifier _namespace_specifier)))
  using_declaration (:seq
                     (:repeat attribute_declaration)
                     "using"
                     (:choice (:choice "namespace" "enum") :blank)
                     (:choice identifier qualified_identifier)
                     ";")
  alias_declaration (:seq
                     "using"
                     (:field :name _type_identifier)
                     (:repeat attribute_declaration)
                     "="
                     (:field :type type_descriptor)
                     ";")
  static_assert_declaration (:seq
                             "static_assert"
                             "("
                             (:field :condition expression)
                             (:choice (:seq "," (:field :message _string)) :blank)
                             ")"
                             ";")
  concept_definition (:seq "concept" (:field :name identifier) "=" expression ";")
  for_range_loop (:seq "for" "(" _for_range_loop_body ")" (:field :body statement))
  _for_range_loop_body (:seq
                        (:field :initializer (:choice init_statement :blank))
                        _declaration_specifiers
                        (:field :declarator _declarator)
                        ":"
                        (:field :right (:choice expression initializer_list)))
  init_statement (:choice alias_declaration type_definition declaration expression_statement)
  condition_clause (:seq
                    "("
                    (:field :initializer (:choice init_statement :blank))
                    (:field :value
                     (:choice
                      expression
                      comma_expression
                      (:alias condition_declaration declaration)))
                    ")")
  condition_declaration (:seq
                         _declaration_specifiers
                         (:field :declarator _declarator)
                         (:choice
                          (:seq "=" (:field :value expression))
                          (:field :value initializer_list)))
  co_return_statement (:seq "co_return" (:choice expression :blank) ";")
  co_yield_statement (:seq "co_yield" expression ";")
  throw_statement (:seq "throw" (:choice expression :blank) ";")
  try_statement (:seq "try" (:field :body compound_statement) (:repeat1 catch_clause))
  catch_clause (:seq "catch" (:field :parameters parameter_list) (:field :body compound_statement))
  raw_string_literal (:seq
                      (:choice "R\"" "LR\"" "uR\"" "UR\"" "u8R\"")
                      (:choice
                       (:seq
                        (:field :delimiter raw_string_delimiter)
                        "("
                        raw_string_content
                        ")"
                        raw_string_delimiter)
                       (:seq "(" raw_string_content ")"))
                      "\"")
  subscript_argument_list (:seq
                           "["
                           (:choice
                            (:seq
                             (:choice expression initializer_list)
                             (:repeat (:seq "," (:choice expression initializer_list))))
                            :blank)
                           "]")
  co_await_expression (:prec-left 14
                       (:seq (:field :operator "co_await") (:field :argument expression)))
  new_expression (:prec-right 16
                  (:seq
                   (:choice "::" :blank)
                   "new"
                   (:field :placement (:choice argument_list :blank))
                   (:field :type type_specifier)
                   (:field :declarator (:choice new_declarator :blank))
                   (:field :arguments (:choice (:choice argument_list initializer_list) :blank))))
  new_declarator (:prec-right 0
                  (:seq "[" (:field :length expression) "]" (:choice new_declarator :blank)))
  delete_expression (:seq (:choice "::" :blank) "delete" (:choice (:seq "[" "]") :blank) expression)
  type_requirement (:seq "typename" _class_name)
  compound_requirement (:seq
                        "{"
                        expression
                        "}"
                        (:choice "noexcept" :blank)
                        (:choice trailing_return_type :blank)
                        ";")
  _requirement (:choice
                (:alias expression_statement simple_requirement)
                type_requirement
                compound_requirement)
  requirement_seq (:seq "{" (:repeat _requirement) "}")
  constraint_conjunction (:prec-left 2
                          (:seq
                           (:field :left _requirement_clause_constraint)
                           (:field :operator (:choice "&&" "and"))
                           (:field :right _requirement_clause_constraint)))
  constraint_disjunction (:prec-left 1
                          (:seq
                           (:field :left _requirement_clause_constraint)
                           (:field :operator (:choice "||" "or"))
                           (:field :right _requirement_clause_constraint)))
  _requirement_clause_constraint (:choice
                                  (:ref "true")
                                  (:ref "false")
                                  _class_name
                                  fold_expression
                                  lambda_expression
                                  requires_expression
                                  (:seq "(" expression ")")
                                  constraint_conjunction
                                  constraint_disjunction)
  requires_clause (:seq "requires" (:field :constraint _requirement_clause_constraint))
  requires_parameter_list (:seq
                           "("
                           (:choice
                            (:seq
                             (:choice
                              parameter_declaration
                              optional_parameter_declaration
                              variadic_parameter_declaration)
                             (:repeat
                              (:seq
                               ","
                               (:choice
                                parameter_declaration
                                optional_parameter_declaration
                                variadic_parameter_declaration))))
                            :blank)
                           ")")
  requires_expression (:seq
                       "requires"
                       (:field :parameters
                        (:choice (:alias requires_parameter_list parameter_list) :blank))
                       (:field :requirements requirement_seq))
  lambda_declarator (:choice
                     (:seq
                      (:repeat attribute_declaration)
                      (:field :parameters parameter_list)
                      (:choice type_qualifier :blank)
                      (:choice _function_exception_specification :blank)
                      (:repeat attribute_declaration)
                      (:choice trailing_return_type :blank)
                      (:choice requires_clause :blank))
                     (:repeat1 attribute_declaration)
                     (:seq (:repeat attribute_declaration) trailing_return_type)
                     (:seq
                      (:repeat attribute_declaration)
                      _function_exception_specification
                      (:repeat attribute_declaration)
                      (:choice trailing_return_type :blank))
                     (:seq
                      (:repeat attribute_declaration)
                      type_qualifier
                      (:choice _function_exception_specification :blank)
                      (:repeat attribute_declaration)
                      (:choice trailing_return_type :blank)))
  lambda_expression (:seq
                     (:field :captures lambda_capture_specifier)
                     (:choice
                      (:seq
                       (:field :template_parameters template_parameter_list)
                       (:choice (:field :constraint requires_clause) :blank))
                      :blank)
                     (:choice (:field :declarator lambda_declarator) :blank)
                     (:field :body compound_statement))
  lambda_capture_specifier (:prec 18
                            (:seq
                             "["
                             (:choice
                              lambda_default_capture
                              (:choice
                               (:seq _lambda_capture (:repeat (:seq "," _lambda_capture)))
                               :blank)
                              (:seq
                               lambda_default_capture
                               ","
                               (:seq _lambda_capture (:repeat (:seq "," _lambda_capture)))))
                             "]"))
  lambda_default_capture (:choice "=" "&")
  _lambda_capture_identifier (:seq
                              (:choice "&" :blank)
                              (:choice
                               identifier
                               qualified_identifier
                               (:alias identifier_parameter_pack_expansion parameter_pack_expansion)))
  lambda_capture_initializer (:seq
                              (:choice "&" :blank)
                              (:choice "..." :blank)
                              (:field :left identifier)
                              "="
                              (:field :right expression))
  _lambda_capture (:choice
                   (:seq (:choice "*" :blank) this)
                   _lambda_capture_identifier
                   lambda_capture_initializer)
  _fold_operator (:choice
                  "+"
                  "-"
                  "*"
                  "/"
                  "%"
                  "^"
                  "&"
                  "|"
                  "="
                  "<"
                  ">"
                  "<<"
                  ">>"
                  "+="
                  "-="
                  "*="
                  "/="
                  "%="
                  "^="
                  "&="
                  "|="
                  ">>="
                  "<<="
                  "=="
                  "!="
                  "<="
                  ">="
                  "&&"
                  "||"
                  ","
                  ".*"
                  "->*"
                  "or"
                  "and"
                  "bitor"
                  "xor"
                  "bitand"
                  "not_eq")
  _binary_fold_operator (:choice
                         (:seq (:field :operator "+") "..." "+")
                         (:seq (:field :operator "-") "..." "-")
                         (:seq (:field :operator "*") "..." "*")
                         (:seq (:field :operator "/") "..." "/")
                         (:seq (:field :operator "%") "..." "%")
                         (:seq (:field :operator "^") "..." "^")
                         (:seq (:field :operator "&") "..." "&")
                         (:seq (:field :operator "|") "..." "|")
                         (:seq (:field :operator "=") "..." "=")
                         (:seq (:field :operator "<") "..." "<")
                         (:seq (:field :operator ">") "..." ">")
                         (:seq (:field :operator "<<") "..." "<<")
                         (:seq (:field :operator ">>") "..." ">>")
                         (:seq (:field :operator "+=") "..." "+=")
                         (:seq (:field :operator "-=") "..." "-=")
                         (:seq (:field :operator "*=") "..." "*=")
                         (:seq (:field :operator "/=") "..." "/=")
                         (:seq (:field :operator "%=") "..." "%=")
                         (:seq (:field :operator "^=") "..." "^=")
                         (:seq (:field :operator "&=") "..." "&=")
                         (:seq (:field :operator "|=") "..." "|=")
                         (:seq (:field :operator ">>=") "..." ">>=")
                         (:seq (:field :operator "<<=") "..." "<<=")
                         (:seq (:field :operator "==") "..." "==")
                         (:seq (:field :operator "!=") "..." "!=")
                         (:seq (:field :operator "<=") "..." "<=")
                         (:seq (:field :operator ">=") "..." ">=")
                         (:seq (:field :operator "&&") "..." "&&")
                         (:seq (:field :operator "||") "..." "||")
                         (:seq (:field :operator ",") "..." ",")
                         (:seq (:field :operator ".*") "..." ".*")
                         (:seq (:field :operator "->*") "..." "->*")
                         (:seq (:field :operator "or") "..." "or")
                         (:seq (:field :operator "and") "..." "and")
                         (:seq (:field :operator "bitor") "..." "bitor")
                         (:seq (:field :operator "xor") "..." "xor")
                         (:seq (:field :operator "bitand") "..." "bitand")
                         (:seq (:field :operator "not_eq") "..." "not_eq"))
  _unary_left_fold (:seq
                    (:field :left "...")
                    (:field :operator _fold_operator)
                    (:field :right expression))
  _unary_right_fold (:seq
                     (:field :left expression)
                     (:field :operator _fold_operator)
                     (:field :right "..."))
  _binary_fold (:seq (:field :left expression) _binary_fold_operator (:field :right expression))
  fold_expression (:seq "(" (:choice _unary_right_fold _unary_left_fold _binary_fold) ")")
  parameter_pack_expansion (:prec -1 (:seq (:field :pattern expression) "..."))
  type_parameter_pack_expansion (:seq (:field :pattern type_descriptor) "...")
  identifier_parameter_pack_expansion (:seq (:field :pattern identifier) "...")
  destructor_name (:prec 1 (:seq "~" identifier))
  dependent_identifier (:seq "template" template_function)
  dependent_field_identifier (:seq "template" template_method)
  dependent_type_identifier (:seq "template" template_type)
  _scope_resolution (:prec 1
                     (:seq
                      (:field :scope
                       (:choice
                        (:choice
                         _namespace_identifier
                         template_type
                         decltype
                         (:alias dependent_type_identifier dependent_name))
                        :blank))
                      "::"))
  qualified_field_identifier (:seq
                              _scope_resolution
                              (:field :name
                               (:choice
                                (:alias dependent_field_identifier dependent_name)
                                (:alias qualified_field_identifier qualified_identifier)
                                template_method
                                (:prec-dynamic 1 _field_identifier))))
  qualified_identifier (:seq
                        _scope_resolution
                        (:field :name
                         (:choice
                          (:alias dependent_identifier dependent_name)
                          qualified_identifier
                          template_function
                          (:seq (:choice "template" :blank) identifier)
                          operator_name
                          destructor_name
                          pointer_type_declarator)))
  qualified_type_identifier (:seq
                             _scope_resolution
                             (:field :name
                              (:choice
                               (:alias dependent_type_identifier dependent_name)
                               (:alias qualified_type_identifier qualified_identifier)
                               template_type
                               _type_identifier)))
  qualified_operator_cast_identifier (:seq
                                      _scope_resolution
                                      (:field :name
                                       (:choice
                                        (:alias
                                         qualified_operator_cast_identifier
                                         qualified_identifier)
                                        operator_cast)))
  _assignment_expression_lhs (:seq
                              (:field :left expression)
                              (:field :operator
                               (:choice
                                "="
                                "*="
                                "/="
                                "%="
                                "+="
                                "-="
                                "<<="
                                ">>="
                                "&="
                                "^="
                                "|="
                                "and_eq"
                                "or_eq"
                                "xor_eq"))
                              (:field :right (:choice expression initializer_list)))
  operator_name (:prec 1
                 (:seq
                  "operator"
                  (:choice
                   "co_await"
                   "+"
                   "-"
                   "*"
                   "/"
                   "%"
                   "^"
                   "&"
                   "|"
                   "~"
                   "!"
                   "="
                   "<"
                   ">"
                   "+="
                   "-="
                   "*="
                   "/="
                   "%="
                   "^="
                   "&="
                   "|="
                   "<<"
                   ">>"
                   ">>="
                   "<<="
                   "=="
                   "!="
                   "<="
                   ">="
                   "<=>"
                   "&&"
                   "||"
                   "++"
                   "--"
                   ","
                   "->*"
                   "->"
                   "()"
                   "[]"
                   "xor"
                   "bitand"
                   "bitor"
                   "compl"
                   "not"
                   "xor_eq"
                   "and_eq"
                   "or_eq"
                   "not_eq"
                   "and"
                   "or"
                   (:seq (:choice "new" "delete") (:choice "[]" :blank))
                   (:seq "\"\"" identifier))))
  this "this"
  literal_suffix (:token-immediate (:pattern "[a-zA-Z_]\\w*"))
  user_defined_literal (:seq (:choice number_literal char_literal _string) literal_suffix)
  _namespace_identifier (:alias identifier namespace_identifier)
  semantics (:seq ":" identifier)
  discard_statement (:seq "discard" ";")
  qualifiers (:choice
              "precise"
              "shared"
              "groupshared"
              "uniform"
              "row_major"
              "column_major"
              "globallycoherent"
              "centroid"
              "noperspective"
              "nointerpolation"
              "sample"
              "linear"
              "snorm"
              "unorm"
              "point"
              "line"
              "triangleadj"
              "lineadj"
              "triangle")
  cbuffer_specifier (:prec-right 0
                     (:seq
                      "cbuffer"
                      (:choice attribute_declaration :blank)
                      (:choice
                       (:field :name _class_name)
                       (:seq
                        (:choice (:field :name _class_name) :blank)
                        (:choice virtual_specifier :blank)
                        (:choice base_class_clause :blank)
                        (:field :body field_declaration_list)))))
  hlsl_attribute (:seq "[" expression "]")}}
