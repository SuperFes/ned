# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "fortran"
 :extras [(:pattern "\\s|\\\\\\r?\\n") comment custom_directive multiline_preproc_comment "&"]
 :conflicts [[_expression complex_literal]
             [_argument_list parenthesized_expression]
             [case_statement]
             [data_set _expression]
             [data_statement identifier]
             [data_value _expression]
             [else_clause]
             [elseif_clause identifier]
             [elseif_clause]
             [elsewhere_clause]
             [intrinsic_type]
             [_intrinsic_type identifier]
             [module_statement procedure_qualifier]
             [procedure_declaration]
             [rank_statement]
             [stop_statement identifier]
             [type_statement]
             [preproc_ifdef_in_specification_part program]
             [preproc_else_in_specification_part program]
             [coarray_critical_statement identifier]
             [format_statement identifier]
             [_inline_if_statement arithmetic_if_statement _block_if_statement identifier]
             [cray_pointer_declaration identifier]
             [unit_identifier identifier]
             [format_identifier identifier]]
 :precedences []
 :externals ["&"
             _integer_literal
             _float_literal
             _boz_literal
             _string_literal
             _string_literal_kind
             _external_end_of_statement
             _preproc_unary_operator
             hollerith_constant
             _do_label
             do_label_virtual
             _do_label_continue]
 :inline [_top_level_item _statement]
 :supertypes [_specification_parts _expression _statements _argument_item _procedure_binding]
 :rules
 {translation_unit (:seq (:repeat _top_level_item) (:choice program :blank))
  _top_level_item (:prec 2
                   (:choice
                    (:seq include_statement _end_of_statement)
                    program
                    module
                    submodule
                    interface
                    subroutine
                    function
                    block_data
                    preproc_if
                    preproc_ifdef
                    preproc_include
                    preproc_def
                    preproc_function_def
                    preproc_call))
  preproc_include (:seq
                   (:alias (:pattern "#[ \t]*include") "#include")
                   (:field :path
                    (:choice
                     string_literal
                     identifier
                     system_lib_string
                     (:alias preproc_call_expression call_expression)))
                   (:pattern "\\r?\\n"))
  preproc_def (:seq
               (:alias (:pattern "#[ \t]*define") "#define")
               (:field :name identifier)
               (:field :value (:choice preproc_arg :blank))
               (:token (:prec 1 (:pattern "\\r?\\n"))))
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
  preproc_if (:prec 4
              (:seq
               (:alias (:pattern "#[ \t]*if") "#if")
               (:field :condition _preproc_expression)
               (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)
               "\n"
               (:repeat _top_level_item)
               (:field :alternative
                (:choice (:choice preproc_else preproc_elif preproc_elifdef) :blank))
               (:alias (:pattern "#[ \t]*endif") "#endif")
               (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)))
  preproc_ifdef (:prec 4
                 (:seq
                  (:choice
                   (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                   (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                  (:field :name identifier)
                  (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)
                  (:repeat _top_level_item)
                  (:field :alternative
                   (:choice (:choice preproc_else preproc_elif preproc_elifdef) :blank))
                  (:alias (:pattern "#[ \t]*endif") "#endif")
                  (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)))
  preproc_else (:prec 4
                (:seq
                 (:alias (:pattern "#[ \t]*else") "#else")
                 (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)
                 (:repeat _top_level_item)))
  preproc_elif (:prec 4
                (:seq
                 (:alias (:pattern "#[ \t]*elif") "#elif")
                 (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)
                 (:field :condition _preproc_expression)
                 "\n"
                 (:repeat _top_level_item)
                 (:field :alternative
                  (:choice (:choice preproc_else preproc_elif preproc_elifdef) :blank))))
  preproc_elifdef (:prec 4
                   (:seq
                    (:choice
                     (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                     (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                    (:field :name identifier)
                    (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)
                    (:repeat _top_level_item)
                    (:field :alternative
                     (:choice (:choice preproc_else preproc_elif preproc_elifdef) :blank))))
  preproc_if_in_module (:prec 0
                        (:seq
                         (:alias (:pattern "#[ \t]*if") "#if")
                         (:field :condition _preproc_expression)
                         (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)
                         "\n"
                         (:seq (:repeat _specification_part) (:choice internal_procedures :blank))
                         (:field :alternative
                          (:choice
                           (:choice
                            (:alias preproc_else_in_module preproc_else)
                            (:alias preproc_elif_in_module preproc_elif)
                            (:alias preproc_elifdef_in_module preproc_elifdef))
                           :blank))
                         (:alias (:pattern "#[ \t]*endif") "#endif")
                         (:choice (:choice inline_preproc_comment multiline_preproc_comment) :blank)))
  preproc_ifdef_in_module (:prec 0
                           (:seq
                            (:choice
                             (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                             (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                            (:field :name identifier)
                            (:choice
                             (:choice inline_preproc_comment multiline_preproc_comment)
                             :blank)
                            (:seq
                             (:repeat _specification_part)
                             (:choice internal_procedures :blank))
                            (:field :alternative
                             (:choice
                              (:choice
                               (:alias preproc_else_in_module preproc_else)
                               (:alias preproc_elif_in_module preproc_elif)
                               (:alias preproc_elifdef_in_module preproc_elifdef))
                              :blank))
                            (:alias (:pattern "#[ \t]*endif") "#endif")
                            (:choice
                             (:choice inline_preproc_comment multiline_preproc_comment)
                             :blank)))
  preproc_else_in_module (:prec 0
                          (:seq
                           (:alias (:pattern "#[ \t]*else") "#else")
                           (:choice
                            (:choice inline_preproc_comment multiline_preproc_comment)
                            :blank)
                           (:seq (:repeat _specification_part) (:choice internal_procedures :blank))))
  preproc_elif_in_module (:prec 0
                          (:seq
                           (:alias (:pattern "#[ \t]*elif") "#elif")
                           (:choice
                            (:choice inline_preproc_comment multiline_preproc_comment)
                            :blank)
                           (:field :condition _preproc_expression)
                           "\n"
                           (:seq (:repeat _specification_part) (:choice internal_procedures :blank))
                           (:field :alternative
                            (:choice
                             (:choice
                              (:alias preproc_else_in_module preproc_else)
                              (:alias preproc_elif_in_module preproc_elif)
                              (:alias preproc_elifdef_in_module preproc_elifdef))
                             :blank))))
  preproc_elifdef_in_module (:prec 0
                             (:seq
                              (:choice
                               (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                               (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                              (:field :name identifier)
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)
                              (:seq
                               (:repeat _specification_part)
                               (:choice internal_procedures :blank))
                              (:field :alternative
                               (:choice
                                (:choice
                                 (:alias preproc_else_in_module preproc_else)
                                 (:alias preproc_elif_in_module preproc_elif)
                                 (:alias preproc_elifdef_in_module preproc_elifdef))
                                :blank))))
  preproc_if_in_specification_part (:prec 3
                                    (:seq
                                     (:alias (:pattern "#[ \t]*if") "#if")
                                     (:field :condition _preproc_expression)
                                     (:choice
                                      (:choice inline_preproc_comment multiline_preproc_comment)
                                      :blank)
                                     "\n"
                                     (:seq
                                      (:repeat _specification_part)
                                      (:repeat _statement)
                                      (:choice internal_procedures :blank))
                                     (:field :alternative
                                      (:choice
                                       (:choice
                                        (:alias preproc_else_in_specification_part preproc_else)
                                        (:alias preproc_elif_in_specification_part preproc_elif)
                                        (:alias
                                         preproc_elifdef_in_specification_part
                                         preproc_elifdef))
                                       :blank))
                                     (:alias (:pattern "#[ \t]*endif") "#endif")
                                     (:choice
                                      (:choice inline_preproc_comment multiline_preproc_comment)
                                      :blank)))
  preproc_ifdef_in_specification_part (:prec 3
                                       (:seq
                                        (:choice
                                         (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                         (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                        (:field :name identifier)
                                        (:choice
                                         (:choice inline_preproc_comment multiline_preproc_comment)
                                         :blank)
                                        (:seq
                                         (:repeat _specification_part)
                                         (:repeat _statement)
                                         (:choice internal_procedures :blank))
                                        (:field :alternative
                                         (:choice
                                          (:choice
                                           (:alias preproc_else_in_specification_part preproc_else)
                                           (:alias preproc_elif_in_specification_part preproc_elif)
                                           (:alias
                                            preproc_elifdef_in_specification_part
                                            preproc_elifdef))
                                          :blank))
                                        (:alias (:pattern "#[ \t]*endif") "#endif")
                                        (:choice
                                         (:choice inline_preproc_comment multiline_preproc_comment)
                                         :blank)))
  preproc_else_in_specification_part (:prec 3
                                      (:seq
                                       (:alias (:pattern "#[ \t]*else") "#else")
                                       (:choice
                                        (:choice inline_preproc_comment multiline_preproc_comment)
                                        :blank)
                                       (:seq
                                        (:repeat _specification_part)
                                        (:repeat _statement)
                                        (:choice internal_procedures :blank))))
  preproc_elif_in_specification_part (:prec 3
                                      (:seq
                                       (:alias (:pattern "#[ \t]*elif") "#elif")
                                       (:choice
                                        (:choice inline_preproc_comment multiline_preproc_comment)
                                        :blank)
                                       (:field :condition _preproc_expression)
                                       "\n"
                                       (:seq
                                        (:repeat _specification_part)
                                        (:repeat _statement)
                                        (:choice internal_procedures :blank))
                                       (:field :alternative
                                        (:choice
                                         (:choice
                                          (:alias preproc_else_in_specification_part preproc_else)
                                          (:alias preproc_elif_in_specification_part preproc_elif)
                                          (:alias
                                           preproc_elifdef_in_specification_part
                                           preproc_elifdef))
                                         :blank))))
  preproc_elifdef_in_specification_part (:prec 3
                                         (:seq
                                          (:choice
                                           (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                           (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                          (:field :name identifier)
                                          (:choice
                                           (:choice
                                            inline_preproc_comment
                                            multiline_preproc_comment)
                                           :blank)
                                          (:seq
                                           (:repeat _specification_part)
                                           (:repeat _statement)
                                           (:choice internal_procedures :blank))
                                          (:field :alternative
                                           (:choice
                                            (:choice
                                             (:alias
                                              preproc_else_in_specification_part
                                              preproc_else)
                                             (:alias
                                              preproc_elif_in_specification_part
                                              preproc_elif)
                                             (:alias
                                              preproc_elifdef_in_specification_part
                                              preproc_elifdef))
                                            :blank))))
  preproc_if_in_statements (:prec 1
                            (:seq
                             (:alias (:pattern "#[ \t]*if") "#if")
                             (:field :condition _preproc_expression)
                             (:choice
                              (:choice inline_preproc_comment multiline_preproc_comment)
                              :blank)
                             "\n"
                             (:seq (:repeat _statement) (:choice internal_procedures :blank))
                             (:field :alternative
                              (:choice
                               (:choice
                                (:alias preproc_else_in_statements preproc_else)
                                (:alias preproc_elif_in_statements preproc_elif)
                                (:alias preproc_elifdef_in_statements preproc_elifdef))
                               :blank))
                             (:alias (:pattern "#[ \t]*endif") "#endif")
                             (:choice
                              (:choice inline_preproc_comment multiline_preproc_comment)
                              :blank)))
  preproc_ifdef_in_statements (:prec 1
                               (:seq
                                (:choice
                                 (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                 (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                (:field :name identifier)
                                (:choice
                                 (:choice inline_preproc_comment multiline_preproc_comment)
                                 :blank)
                                (:seq (:repeat _statement) (:choice internal_procedures :blank))
                                (:field :alternative
                                 (:choice
                                  (:choice
                                   (:alias preproc_else_in_statements preproc_else)
                                   (:alias preproc_elif_in_statements preproc_elif)
                                   (:alias preproc_elifdef_in_statements preproc_elifdef))
                                  :blank))
                                (:alias (:pattern "#[ \t]*endif") "#endif")
                                (:choice
                                 (:choice inline_preproc_comment multiline_preproc_comment)
                                 :blank)))
  preproc_else_in_statements (:prec 1
                              (:seq
                               (:alias (:pattern "#[ \t]*else") "#else")
                               (:choice
                                (:choice inline_preproc_comment multiline_preproc_comment)
                                :blank)
                               (:seq (:repeat _statement) (:choice internal_procedures :blank))))
  preproc_elif_in_statements (:prec 1
                              (:seq
                               (:alias (:pattern "#[ \t]*elif") "#elif")
                               (:choice
                                (:choice inline_preproc_comment multiline_preproc_comment)
                                :blank)
                               (:field :condition _preproc_expression)
                               "\n"
                               (:seq (:repeat _statement) (:choice internal_procedures :blank))
                               (:field :alternative
                                (:choice
                                 (:choice
                                  (:alias preproc_else_in_statements preproc_else)
                                  (:alias preproc_elif_in_statements preproc_elif)
                                  (:alias preproc_elifdef_in_statements preproc_elifdef))
                                 :blank))))
  preproc_elifdef_in_statements (:prec 1
                                 (:seq
                                  (:choice
                                   (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                   (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                  (:field :name identifier)
                                  (:choice
                                   (:choice inline_preproc_comment multiline_preproc_comment)
                                   :blank)
                                  (:seq (:repeat _statement) (:choice internal_procedures :blank))
                                  (:field :alternative
                                   (:choice
                                    (:choice
                                     (:alias preproc_else_in_statements preproc_else)
                                     (:alias preproc_elif_in_statements preproc_elif)
                                     (:alias preproc_elifdef_in_statements preproc_elifdef))
                                    :blank))))
  preproc_if_in_procedure_statements (:prec 2
                                      (:seq
                                       (:alias (:pattern "#[ \t]*if") "#if")
                                       (:field :condition _preproc_expression)
                                       (:choice
                                        (:choice inline_preproc_comment multiline_preproc_comment)
                                        :blank)
                                       "\n"
                                       (:seq
                                        (:repeat _statement)
                                        (:choice internal_procedures :blank))
                                       (:field :alternative
                                        (:choice
                                         (:choice
                                          (:alias preproc_else_in_procedure_statements preproc_else)
                                          (:alias preproc_elif_in_procedure_statements preproc_elif)
                                          (:alias
                                           preproc_elifdef_in_procedure_statements
                                           preproc_elifdef))
                                         :blank))
                                       (:alias (:pattern "#[ \t]*endif") "#endif")
                                       (:choice
                                        (:choice inline_preproc_comment multiline_preproc_comment)
                                        :blank)))
  preproc_ifdef_in_procedure_statements (:prec 2
                                         (:seq
                                          (:choice
                                           (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                           (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                          (:field :name identifier)
                                          (:choice
                                           (:choice
                                            inline_preproc_comment
                                            multiline_preproc_comment)
                                           :blank)
                                          (:seq
                                           (:repeat _statement)
                                           (:choice internal_procedures :blank))
                                          (:field :alternative
                                           (:choice
                                            (:choice
                                             (:alias
                                              preproc_else_in_procedure_statements
                                              preproc_else)
                                             (:alias
                                              preproc_elif_in_procedure_statements
                                              preproc_elif)
                                             (:alias
                                              preproc_elifdef_in_procedure_statements
                                              preproc_elifdef))
                                            :blank))
                                          (:alias (:pattern "#[ \t]*endif") "#endif")
                                          (:choice
                                           (:choice
                                            inline_preproc_comment
                                            multiline_preproc_comment)
                                           :blank)))
  preproc_else_in_procedure_statements (:prec 2
                                        (:seq
                                         (:alias (:pattern "#[ \t]*else") "#else")
                                         (:choice
                                          (:choice inline_preproc_comment multiline_preproc_comment)
                                          :blank)
                                         (:seq
                                          (:repeat _statement)
                                          (:choice internal_procedures :blank))))
  preproc_elif_in_procedure_statements (:prec 2
                                        (:seq
                                         (:alias (:pattern "#[ \t]*elif") "#elif")
                                         (:choice
                                          (:choice inline_preproc_comment multiline_preproc_comment)
                                          :blank)
                                         (:field :condition _preproc_expression)
                                         "\n"
                                         (:seq
                                          (:repeat _statement)
                                          (:choice internal_procedures :blank))
                                         (:field :alternative
                                          (:choice
                                           (:choice
                                            (:alias
                                             preproc_else_in_procedure_statements
                                             preproc_else)
                                            (:alias
                                             preproc_elif_in_procedure_statements
                                             preproc_elif)
                                            (:alias
                                             preproc_elifdef_in_procedure_statements
                                             preproc_elifdef))
                                           :blank))))
  preproc_elifdef_in_procedure_statements (:prec 2
                                           (:seq
                                            (:choice
                                             (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                             (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                            (:field :name identifier)
                                            (:choice
                                             (:choice
                                              inline_preproc_comment
                                              multiline_preproc_comment)
                                             :blank)
                                            (:seq
                                             (:repeat _statement)
                                             (:choice internal_procedures :blank))
                                            (:field :alternative
                                             (:choice
                                              (:choice
                                               (:alias
                                                preproc_else_in_procedure_statements
                                                preproc_else)
                                               (:alias
                                                preproc_elif_in_procedure_statements
                                                preproc_elif)
                                               (:alias
                                                preproc_elifdef_in_procedure_statements
                                                preproc_elifdef))
                                              :blank))))
  preproc_if_in_internal_procedures (:prec 0
                                     (:seq
                                      (:alias (:pattern "#[ \t]*if") "#if")
                                      (:field :condition _preproc_expression)
                                      (:choice
                                       (:choice inline_preproc_comment multiline_preproc_comment)
                                       :blank)
                                      "\n"
                                      (:repeat _internal_procedures)
                                      (:field :alternative
                                       (:choice
                                        (:choice
                                         (:alias preproc_else_in_internal_procedures preproc_else)
                                         (:alias preproc_elif_in_internal_procedures preproc_elif)
                                         (:alias
                                          preproc_elifdef_in_internal_procedures
                                          preproc_elifdef))
                                        :blank))
                                      (:alias (:pattern "#[ \t]*endif") "#endif")
                                      (:choice
                                       (:choice inline_preproc_comment multiline_preproc_comment)
                                       :blank)))
  preproc_ifdef_in_internal_procedures (:prec 0
                                        (:seq
                                         (:choice
                                          (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                          (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                         (:field :name identifier)
                                         (:choice
                                          (:choice inline_preproc_comment multiline_preproc_comment)
                                          :blank)
                                         (:repeat _internal_procedures)
                                         (:field :alternative
                                          (:choice
                                           (:choice
                                            (:alias
                                             preproc_else_in_internal_procedures
                                             preproc_else)
                                            (:alias
                                             preproc_elif_in_internal_procedures
                                             preproc_elif)
                                            (:alias
                                             preproc_elifdef_in_internal_procedures
                                             preproc_elifdef))
                                           :blank))
                                         (:alias (:pattern "#[ \t]*endif") "#endif")
                                         (:choice
                                          (:choice inline_preproc_comment multiline_preproc_comment)
                                          :blank)))
  preproc_else_in_internal_procedures (:prec 0
                                       (:seq
                                        (:alias (:pattern "#[ \t]*else") "#else")
                                        (:choice
                                         (:choice inline_preproc_comment multiline_preproc_comment)
                                         :blank)
                                        (:repeat _internal_procedures)))
  preproc_elif_in_internal_procedures (:prec 0
                                       (:seq
                                        (:alias (:pattern "#[ \t]*elif") "#elif")
                                        (:choice
                                         (:choice inline_preproc_comment multiline_preproc_comment)
                                         :blank)
                                        (:field :condition _preproc_expression)
                                        "\n"
                                        (:repeat _internal_procedures)
                                        (:field :alternative
                                         (:choice
                                          (:choice
                                           (:alias preproc_else_in_internal_procedures preproc_else)
                                           (:alias preproc_elif_in_internal_procedures preproc_elif)
                                           (:alias
                                            preproc_elifdef_in_internal_procedures
                                            preproc_elifdef))
                                          :blank))))
  preproc_elifdef_in_internal_procedures (:prec 0
                                          (:seq
                                           (:choice
                                            (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                            (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                           (:field :name identifier)
                                           (:choice
                                            (:choice
                                             inline_preproc_comment
                                             multiline_preproc_comment)
                                            :blank)
                                           (:repeat _internal_procedures)
                                           (:field :alternative
                                            (:choice
                                             (:choice
                                              (:alias
                                               preproc_else_in_internal_procedures
                                               preproc_else)
                                              (:alias
                                               preproc_elif_in_internal_procedures
                                               preproc_elif)
                                              (:alias
                                               preproc_elifdef_in_internal_procedures
                                               preproc_elifdef))
                                             :blank))))
  preproc_if_in_interface (:prec 0
                           (:seq
                            (:alias (:pattern "#[ \t]*if") "#if")
                            (:field :condition _preproc_expression)
                            (:choice
                             (:choice inline_preproc_comment multiline_preproc_comment)
                             :blank)
                            "\n"
                            (:repeat _interface_items)
                            (:field :alternative
                             (:choice
                              (:choice
                               (:alias preproc_else_in_interface preproc_else)
                               (:alias preproc_elif_in_interface preproc_elif)
                               (:alias preproc_elifdef_in_interface preproc_elifdef))
                              :blank))
                            (:alias (:pattern "#[ \t]*endif") "#endif")
                            (:choice
                             (:choice inline_preproc_comment multiline_preproc_comment)
                             :blank)))
  preproc_ifdef_in_interface (:prec 0
                              (:seq
                               (:choice
                                (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                               (:field :name identifier)
                               (:choice
                                (:choice inline_preproc_comment multiline_preproc_comment)
                                :blank)
                               (:repeat _interface_items)
                               (:field :alternative
                                (:choice
                                 (:choice
                                  (:alias preproc_else_in_interface preproc_else)
                                  (:alias preproc_elif_in_interface preproc_elif)
                                  (:alias preproc_elifdef_in_interface preproc_elifdef))
                                 :blank))
                               (:alias (:pattern "#[ \t]*endif") "#endif")
                               (:choice
                                (:choice inline_preproc_comment multiline_preproc_comment)
                                :blank)))
  preproc_else_in_interface (:prec 0
                             (:seq
                              (:alias (:pattern "#[ \t]*else") "#else")
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)
                              (:repeat _interface_items)))
  preproc_elif_in_interface (:prec 0
                             (:seq
                              (:alias (:pattern "#[ \t]*elif") "#elif")
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)
                              (:field :condition _preproc_expression)
                              "\n"
                              (:repeat _interface_items)
                              (:field :alternative
                               (:choice
                                (:choice
                                 (:alias preproc_else_in_interface preproc_else)
                                 (:alias preproc_elif_in_interface preproc_elif)
                                 (:alias preproc_elifdef_in_interface preproc_elifdef))
                                :blank))))
  preproc_elifdef_in_interface (:prec 0
                                (:seq
                                 (:choice
                                  (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                  (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                 (:field :name identifier)
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)
                                 (:repeat _interface_items)
                                 (:field :alternative
                                  (:choice
                                   (:choice
                                    (:alias preproc_else_in_interface preproc_else)
                                    (:alias preproc_elif_in_interface preproc_elif)
                                    (:alias preproc_elifdef_in_interface preproc_elifdef))
                                   :blank))))
  preproc_if_in_derived_type (:prec 0
                              (:seq
                               (:alias (:pattern "#[ \t]*if") "#if")
                               (:field :condition _preproc_expression)
                               (:choice
                                (:choice inline_preproc_comment multiline_preproc_comment)
                                :blank)
                               "\n"
                               (:repeat variable_declaration)
                               (:field :alternative
                                (:choice
                                 (:choice
                                  (:alias preproc_else_in_derived_type preproc_else)
                                  (:alias preproc_elif_in_derived_type preproc_elif)
                                  (:alias preproc_elifdef_in_derived_type preproc_elifdef))
                                 :blank))
                               (:alias (:pattern "#[ \t]*endif") "#endif")
                               (:choice
                                (:choice inline_preproc_comment multiline_preproc_comment)
                                :blank)))
  preproc_ifdef_in_derived_type (:prec 0
                                 (:seq
                                  (:choice
                                   (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                   (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                  (:field :name identifier)
                                  (:choice
                                   (:choice inline_preproc_comment multiline_preproc_comment)
                                   :blank)
                                  (:repeat variable_declaration)
                                  (:field :alternative
                                   (:choice
                                    (:choice
                                     (:alias preproc_else_in_derived_type preproc_else)
                                     (:alias preproc_elif_in_derived_type preproc_elif)
                                     (:alias preproc_elifdef_in_derived_type preproc_elifdef))
                                    :blank))
                                  (:alias (:pattern "#[ \t]*endif") "#endif")
                                  (:choice
                                   (:choice inline_preproc_comment multiline_preproc_comment)
                                   :blank)))
  preproc_else_in_derived_type (:prec 0
                                (:seq
                                 (:alias (:pattern "#[ \t]*else") "#else")
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)
                                 (:repeat variable_declaration)))
  preproc_elif_in_derived_type (:prec 0
                                (:seq
                                 (:alias (:pattern "#[ \t]*elif") "#elif")
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)
                                 (:field :condition _preproc_expression)
                                 "\n"
                                 (:repeat variable_declaration)
                                 (:field :alternative
                                  (:choice
                                   (:choice
                                    (:alias preproc_else_in_derived_type preproc_else)
                                    (:alias preproc_elif_in_derived_type preproc_elif)
                                    (:alias preproc_elifdef_in_derived_type preproc_elifdef))
                                   :blank))))
  preproc_elifdef_in_derived_type (:prec 0
                                   (:seq
                                    (:choice
                                     (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                     (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                    (:field :name identifier)
                                    (:choice
                                     (:choice inline_preproc_comment multiline_preproc_comment)
                                     :blank)
                                    (:repeat variable_declaration)
                                    (:field :alternative
                                     (:choice
                                      (:choice
                                       (:alias preproc_else_in_derived_type preproc_else)
                                       (:alias preproc_elif_in_derived_type preproc_elif)
                                       (:alias preproc_elifdef_in_derived_type preproc_elifdef))
                                      :blank))))
  preproc_if_in_bound_procedures (:prec 0
                                  (:seq
                                   (:alias (:pattern "#[ \t]*if") "#if")
                                   (:field :condition _preproc_expression)
                                   (:choice
                                    (:choice inline_preproc_comment multiline_preproc_comment)
                                    :blank)
                                   "\n"
                                   (:repeat _procedure_binding)
                                   (:field :alternative
                                    (:choice
                                     (:choice
                                      (:alias preproc_else_in_bound_procedures preproc_else)
                                      (:alias preproc_elif_in_bound_procedures preproc_elif)
                                      (:alias preproc_elifdef_in_bound_procedures preproc_elifdef))
                                     :blank))
                                   (:alias (:pattern "#[ \t]*endif") "#endif")
                                   (:choice
                                    (:choice inline_preproc_comment multiline_preproc_comment)
                                    :blank)))
  preproc_ifdef_in_bound_procedures (:prec 0
                                     (:seq
                                      (:choice
                                       (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                       (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                      (:field :name identifier)
                                      (:choice
                                       (:choice inline_preproc_comment multiline_preproc_comment)
                                       :blank)
                                      (:repeat _procedure_binding)
                                      (:field :alternative
                                       (:choice
                                        (:choice
                                         (:alias preproc_else_in_bound_procedures preproc_else)
                                         (:alias preproc_elif_in_bound_procedures preproc_elif)
                                         (:alias
                                          preproc_elifdef_in_bound_procedures
                                          preproc_elifdef))
                                        :blank))
                                      (:alias (:pattern "#[ \t]*endif") "#endif")
                                      (:choice
                                       (:choice inline_preproc_comment multiline_preproc_comment)
                                       :blank)))
  preproc_else_in_bound_procedures (:prec 0
                                    (:seq
                                     (:alias (:pattern "#[ \t]*else") "#else")
                                     (:choice
                                      (:choice inline_preproc_comment multiline_preproc_comment)
                                      :blank)
                                     (:repeat _procedure_binding)))
  preproc_elif_in_bound_procedures (:prec 0
                                    (:seq
                                     (:alias (:pattern "#[ \t]*elif") "#elif")
                                     (:choice
                                      (:choice inline_preproc_comment multiline_preproc_comment)
                                      :blank)
                                     (:field :condition _preproc_expression)
                                     "\n"
                                     (:repeat _procedure_binding)
                                     (:field :alternative
                                      (:choice
                                       (:choice
                                        (:alias preproc_else_in_bound_procedures preproc_else)
                                        (:alias preproc_elif_in_bound_procedures preproc_elif)
                                        (:alias preproc_elifdef_in_bound_procedures preproc_elifdef))
                                       :blank))))
  preproc_elifdef_in_bound_procedures (:prec 0
                                       (:seq
                                        (:choice
                                         (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                         (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                        (:field :name identifier)
                                        (:choice
                                         (:choice inline_preproc_comment multiline_preproc_comment)
                                         :blank)
                                        (:repeat _procedure_binding)
                                        (:field :alternative
                                         (:choice
                                          (:choice
                                           (:alias preproc_else_in_bound_procedures preproc_else)
                                           (:alias preproc_elif_in_bound_procedures preproc_elif)
                                           (:alias
                                            preproc_elifdef_in_bound_procedures
                                            preproc_elifdef))
                                          :blank))))
  preproc_if_in_select_case (:prec 0
                             (:seq
                              (:alias (:pattern "#[ \t]*if") "#if")
                              (:field :condition _preproc_expression)
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)
                              "\n"
                              case_statement
                              (:field :alternative
                               (:choice
                                (:choice
                                 (:alias preproc_else_in_select_case preproc_else)
                                 (:alias preproc_elif_in_select_case preproc_elif)
                                 (:alias preproc_elifdef_in_select_case preproc_elifdef))
                                :blank))
                              (:alias (:pattern "#[ \t]*endif") "#endif")
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)))
  preproc_ifdef_in_select_case (:prec 0
                                (:seq
                                 (:choice
                                  (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                  (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                 (:field :name identifier)
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)
                                 case_statement
                                 (:field :alternative
                                  (:choice
                                   (:choice
                                    (:alias preproc_else_in_select_case preproc_else)
                                    (:alias preproc_elif_in_select_case preproc_elif)
                                    (:alias preproc_elifdef_in_select_case preproc_elifdef))
                                   :blank))
                                 (:alias (:pattern "#[ \t]*endif") "#endif")
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)))
  preproc_else_in_select_case (:prec 0
                               (:seq
                                (:alias (:pattern "#[ \t]*else") "#else")
                                (:choice
                                 (:choice inline_preproc_comment multiline_preproc_comment)
                                 :blank)
                                case_statement))
  preproc_elif_in_select_case (:prec 0
                               (:seq
                                (:alias (:pattern "#[ \t]*elif") "#elif")
                                (:choice
                                 (:choice inline_preproc_comment multiline_preproc_comment)
                                 :blank)
                                (:field :condition _preproc_expression)
                                "\n"
                                case_statement
                                (:field :alternative
                                 (:choice
                                  (:choice
                                   (:alias preproc_else_in_select_case preproc_else)
                                   (:alias preproc_elif_in_select_case preproc_elif)
                                   (:alias preproc_elifdef_in_select_case preproc_elifdef))
                                  :blank))))
  preproc_elifdef_in_select_case (:prec 0
                                  (:seq
                                   (:choice
                                    (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                    (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                   (:field :name identifier)
                                   (:choice
                                    (:choice inline_preproc_comment multiline_preproc_comment)
                                    :blank)
                                   case_statement
                                   (:field :alternative
                                    (:choice
                                     (:choice
                                      (:alias preproc_else_in_select_case preproc_else)
                                      (:alias preproc_elif_in_select_case preproc_elif)
                                      (:alias preproc_elifdef_in_select_case preproc_elifdef))
                                     :blank))))
  preproc_if_in_select_type (:prec 0
                             (:seq
                              (:alias (:pattern "#[ \t]*if") "#if")
                              (:field :condition _preproc_expression)
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)
                              "\n"
                              type_statement
                              (:field :alternative
                               (:choice
                                (:choice
                                 (:alias preproc_else_in_select_type preproc_else)
                                 (:alias preproc_elif_in_select_type preproc_elif)
                                 (:alias preproc_elifdef_in_select_type preproc_elifdef))
                                :blank))
                              (:alias (:pattern "#[ \t]*endif") "#endif")
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)))
  preproc_ifdef_in_select_type (:prec 0
                                (:seq
                                 (:choice
                                  (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                  (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                 (:field :name identifier)
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)
                                 type_statement
                                 (:field :alternative
                                  (:choice
                                   (:choice
                                    (:alias preproc_else_in_select_type preproc_else)
                                    (:alias preproc_elif_in_select_type preproc_elif)
                                    (:alias preproc_elifdef_in_select_type preproc_elifdef))
                                   :blank))
                                 (:alias (:pattern "#[ \t]*endif") "#endif")
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)))
  preproc_else_in_select_type (:prec 0
                               (:seq
                                (:alias (:pattern "#[ \t]*else") "#else")
                                (:choice
                                 (:choice inline_preproc_comment multiline_preproc_comment)
                                 :blank)
                                type_statement))
  preproc_elif_in_select_type (:prec 0
                               (:seq
                                (:alias (:pattern "#[ \t]*elif") "#elif")
                                (:choice
                                 (:choice inline_preproc_comment multiline_preproc_comment)
                                 :blank)
                                (:field :condition _preproc_expression)
                                "\n"
                                type_statement
                                (:field :alternative
                                 (:choice
                                  (:choice
                                   (:alias preproc_else_in_select_type preproc_else)
                                   (:alias preproc_elif_in_select_type preproc_elif)
                                   (:alias preproc_elifdef_in_select_type preproc_elifdef))
                                  :blank))))
  preproc_elifdef_in_select_type (:prec 0
                                  (:seq
                                   (:choice
                                    (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                    (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                   (:field :name identifier)
                                   (:choice
                                    (:choice inline_preproc_comment multiline_preproc_comment)
                                    :blank)
                                   type_statement
                                   (:field :alternative
                                    (:choice
                                     (:choice
                                      (:alias preproc_else_in_select_type preproc_else)
                                      (:alias preproc_elif_in_select_type preproc_elif)
                                      (:alias preproc_elifdef_in_select_type preproc_elifdef))
                                     :blank))))
  preproc_if_in_select_rank (:prec 0
                             (:seq
                              (:alias (:pattern "#[ \t]*if") "#if")
                              (:field :condition _preproc_expression)
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)
                              "\n"
                              rank_statement
                              (:field :alternative
                               (:choice
                                (:choice
                                 (:alias preproc_else_in_select_rank preproc_else)
                                 (:alias preproc_elif_in_select_rank preproc_elif)
                                 (:alias preproc_elifdef_in_select_rank preproc_elifdef))
                                :blank))
                              (:alias (:pattern "#[ \t]*endif") "#endif")
                              (:choice
                               (:choice inline_preproc_comment multiline_preproc_comment)
                               :blank)))
  preproc_ifdef_in_select_rank (:prec 0
                                (:seq
                                 (:choice
                                  (:alias (:pattern "#[ \t]*ifdef") "#ifdef")
                                  (:alias (:pattern "#[ \t]*ifndef") "#ifndef"))
                                 (:field :name identifier)
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)
                                 rank_statement
                                 (:field :alternative
                                  (:choice
                                   (:choice
                                    (:alias preproc_else_in_select_rank preproc_else)
                                    (:alias preproc_elif_in_select_rank preproc_elif)
                                    (:alias preproc_elifdef_in_select_rank preproc_elifdef))
                                   :blank))
                                 (:alias (:pattern "#[ \t]*endif") "#endif")
                                 (:choice
                                  (:choice inline_preproc_comment multiline_preproc_comment)
                                  :blank)))
  preproc_else_in_select_rank (:prec 0
                               (:seq
                                (:alias (:pattern "#[ \t]*else") "#else")
                                (:choice
                                 (:choice inline_preproc_comment multiline_preproc_comment)
                                 :blank)
                                rank_statement))
  preproc_elif_in_select_rank (:prec 0
                               (:seq
                                (:alias (:pattern "#[ \t]*elif") "#elif")
                                (:choice
                                 (:choice inline_preproc_comment multiline_preproc_comment)
                                 :blank)
                                (:field :condition _preproc_expression)
                                "\n"
                                rank_statement
                                (:field :alternative
                                 (:choice
                                  (:choice
                                   (:alias preproc_else_in_select_rank preproc_else)
                                   (:alias preproc_elif_in_select_rank preproc_elif)
                                   (:alias preproc_elifdef_in_select_rank preproc_elifdef))
                                  :blank))))
  preproc_elifdef_in_select_rank (:prec 0
                                  (:seq
                                   (:choice
                                    (:alias (:pattern "#[ \t]*elifdef") "#elifdef")
                                    (:alias (:pattern "#[ \t]*elifndef") "#elifndef"))
                                   (:field :name identifier)
                                   (:choice
                                    (:choice inline_preproc_comment multiline_preproc_comment)
                                    :blank)
                                   rank_statement
                                   (:field :alternative
                                    (:choice
                                     (:choice
                                      (:alias preproc_else_in_select_rank preproc_else)
                                      (:alias preproc_elif_in_select_rank preproc_elif)
                                      (:alias preproc_elifdef_in_select_rank preproc_elifdef))
                                     :blank))))
  preproc_arg (:token (:prec -1 (:pattern "\\S([^/\\n]|\\/[^*]|\\\\\\r?\\n)*")))
  preproc_directive (:pattern "#[ \\t]*[a-zA-Z0-9]\\w*")
  _preproc_expression (:choice
                       identifier
                       (:alias preproc_call_expression call_expression)
                       number_literal
                       string_literal
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
                             (:field :operator _preproc_unary_operator)
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
  inline_preproc_comment (:pattern "\\/\\/.*")
  multiline_preproc_comment (:pattern "\\/\\*([^*]|\\*+[^*/])*\\*+\\/")
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
  system_lib_string (:token (:seq "<" (:repeat (:choice (:pattern "[^>\\n]") "\\>")) ">"))
  program (:seq
           (:choice program_statement :blank)
           (:repeat
            (:choice
             _specification_part
             (:alias preproc_if_in_specification_part preproc_if)
             (:alias preproc_ifdef_in_specification_part preproc_ifdef)))
           (:repeat _statement)
           (:choice internal_procedures :blank)
           (:choice statement_label :blank)
           end_program_statement
           _end_of_statement)
  program_statement (:seq
                     (:alias (:pattern "[pP][rR][oO][gG][rR][aA][mM]") "program")
                     _name
                     _end_of_statement)
  end_program_statement (:choice
                         (:seq
                          (:alias (:pattern "[eE][nN][dD]") "end")
                          (:alias (:pattern "[pP][rR][oO][gG][rR][aA][mM]") "program")
                          (:choice _name :blank))
                         (:seq
                          (:alias
                           (:pattern "[eE][nN][dD][pP][rR][oO][gG][rR][aA][mM]")
                           "endprogram")
                          (:choice _name :blank))
                         (:alias (:pattern "[eE][nN][dD]") "end"))
  module (:seq
          module_statement
          (:repeat
           (:choice
            _specification_part
            (:alias preproc_if_in_module preproc_if)
            (:alias preproc_ifdef_in_module preproc_ifdef)))
          (:choice internal_procedures :blank)
          end_module_statement
          _end_of_statement)
  module_statement (:seq
                    (:alias (:pattern "[mM][oO][dD][uU][lL][eE]") "module")
                    _name
                    _end_of_statement)
  end_module_statement (:choice
                        (:seq
                         (:alias (:pattern "[eE][nN][dD]") "end")
                         (:alias (:pattern "[mM][oO][dD][uU][lL][eE]") "module")
                         (:choice _name :blank))
                        (:seq
                         (:alias (:pattern "[eE][nN][dD][mM][oO][dD][uU][lL][eE]") "endmodule")
                         (:choice _name :blank))
                        (:alias (:pattern "[eE][nN][dD]") "end"))
  submodule (:seq
             submodule_statement
             (:repeat
              (:choice
               _specification_part
               (:alias preproc_if_in_module preproc_if)
               (:alias preproc_ifdef_in_module preproc_ifdef)))
             (:choice internal_procedures :blank)
             end_submodule_statement
             _end_of_statement)
  submodule_statement (:seq
                       (:alias (:pattern "[sS][uU][bB][mM][oO][dD][uU][lL][eE]") "submodule")
                       "("
                       (:field :ancestor module_name)
                       (:choice (:seq ":" (:field :parent module_name)) :blank)
                       ")"
                       _name
                       _end_of_statement)
  end_submodule_statement (:choice
                           (:seq
                            (:alias (:pattern "[eE][nN][dD]") "end")
                            (:alias (:pattern "[sS][uU][bB][mM][oO][dD][uU][lL][eE]") "submodule")
                            (:choice _name :blank))
                           (:seq
                            (:alias
                             (:pattern "[eE][nN][dD][sS][uU][bB][mM][oO][dD][uU][lL][eE]")
                             "endsubmodule")
                            (:choice _name :blank))
                           (:alias (:pattern "[eE][nN][dD]") "end"))
  module_name _name
  interface (:seq
             interface_statement
             (:repeat
              (:choice
               _interface_items
               include_statement
               preproc_include
               preproc_def
               preproc_function_def
               preproc_call
               (:alias preproc_if_in_interface preproc_if)
               (:alias preproc_ifdef_in_interface preproc_ifdef)))
             end_interface_statement
             _end_of_statement)
  _interface_items (:choice import_statement procedure_statement function subroutine ";")
  interface_statement (:seq
                       (:choice abstract_specifier :blank)
                       (:alias (:pattern "[iI][nN][tT][eE][rR][fF][aA][cC][eE]") "interface")
                       (:choice (:choice _name _generic_procedure) :blank)
                       _end_of_statement)
  end_interface_statement (:choice
                           (:seq
                            (:alias (:pattern "[eE][nN][dD]") "end")
                            (:alias (:pattern "[iI][nN][tT][eE][rR][fF][aA][cC][eE]") "interface")
                            (:choice _end_interface_spec :blank))
                           (:seq
                            (:alias
                             (:pattern "[eE][nN][dD][iI][nN][tT][eE][rR][fF][aA][cC][eE]")
                             "endinterface")
                            (:choice _end_interface_spec :blank))
                           (:alias (:pattern "[eE][nN][dD]") "end"))
  _end_interface_spec (:choice _name _generic_procedure)
  block_data (:seq
              block_data_statement
              (:repeat
               (:choice
                _specification_part
                (:alias preproc_if_in_module preproc_if)
                (:alias preproc_ifdef_in_module preproc_ifdef)))
              end_block_data_statement
              _end_of_statement)
  block_data_statement (:seq
                        (:choice
                         (:seq
                          (:alias (:pattern "[bB][lL][oO][cC][kK]") "block")
                          (:alias (:pattern "[dD][aA][tT][aA]") "data"))
                         (:alias (:pattern "[bB][lL][oO][cC][kK][dD][aA][tT][aA]") "blockdata"))
                        (:choice _name :blank)
                        _end_of_statement)
  end_block_data_statement (:choice
                            (:seq
                             (:alias (:pattern "[eE][nN][dD]") "end")
                             (:alias (:pattern "[bB][lL][oO][cC][kK]") "block")
                             (:alias (:pattern "[dD][aA][tT][aA]") "data")
                             (:choice _name :blank))
                            (:seq
                             (:alias (:pattern "[eE][nN][dD][bB][lL][oO][cC][kK]") "endblock")
                             (:alias (:pattern "[dD][aA][tT][aA]") "data")
                             (:choice _name :blank))
                            (:seq
                             (:alias (:pattern "[eE][nN][dD]") "end")
                             (:alias (:pattern "[bB][lL][oO][cC][kK][dD][aA][tT][aA]") "blockdata")
                             (:choice _name :blank))
                            (:seq
                             (:alias
                              (:pattern "[eE][nN][dD][bB][lL][oO][cC][kK][dD][aA][tT][aA]")
                              "endblockdata")
                             (:choice _name :blank))
                            (:seq
                             (:alias (:pattern "[eE][nN][dD]") "end")
                             (:alias (:pattern "[bB][lL][oO][cC][kK]") "block"))
                            (:alias (:pattern "[eE][nN][dD][bB][lL][oO][cC][kK]") "endblock")
                            (:alias (:pattern "[eE][nN][dD]") "end"))
  assignment (:seq
              (:alias (:pattern "[aA][sS][sS][iI][gG][nN][mM][eE][nN][tT]") "assignment")
              "("
              "="
              ")")
  operator (:seq
            (:alias (:pattern "[oO][pP][eE][rR][aA][tT][oO][rR]") "operator")
            "("
            (:alias (:pattern "[^()]+") operator_name)
            ")")
  defined_io_procedure (:seq
                        (:choice
                         (:alias (:pattern "[rR][eE][aA][dD]") "read")
                         (:alias (:pattern "[wW][rR][iI][tT][eE]") "write"))
                        "("
                        (:choice
                         (:alias (:pattern "[fF][oO][rR][mM][aA][tT][tT][eE][dD]") "formatted")
                         (:alias
                          (:pattern "[uU][nN][fF][oO][rR][mM][aA][tT][tT][eE][dD]")
                          "unformatted"))
                        ")")
  _generic_procedure (:choice assignment operator defined_io_procedure)
  subroutine (:seq
              subroutine_statement
              (:repeat
               (:choice
                _specification_part
                (:alias preproc_if_in_specification_part preproc_if)
                (:alias preproc_ifdef_in_specification_part preproc_ifdef)))
              (:repeat
               (:choice
                _statement
                (:alias preproc_if_in_procedure_statements preproc_if)
                (:alias preproc_ifdef_in_procedure_statements preproc_ifdef)))
              (:choice internal_procedures :blank)
              (:choice statement_label :blank)
              end_subroutine_statement
              _end_of_statement)
  subroutine_statement (:seq
                        (:choice _callable_interface_qualifers :blank)
                        (:alias (:pattern "[sS][uU][bB][rR][oO][uU][tT][iI][nN][eE]") "subroutine")
                        (:field :name _name)
                        (:choice (:field :parameters _parameters) :blank)
                        (:choice language_binding :blank)
                        _end_of_statement)
  end_subroutine_statement (:choice
                            (:seq
                             (:alias (:pattern "[eE][nN][dD]") "end")
                             (:alias
                              (:pattern "[sS][uU][bB][rR][oO][uU][tT][iI][nN][eE]")
                              "subroutine")
                             (:choice _name :blank))
                            (:seq
                             (:alias
                              (:pattern "[eE][nN][dD][sS][uU][bB][rR][oO][uU][tT][iI][nN][eE]")
                              "endsubroutine")
                             (:choice _name :blank))
                            (:alias (:pattern "[eE][nN][dD]") "end"))
  module_procedure (:seq
                    module_procedure_statement
                    (:repeat
                     (:choice
                      _specification_part
                      (:alias preproc_if_in_specification_part preproc_if)
                      (:alias preproc_ifdef_in_specification_part preproc_ifdef)))
                    (:repeat
                     (:choice
                      _statement
                      (:alias preproc_if_in_procedure_statements preproc_if)
                      (:alias preproc_ifdef_in_procedure_statements preproc_ifdef)))
                    (:choice internal_procedures :blank)
                    (:choice statement_label :blank)
                    end_module_procedure_statement
                    _end_of_statement)
  module_procedure_statement (:seq
                              (:choice _callable_interface_qualifers :blank)
                              (:seq
                               (:alias (:pattern "[mM][oO][dD][uU][lL][eE]") "module")
                               (:alias
                                (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]")
                                "procedure"))
                              (:field :name _name)
                              _end_of_statement)
  end_module_procedure_statement (:choice
                                  (:seq
                                   (:alias (:pattern "[eE][nN][dD]") "end")
                                   (:alias
                                    (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]")
                                    "procedure")
                                   (:choice _name :blank))
                                  (:seq
                                   (:alias
                                    (:pattern "[eE][nN][dD][pP][rR][oO][cC][eE][dD][uU][rR][eE]")
                                    "endprocedure")
                                   (:choice _name :blank))
                                  (:alias (:pattern "[eE][nN][dD]") "end"))
  function (:seq
            function_statement
            (:repeat
             (:choice
              _specification_part
              (:alias preproc_if_in_specification_part preproc_if)
              (:alias preproc_ifdef_in_specification_part preproc_ifdef)))
            (:repeat
             (:choice
              _statement
              (:alias preproc_if_in_procedure_statements preproc_if)
              (:alias preproc_ifdef_in_procedure_statements preproc_ifdef)))
            (:choice internal_procedures :blank)
            (:choice statement_label :blank)
            end_function_statement
            _end_of_statement)
  function_statement (:seq
                      (:choice _callable_interface_qualifers :blank)
                      (:alias (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]") "function")
                      (:field :name _name)
                      (:choice (:field :parameters _parameters) :blank)
                      (:choice (:repeat (:choice language_binding function_result)) :blank)
                      _end_of_statement)
  language_binding (:seq
                    (:alias (:pattern "[bB][iI][nN][dD]") "bind")
                    "("
                    identifier
                    (:choice (:seq "," keyword_argument) :blank)
                    ")")
  _callable_interface_qualifers (:repeat1
                                 (:prec-right 1
                                  (:choice
                                   procedure_attributes
                                   procedure_qualifier
                                   (:field :type intrinsic_type)
                                   (:field :type derived_type))))
  procedure_attributes (:prec 1
                        (:seq
                         (:alias (:pattern "[aA][tT][tT][rR][iI][bB][uU][tT][eE][sS]") "attributes")
                         "("
                         (:seq
                          (:choice
                           (:alias (:pattern "[gG][lL][oO][bB][aA][lL]") "global")
                           (:alias (:pattern "[dD][eE][vV][iI][cC][eE]") "device")
                           (:alias (:pattern "[hH][oO][sS][tT]") "host")
                           (:alias
                            (:pattern "[gG][rR][iI][dD]_[gG][lL][oO][bB][aA][lL]")
                            "grid_global"))
                          (:repeat
                           (:seq
                            ","
                            (:choice
                             (:alias (:pattern "[gG][lL][oO][bB][aA][lL]") "global")
                             (:alias (:pattern "[dD][eE][vV][iI][cC][eE]") "device")
                             (:alias (:pattern "[hH][oO][sS][tT]") "host")
                             (:alias
                              (:pattern "[gG][rR][iI][dD]_[gG][lL][oO][bB][aA][lL]")
                              "grid_global")))))
                         ")"))
  end_function_statement (:choice
                          (:seq
                           (:alias (:pattern "[eE][nN][dD]") "end")
                           (:alias (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]") "function")
                           (:choice _name :blank))
                          (:seq
                           (:alias
                            (:pattern "[eE][nN][dD][fF][uU][nN][cC][tT][iI][oO][nN]")
                            "endfunction")
                           (:choice _name :blank))
                          (:alias (:pattern "[eE][nN][dD]") "end"))
  function_result (:seq (:alias (:pattern "[rR][eE][sS][uU][lL][tT]") "result") "(" identifier ")")
  _name (:alias identifier name)
  _parameters (:choice (:seq "(" ")") parameters)
  parameters (:seq "(" (:seq identifier (:repeat (:seq "," identifier))) ")")
  internal_procedures (:seq contains_statement _end_of_statement (:repeat _internal_procedures))
  contains_statement (:alias (:pattern "[cC][oO][nN][tT][aA][iI][nN][sS]") "contains")
  _internal_procedures (:choice
                        function
                        module_procedure
                        subroutine
                        include_statement
                        (:alias preproc_if_in_internal_procedures preproc_if)
                        (:alias preproc_ifdef_in_internal_procedures preproc_ifdef)
                        preproc_include
                        preproc_def
                        preproc_function_def
                        preproc_call)
  _specification_part (:prec 1
                       (:choice
                        (:seq _specification_parts _end_of_statement)
                        interface
                        derived_type_definition
                        (:prec 1 (:seq statement_label format_statement _end_of_statement))
                        preproc_include
                        preproc_def
                        preproc_function_def
                        preproc_call
                        (:alias preproc_if_in_specification_part preproc_if)
                        (:alias preproc_ifdef_in_specification_part preproc_ifdef)
                        ";"))
  _specification_parts (:prec 1
                        (:choice
                         include_statement
                         use_statement
                         implicit_statement
                         save_statement
                         import_statement
                         public_statement
                         private_statement
                         bind_statement
                         enum
                         enumeration_type
                         namelist_statement
                         common_statement
                         variable_declaration
                         variable_modification
                         parameter_statement
                         equivalence_statement
                         data_statement
                         cray_pointer_declaration
                         assignment_statement
                         entry_statement))
  use_statement (:seq
                 (:alias (:pattern "[uU][sS][eE]") "use")
                 (:choice
                  (:choice
                   (:seq
                    ","
                    (:choice
                     (:alias (:pattern "[iI][nN][tT][rR][iI][nN][sS][iI][cC]") "intrinsic")
                     (:alias
                      (:pattern "[nN][oO][nN]_[iI][nN][tT][rR][iI][nN][sS][iI][cC]")
                      "non_intrinsic"))
                    "::")
                   :blank)
                  (:choice "::" :blank))
                 (:alias identifier module_name)
                 (:choice
                  (:choice
                   (:seq "," (:seq use_alias (:repeat (:seq "," use_alias))))
                   included_items)
                  :blank))
  included_items (:seq
                  ","
                  (:alias (:pattern "[oO][nN][lL][yY]") "only")
                  ":"
                  (:choice
                   (:seq
                    (:choice use_alias identifier _generic_procedure)
                    (:repeat (:seq "," (:choice use_alias identifier _generic_procedure))))
                   :blank))
  use_alias (:seq (:alias identifier local_name) "=>" identifier)
  implicit_statement (:seq
                      (:alias (:pattern "[iI][mM][pP][lL][iI][cC][iI][tT]") "implicit")
                      (:choice
                       (:seq
                        (:seq
                         (:choice intrinsic_type derived_type)
                         "("
                         (:seq implicit_range (:repeat (:seq "," implicit_range)))
                         ")")
                        (:repeat
                         (:seq
                          ","
                          (:seq
                           (:choice intrinsic_type derived_type)
                           "("
                           (:seq implicit_range (:repeat (:seq "," implicit_range)))
                           ")"))))
                       (:seq
                        (:alias (:pattern "[nN][oO][nN][eE]") none)
                        (:choice
                         (:seq
                          "("
                          (:seq
                           (:choice
                            (:alias (:pattern "[tT][yY][pP][eE]") "type")
                            (:alias (:pattern "[eE][xX][tT][eE][rR][nN][aA][lL]") "external"))
                           (:repeat
                            (:seq
                             ","
                             (:choice
                              (:alias (:pattern "[tT][yY][pP][eE]") "type")
                              (:alias (:pattern "[eE][xX][tT][eE][rR][nN][aA][lL]") "external")))))
                          ")")
                         :blank))))
  save_statement (:prec 1
                  (:seq
                   (:alias (:pattern "[sS][aA][vV][eE]") "save")
                   (:choice _identifier_or_common_block :blank)))
  bind_statement (:seq language_binding _identifier_or_common_block)
  _identifier_or_common_block (:seq
                               (:choice "::" :blank)
                               (:seq
                                (:choice identifier (:seq "/" (:alias identifier common_block) "/"))
                                (:repeat
                                 (:seq
                                  ","
                                  (:choice
                                   identifier
                                   (:seq "/" (:alias identifier common_block) "/"))))))
  private_statement (:prec-right 1
                     (:seq
                      (:alias (:pattern "[pP][rR][iI][vV][aA][tT][eE]") "private")
                      (:choice
                       (:seq
                        (:choice "::" :blank)
                        (:seq
                         (:choice identifier _generic_procedure)
                         (:repeat (:seq "," (:choice identifier _generic_procedure)))))
                       :blank)))
  public_statement (:prec-right 1
                    (:seq
                     (:alias (:pattern "[pP][uU][bB][lL][iI][cC]") "public")
                     (:choice
                      (:seq
                       (:choice "::" :blank)
                       (:seq
                        (:choice identifier _generic_procedure)
                        (:repeat (:seq "," (:choice identifier _generic_procedure)))))
                      :blank)))
  namelist_statement (:seq
                      (:alias (:pattern "[nN][aA][mM][eE][lL][iI][sS][tT]") "namelist")
                      (:repeat1 variable_group))
  common_statement (:seq
                    (:alias (:pattern "[cC][oO][mM][mM][oO][nN]") "common")
                    (:repeat1
                     (:choice
                      variable_group
                      (:seq _variable_declarator (:repeat (:seq "," _variable_declarator))))))
  variable_group (:seq
                  "/"
                  _name
                  "/"
                  (:seq _variable_declarator (:repeat (:seq "," _variable_declarator))))
  implicit_range (:seq (:pattern "[a-zA-Z]") (:choice (:seq "-" (:pattern "[a-zA-Z]")) :blank))
  import_statement (:prec-left 0
                    (:seq
                     (:alias (:pattern "[iI][mM][pP][oO][rR][tT]") "import")
                     (:choice _import_names :blank)))
  _import_names (:choice
                 (:seq (:choice "::" :blank) (:seq identifier (:repeat (:seq "," identifier))))
                 (:seq
                  ","
                  (:choice
                   (:seq
                    (:alias (:pattern "[oO][nN][lL][yY]") "only")
                    ":"
                    (:seq identifier (:repeat (:seq "," identifier))))
                   (:alias (:pattern "[nN][oO][nN][eE]") "none")
                   (:alias (:pattern "[aA][lL][lL]") "all"))))
  derived_type_definition (:seq
                           derived_type_statement
                           (:repeat
                            (:choice
                             (:seq
                              (:choice
                               public_statement
                               private_statement
                               (:alias
                                (:pattern "[sS][eE][qQ][uU][eE][nN][cC][eE]")
                                sequence_statement)
                               include_statement)
                              _end_of_statement)
                             (:seq variable_declaration _end_of_statement)
                             preproc_include
                             preproc_def
                             preproc_function_def
                             preproc_call
                             (:alias preproc_if_in_derived_type preproc_if)
                             (:alias preproc_ifdef_in_derived_type preproc_ifdef)))
                           (:choice derived_type_procedures :blank)
                           end_type_statement
                           _end_of_statement)
  abstract_specifier (:alias (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]") "abstract")
  access_specifier (:choice
                    (:alias (:pattern "[pP][uU][bB][lL][iI][cC]") "public")
                    (:alias (:pattern "[pP][rR][iI][vV][aA][tT][eE]") "private"))
  base_type_specifier (:seq
                       (:alias (:pattern "[eE][xX][tT][eE][nN][dD][sS]") "extends")
                       "("
                       identifier
                       ")")
  _derived_type_qualifier (:choice
                           abstract_specifier
                           (:field :access access_specifier)
                           (:field :base base_type_specifier)
                           language_binding)
  derived_type_statement (:seq
                          (:choice statement_label :blank)
                          (:alias (:pattern "[tT][yY][pP][eE]") "type")
                          (:choice
                           (:seq (:choice "::" :blank) _type_name)
                           (:seq
                            ","
                            (:seq
                             _derived_type_qualifier
                             (:repeat (:seq "," _derived_type_qualifier)))
                            "::"
                            _type_name))
                          (:choice (:alias argument_list derived_type_parameter_list) :blank)
                          _end_of_statement)
  end_type_statement (:choice
                      (:seq
                       (:alias (:pattern "[eE][nN][dD]") "end")
                       (:alias (:pattern "[tT][yY][pP][eE]") "type")
                       (:choice _name :blank))
                      (:seq
                       (:alias (:pattern "[eE][nN][dD][tT][yY][pP][eE]") "endtype")
                       (:choice _name :blank))
                      (:alias (:pattern "[eE][nN][dD]") "end"))
  _type_name (:alias identifier type_name)
  derived_type_procedures (:seq
                           contains_statement
                           (:repeat
                            (:choice
                             (:alias "private" private_statement)
                             _procedure_binding
                             include_statement
                             (:alias preproc_if_in_bound_procedures preproc_if)
                             (:alias preproc_ifdef_in_bound_procedures preproc_ifdef))))
  _procedure_binding (:choice procedure_statement generic_statement final_statement)
  procedure_statement (:seq
                       procedure_kind
                       (:choice (:seq "(" (:alias identifier procedure_interface) ")") :blank)
                       (:choice
                        (:seq
                         ","
                         (:seq procedure_attribute (:repeat (:seq "," procedure_attribute))))
                        :blank)
                       (:choice "::" :blank)
                       (:seq
                        (:field :declarator (:choice _method_name binding))
                        (:repeat (:seq "," (:field :declarator (:choice _method_name binding))))))
  generic_statement (:seq
                     (:alias (:pattern "[gG][eE][nN][eE][rR][iI][cC]") "generic")
                     (:choice (:seq "," (:prec-left 0 access_specifier)) :blank)
                     "::"
                     (:field :declarator binding_list))
  final_statement (:seq
                   (:alias (:pattern "[fF][iI][nN][aA][lL]") "final")
                   "::"
                   (:seq
                    (:field :declarator _method_name)
                    (:repeat (:seq "," (:field :declarator _method_name)))))
  binding (:seq binding_name "=>" _method_name)
  binding_name (:choice identifier _generic_procedure)
  binding_list (:seq binding_name "=>" (:seq _method_name (:repeat (:seq "," _method_name))))
  _method_name (:alias identifier method_name)
  procedure_kind (:choice
                  (:alias (:pattern "[iI][nN][iI][tT][iI][aA][lL]") "initial")
                  (:alias (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]") "procedure")
                  (:seq
                   (:alias (:pattern "[mM][oO][dD][uU][lL][eE]") "module")
                   (:alias (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]") "procedure"))
                  (:alias (:pattern "[pP][rR][oO][pP][eE][rR][tT][yY]") "property"))
  procedure_attribute (:prec-left 0
                       (:choice
                        (:alias (:pattern "[dD][eE][fF][eE][rR][rR][eE][dD]") "deferred")
                        (:seq
                         (:alias (:pattern "[pP][aA][sS][sS]") "pass")
                         (:choice (:seq "(" identifier ")") :blank))
                        (:alias (:pattern "[nN][oO][pP][aA][sS][sS]") "nopass")
                        (:alias
                         (:pattern "[nN][oO][nN]_[oO][vV][eE][rR][rR][iI][dD][aA][bB][lL][eE]")
                         "non_overridable")
                        (:alias (:pattern "[pP][uU][bB][lL][iI][cC]") "public")
                        (:alias (:pattern "[pP][rR][iI][vV][aA][tT][eE]") "private")
                        (:alias (:pattern "[fF][aA][mM][iI][lL][yY]") "family")
                        (:alias (:pattern "[pP][oO][iI][nN][tT][eE][rR]") "pointer")))
  variable_declaration (:seq
                        (:field :type
                         (:choice
                          intrinsic_type
                          derived_type
                          (:alias procedure_declaration procedure)
                          declared_type))
                        (:choice
                         (:seq
                          ","
                          (:seq
                           (:field :attribute (:choice type_qualifier language_binding))
                           (:repeat
                            (:seq "," (:field :attribute (:choice type_qualifier language_binding))))))
                         :blank)
                        (:choice "::" :blank)
                        _declaration_targets)
  procedure_declaration (:seq
                         (:alias (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]") "procedure")
                         (:choice
                          (:seq
                           "("
                           (:choice
                            (:choice
                             (:alias identifier procedure_interface)
                             intrinsic_type
                             derived_type)
                            :blank)
                           ")")
                          :blank)
                         (:choice
                          (:seq
                           ","
                           (:seq procedure_attribute (:repeat (:seq "," procedure_attribute))))
                          :blank))
  variable_modification (:seq
                         (:choice
                          (:alias _standalone_type_qualifier type_qualifier)
                          variable_attributes)
                         (:choice "::" :blank)
                         (:seq
                          (:field :declarator _variable_declarator)
                          (:repeat (:seq "," (:field :declarator _variable_declarator)))))
  variable_attributes (:seq
                       (:alias (:pattern "[aA][tT][tT][rR][iI][bB][uU][tT][eE][sS]") "attributes")
                       "("
                       (:choice
                        (:alias (:pattern "[dD][eE][vV][iI][cC][eE]") "device")
                        (:alias (:pattern "[mM][aA][nN][aA][gG][eE][dD]") "managed")
                        (:alias (:pattern "[cC][oO][nN][sS][tT][aA][nN][tT]") "constant")
                        (:alias (:pattern "[sS][hH][aA][rR][eE][dD]") "shared")
                        (:alias (:pattern "[pP][iI][nN][nN][eE][dD]") "pinned")
                        (:alias (:pattern "[tT][eE][xX][tT][uU][rR][eE]") "texture"))
                       ")")
  _variable_declarator (:choice identifier sized_declarator coarray_declarator)
  sized_declarator (:prec-right 1
                    (:seq
                     identifier
                     (:choice
                      (:seq (:alias argument_list size) (:choice character_length :blank))
                      character_length)))
  _declaration_assignment (:seq (:field :left _variable_declarator) "=" (:field :right _expression))
  _declaration_pointer_association (:seq
                                    (:field :left _variable_declarator)
                                    "=>"
                                    (:field :right _expression))
  data_declarator (:seq (:field :left _variable_declarator) (:field :right data_value))
  _declaration_targets (:seq
                        (:field :declarator
                         (:choice
                          _variable_declarator
                          (:alias _declaration_assignment init_declarator)
                          (:alias _declaration_pointer_association pointer_init_declarator)
                          data_declarator))
                        (:repeat
                         (:seq
                          ","
                          (:field :declarator
                           (:choice
                            _variable_declarator
                            (:alias _declaration_assignment init_declarator)
                            (:alias _declaration_pointer_association pointer_init_declarator)
                            data_declarator)))))
  _intrinsic_type (:choice
                   (:alias (:pattern "[bB][yY][tT][eE]") "byte")
                   (:alias (:pattern "[iI][nN][tT][eE][gG][eE][rR]") "integer")
                   (:alias (:pattern "[rR][eE][aA][lL]") "real")
                   (:choice
                    (:seq
                     (:alias (:pattern "[dD][oO][uU][bB][lL][eE]") "double")
                     (:alias (:pattern "[pP][rR][eE][cC][iI][sS][iI][oO][nN]") "precision"))
                    (:alias
                     (:pattern "[dD][oO][uU][bB][lL][eE][pP][rR][eE][cC][iI][sS][iI][oO][nN]")
                     "doubleprecision"))
                   (:alias (:pattern "[cC][oO][mM][pP][lL][eE][xX]") "complex")
                   (:choice
                    (:seq
                     (:alias (:pattern "[dD][oO][uU][bB][lL][eE]") "double")
                     (:alias (:pattern "[cC][oO][mM][pP][lL][eE][xX]") "complex"))
                    (:alias
                     (:pattern "[dD][oO][uU][bB][lL][eE][cC][oO][mM][pP][lL][eE][xX]")
                     "doublecomplex"))
                   (:alias (:pattern "[lL][oO][gG][iI][cC][aA][lL]") "logical")
                   (:alias (:pattern "[cC][hH][aA][rR][aA][cC][tT][eE][rR]") "character"))
  intrinsic_type (:seq _intrinsic_type (:choice (:field :kind kind) :blank))
  derived_type (:seq
                (:choice
                 (:alias (:pattern "[tT][yY][pP][eE]") "type")
                 (:alias (:pattern "[cC][lL][aA][sS][sS]") "class"))
                "("
                (:choice
                 (:seq
                  (:field :name
                   (:choice (:prec-dynamic 1 (:alias _intrinsic_type intrinsic_type)) _type_name))
                  (:choice (:field :kind kind) :blank))
                 unlimited_polymorphic)
                ")")
  declared_type (:seq
                 (:choice
                  (:alias (:pattern "[tT][yY][pP][eE][oO][fF]") "typeof")
                  (:alias (:pattern "[cC][lL][aA][sS][sS][oO][fF]") "classof"))
                 "("
                 (:field :name (:choice identifier derived_type_member_expression))
                 ")")
  unlimited_polymorphic "*"
  kind (:choice
        (:seq (:choice (:alias "*" assumed_size) :blank) _argument_list)
        (:seq "*" (:choice (:alias (:pattern "\\d+") number_literal) parenthesized_expression)))
  character_length (:seq "*" (:choice (:pattern "\\d+") :blank) (:choice (:seq "(" "*" ")") :blank))
  _standalone_type_qualifier (:choice
                              (:alias (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]") "abstract")
                              (:alias
                               (:pattern "[aA][lL][lL][oO][cC][aA][tT][aA][bB][lL][eE]")
                               "allocatable")
                              (:alias
                               (:pattern "[aA][sS][yY][nN][cC][hH][rR][oO][nN][oO][uU][sS]")
                               "asynchronous")
                              (:alias (:pattern "[aA][uU][tT][oO][mM][aA][tT][iI][cC]") "automatic")
                              (:prec-right 0
                               (:seq
                                (:alias
                                 (:pattern "[cC][oO][dD][iI][mM][eE][nN][sS][iI][oO][nN]")
                                 "codimension")
                                (:alias coarray_index coarray_size)))
                              (:prec-right 0
                               (:seq
                                (:alias
                                 (:pattern "[dD][iI][mM][eE][nN][sS][iI][oO][nN]")
                                 "dimension")
                                (:choice argument_list :blank)))
                              (:alias (:pattern "[cC][oO][nN][sS][tT][aA][nN][tT]") "constant")
                              (:alias
                               (:pattern "[cC][oO][nN][tT][iI][gG][uU][oO][uU][sS]")
                               "contiguous")
                              (:alias (:pattern "[dD][eE][vV][iI][cC][eE]") "device")
                              (:alias (:pattern "[eE][xX][tT][eE][rR][nN][aA][lL]") "external")
                              (:seq
                               (:alias (:pattern "[iI][nN][tT][eE][nN][tT]") "intent")
                               "("
                               (:choice
                                (:alias (:pattern "[iI][nN]") "in")
                                (:alias (:pattern "[oO][uU][tT]") "out")
                                (:choice
                                 (:seq
                                  (:alias (:pattern "[iI][nN]") "in")
                                  (:alias (:pattern "[oO][uU][tT]") "out"))
                                 (:alias (:pattern "[iI][nN][oO][uU][tT]") "inout")))
                               ")")
                              (:alias (:pattern "[iI][nN][tT][rR][iI][nN][sS][iI][cC]") "intrinsic")
                              (:alias (:pattern "[mM][aA][nN][aA][gG][eE][dD]") "managed")
                              (:alias (:pattern "[oO][pP][tT][iI][oO][nN][aA][lL]") "optional")
                              (:alias (:pattern "[pP][aA][rR][aA][mM][eE][tT][eE][rR]") "parameter")
                              (:alias (:pattern "[pP][iI][nN][nN][eE][dD]") "pinned")
                              (:alias (:pattern "[pP][oO][iI][nN][tT][eE][rR]") "pointer")
                              (:alias (:pattern "[pP][rR][iI][vV][aA][tT][eE]") "private")
                              (:alias (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]") "protected")
                              (:alias (:pattern "[pP][uU][bB][lL][iI][cC]") "public")
                              (:seq (:alias (:pattern "[rR][aA][nN][kK]") "rank") argument_list)
                              (:alias (:pattern "[sS][aA][vV][eE]") "save")
                              (:alias (:pattern "[sS][eE][qQ][uU][eE][nN][cC][eE]") "sequence")
                              (:alias (:pattern "[sS][hH][aA][rR][eE][dD]") "shared")
                              (:alias (:pattern "[sS][tT][aA][tT][iI][cC]") "static")
                              (:alias (:pattern "[tT][aA][rR][gG][eE][tT]") "target")
                              (:alias (:pattern "[tT][eE][xX][tT][uU][rR][eE]") "texture")
                              (:alias (:pattern "[vV][aA][lL][uU][eE]") "value")
                              (:alias (:pattern "[vV][oO][lL][aA][tT][iI][lL][eE]") "volatile"))
  type_qualifier (:choice
                  _standalone_type_qualifier
                  (:field :type_param (:alias (:pattern "[kK][iI][nN][dD]") "kind"))
                  (:field :type_param (:alias (:pattern "[lL][eE][nN]") "len")))
  procedure_qualifier (:choice
                       (:alias (:pattern "[eE][lL][eE][mM][eE][nN][tT][aA][lL]") "elemental")
                       (:alias (:pattern "[iI][mM][pP][uU][rR][eE]") "impure")
                       (:alias (:pattern "[mM][oO][dD][uU][lL][eE]") "module")
                       (:alias (:pattern "[pP][uU][rR][eE]") "pure")
                       (:alias (:pattern "[rR][eE][cC][uU][rR][sS][iI][vV][eE]") "recursive")
                       (:alias (:pattern "[sS][iI][mM][pP][lL][eE]") "simple"))
  parameter_statement (:prec 1
                       (:seq
                        (:alias (:pattern "[pP][aA][rR][aA][mM][eE][tT][eE][rR]") "parameter")
                        "("
                        (:seq parameter_assignment (:repeat (:seq "," parameter_assignment)))
                        ")"))
  parameter_assignment (:seq identifier "=" _expression)
  equivalence_statement (:seq
                         (:alias
                          (:pattern "[eE][qQ][uU][iI][vV][aA][lL][eE][nN][cC][eE]")
                          "equivalence")
                         (:seq equivalence_set (:repeat (:seq "," equivalence_set))))
  equivalence_set (:seq
                   "("
                   (:choice identifier call_expression)
                   ","
                   (:seq
                    (:choice identifier call_expression)
                    (:repeat (:seq "," (:choice identifier call_expression))))
                   ")")
  cray_pointer_declaration (:seq
                            (:alias (:pattern "[pP][oO][iI][nN][tT][eE][rR]") "pointer")
                            (:seq cray_pointer_pair (:repeat (:seq "," cray_pointer_pair))))
  cray_pointer_pair (:seq
                     "("
                     (:field :pointer identifier)
                     ","
                     (:field :target _variable_declarator)
                     ")")
  _statement (:choice
              (:alias preproc_if_in_statements preproc_if)
              (:alias preproc_ifdef_in_statements preproc_ifdef)
              preproc_include
              preproc_def
              preproc_function_def
              preproc_call
              (:seq (:choice statement_label :blank) _statements _end_of_statement)
              ";")
  _statements (:choice
               assignment_statement
               pointer_association_statement
               subroutine_call
               keyword_statement
               if_statement
               arithmetic_if_statement
               where_statement
               forall_statement
               select_case_statement
               select_type_statement
               select_rank_statement
               do_loop
               format_statement
               open_statement
               close_statement
               print_statement
               write_statement
               read_statement
               inquire_statement
               stop_statement
               block_construct
               associate_statement
               file_position_statement
               allocate_statement
               deallocate_statement
               nullify_statement
               entry_statement
               assign_statement
               coarray_statement
               coarray_team_statement
               coarray_critical_statement
               call_expression
               include_statement)
  statement_label (:prec 1 (:alias _integer_literal "statement_label"))
  statement_label_reference (:alias statement_label "statement_label_reference")
  stop_statement (:seq
                  (:choice (:alias (:pattern "[eE][rR][rR][oO][rR]") "error") :blank)
                  (:alias (:pattern "[sS][tT][oO][pP]") "stop")
                  (:choice _expression :blank)
                  (:choice
                   (:seq
                    ","
                    (:prec 1
                     (:seq (:alias (:pattern "[qQ][uU][iI][eE][tT]") "quiet") "=" _expression)))
                   :blank))
  assignment_statement (:prec-right -10
                        (:seq (:field :left _expression) "=" (:field :right _expression)))
  pointer_association_statement (:prec-right 0 (:seq _expression "=>" _expression))
  subroutine_call (:prec 1
                   (:seq
                    (:alias (:pattern "[cC][aA][lL][lL]") "call")
                    (:field :subroutine _expression)
                    (:choice cuda_kernel_argument_list :blank)
                    (:choice argument_list :blank)))
  cuda_kernel_argument_list (:seq "<<<" (:seq _expression (:repeat (:seq "," _expression))) ">>>")
  keyword_statement (:choice
                     (:alias (:pattern "[cC][oO][nN][tT][iI][nN][uU][eE]") "continue")
                     (:seq
                      (:alias (:pattern "[cC][yY][cC][lL][eE]") "cycle")
                      (:choice identifier :blank))
                     (:seq
                      (:alias (:pattern "[eE][xX][iI][tT]") "exit")
                      (:choice identifier :blank))
                     (:seq
                      (:choice
                       (:seq
                        (:alias (:pattern "[gG][oO]") "go")
                        (:alias (:pattern "[tT][oO]") "to"))
                       (:alias (:pattern "[gG][oO][tT][oO]") "goto"))
                      (:choice
                       statement_label_reference
                       (:seq
                        "("
                        (:seq
                         statement_label_reference
                         (:repeat (:seq "," statement_label_reference)))
                        ")"
                        (:choice "," :blank)
                        _expression)
                       (:seq
                        _expression
                        (:choice "," :blank)
                        "("
                        (:seq
                         statement_label_reference
                         (:repeat (:seq "," statement_label_reference)))
                        ")")))
                     (:seq
                      (:alias (:pattern "[rR][eE][tT][uU][rR][nN]") "return")
                      (:choice _expression :blank)))
  include_statement (:prec 1
                     (:seq
                      (:alias (:pattern "[iI][nN][cC][lL][uU][dD][eE]") "include")
                      (:field :path (:alias string_literal filename))))
  data_statement (:seq
                  (:alias (:pattern "[dD][aA][tT][aA]") "data")
                  (:seq data_set (:repeat (:seq (:choice "," :blank) data_set))))
  data_set (:prec 1
            (:seq
             (:seq
              (:choice
               identifier
               implied_do_loop_expression
               call_expression
               derived_type_member_expression)
              (:repeat
               (:seq
                ","
                (:choice
                 identifier
                 implied_do_loop_expression
                 call_expression
                 derived_type_member_expression))))
             data_value))
  data_value (:seq
              "/"
              (:seq
               (:seq
                (:choice
                 (:prec 1 (:seq (:field :repeat (:choice number_literal identifier)) "*"))
                 :blank)
                (:choice
                 number_literal
                 complex_literal
                 string_literal
                 boolean_literal
                 (:alias _signed_literal unary_expression)
                 null_literal
                 identifier
                 call_expression))
               (:repeat
                (:seq
                 ","
                 (:seq
                  (:choice
                   (:prec 1 (:seq (:field :repeat (:choice number_literal identifier)) "*"))
                   :blank)
                  (:choice
                   number_literal
                   complex_literal
                   string_literal
                   boolean_literal
                   (:alias _signed_literal unary_expression)
                   null_literal
                   identifier
                   call_expression)))))
              "/")
  _signed_literal (:prec-right 55 (:seq (:choice "-" "+") number_literal))
  do_loop (:seq _do_stmt _end_of_statement (:repeat _statement) _end_do_loop)
  _do_stmt (:choice
            (:seq (:choice block_label_start_expression :blank) (:alias do_stmt_label do_statement))
            (:seq
             (:choice block_label_start_expression :blank)
             (:alias do_stmt_nonlabel do_statement)))
  do_stmt_label (:seq
                 (:alias (:pattern "[dD][oO]") "do")
                 (:field :do_label (:alias _do_label statement_label_reference))
                 (:choice _do_stmt_control :blank))
  do_stmt_nonlabel (:seq (:alias (:pattern "[dD][oO]") "do") (:choice _do_stmt_control :blank))
  _do_stmt_control (:seq
                    (:choice "," :blank)
                    (:choice while_statement loop_control_expression concurrent_statement))
  _end_do_loop (:choice
                end_do_label_loop_statement
                (:seq (:choice statement_label :blank) end_do_loop_statement))
  end_do_label_loop_statement (:choice
                               (:field :do_label do_label_virtual)
                               (:seq
                                (:field :do_label (:alias _do_label_continue statement_label))
                                (:choice
                                 _statements
                                 (:choice
                                  (:seq
                                   (:alias (:pattern "[eE][nN][dD]") "end")
                                   (:alias (:pattern "[dD][oO]") "do")
                                   (:choice _block_label :blank))
                                  (:seq
                                   (:alias (:pattern "[eE][nN][dD][dD][oO]") "enddo")
                                   (:choice _block_label :blank))
                                  (:alias (:pattern "[eE][nN][dD]") "end")))))
  end_do_loop_statement (:choice
                         (:seq
                          (:alias (:pattern "[eE][nN][dD]") "end")
                          (:alias (:pattern "[dD][oO]") "do")
                          (:choice _block_label :blank))
                         (:seq
                          (:alias (:pattern "[eE][nN][dD][dD][oO]") "enddo")
                          (:choice _block_label :blank))
                         (:alias (:pattern "[eE][nN][dD]") "end"))
  while_statement (:seq (:alias (:pattern "[wW][hH][iI][lL][eE]") "while") parenthesized_expression)
  concurrent_statement (:seq concurrent_header (:repeat concurrent_locality))
  concurrent_header (:seq
                     (:alias (:pattern "[cC][oO][nN][cC][uU][rR][rR][eE][nN][tT]") "concurrent")
                     "("
                     (:choice (:seq (:field :type intrinsic_type) "::") :blank)
                     (:seq concurrent_control (:repeat (:seq "," concurrent_control)))
                     (:choice (:seq "," (:alias _expression concurrent_mask)) :blank)
                     ")")
  concurrent_control (:seq
                      identifier
                      "="
                      (:field :initial _expression)
                      ":"
                      (:field :final _expression)
                      (:choice (:seq ":" (:field :step _expression)) :blank))
  concurrent_locality (:choice
                       (:seq
                        (:choice
                         (:alias (:pattern "[lL][oO][cC][aA][lL]") "local")
                         (:alias (:pattern "[lL][oO][cC][aA][lL]_[iI][nN][iI][tT]") "local_init")
                         (:alias (:pattern "[sS][hH][aA][rR][eE][dD]") "shared"))
                        "("
                        (:seq identifier (:repeat (:seq "," identifier)))
                        ")")
                       (:seq
                        (:alias (:pattern "[dD][eE][fF][aA][uU][lL][tT]") "default")
                        "("
                        (:alias (:pattern "[nN][oO][nN][eE]") "none")
                        ")")
                       (:seq
                        (:alias (:pattern "[rR][eE][dD][uU][cC][eE]") "reduce")
                        "("
                        binary_op
                        ":"
                        (:seq identifier (:repeat (:seq "," identifier)))
                        ")"))
  binary_op (:choice "+" "*" (:pattern "(\\.\\w+\\.|\\w+)"))
  if_statement (:choice _inline_if_statement _block_if_statement)
  _inline_if_statement (:seq
                        (:alias (:pattern "[iI][fF]") "if")
                        parenthesized_expression
                        _statements)
  arithmetic_if_statement (:prec-right 0
                           (:seq
                            (:alias (:pattern "[iI][fF]") "if")
                            parenthesized_expression
                            statement_label_reference
                            ","
                            statement_label_reference
                            ","
                            statement_label_reference))
  _block_if_statement (:seq
                       (:choice block_label_start_expression :blank)
                       (:alias (:pattern "[iI][fF]") "if")
                       parenthesized_expression
                       (:alias (:pattern "[tT][hH][eE][nN]") "then")
                       (:choice _block_label :blank)
                       _end_of_statement
                       (:repeat _statement)
                       (:repeat elseif_clause)
                       (:choice else_clause :blank)
                       (:choice statement_label :blank)
                       end_if_statement)
  end_if_statement (:choice
                    (:seq
                     (:alias (:pattern "[eE][nN][dD]") "end")
                     (:alias (:pattern "[iI][fF]") "if")
                     (:choice _block_label :blank))
                    (:seq
                     (:alias (:pattern "[eE][nN][dD][iI][fF]") "endif")
                     (:choice _block_label :blank))
                    (:alias (:pattern "[eE][nN][dD]") "end"))
  elseif_clause (:seq
                 (:choice
                  (:seq
                   (:alias (:pattern "[eE][lL][sS][eE]") "else")
                   (:alias (:pattern "[iI][fF]") "if"))
                  (:alias (:pattern "[eE][lL][sS][eE][iI][fF]") "elseif"))
                 parenthesized_expression
                 (:alias (:pattern "[tT][hH][eE][nN]") "then")
                 (:choice _block_label :blank)
                 _end_of_statement
                 (:repeat _statement))
  else_clause (:seq
               (:alias (:pattern "[eE][lL][sS][eE]") "else")
               (:choice _block_label :blank)
               _end_of_statement
               (:repeat _statement))
  where_statement (:choice _inline_where_statement _block_where_statement)
  _inline_where_statement (:prec-right 0
                           (:seq
                            (:alias (:pattern "[wW][hH][eE][rR][eE]") "where")
                            parenthesized_expression
                            _statements))
  _block_where_statement (:seq
                          (:choice block_label_start_expression :blank)
                          (:alias (:pattern "[wW][hH][eE][rR][eE]") "where")
                          parenthesized_expression
                          _end_of_statement
                          (:repeat _statement)
                          (:repeat elsewhere_clause)
                          end_where_statement)
  end_where_statement (:choice
                       (:seq
                        (:alias (:pattern "[eE][nN][dD]") "end")
                        (:alias (:pattern "[wW][hH][eE][rR][eE]") "where")
                        (:choice _block_label :blank))
                       (:seq
                        (:alias (:pattern "[eE][nN][dD][wW][hH][eE][rR][eE]") "endwhere")
                        (:choice _block_label :blank))
                       (:alias (:pattern "[eE][nN][dD]") "end"))
  elsewhere_clause (:seq
                    (:choice
                     (:seq
                      (:alias (:pattern "[eE][lL][sS][eE]") "else")
                      (:alias (:pattern "[wW][hH][eE][rR][eE]") "where"))
                     (:alias (:pattern "[eE][lL][sS][eE][wW][hH][eE][rR][eE]") "elsewhere"))
                    (:choice parenthesized_expression :blank)
                    (:choice _block_label :blank)
                    _end_of_statement
                    (:repeat _statement))
  forall_statement (:choice _inline_forall_statement _block_forall_statement)
  triplet_spec (:seq
                identifier
                "="
                _expression
                ":"
                _expression
                (:choice (:seq ":" _expression) :blank))
  _forall_control_expression (:seq
                              (:alias (:pattern "[fF][oO][rR][aA][lL][lL]") "forall")
                              "("
                              (:seq triplet_spec (:repeat (:seq "," triplet_spec)))
                              (:choice
                               (:seq "," (:choice logical_expression relational_expression))
                               :blank)
                              ")")
  _inline_forall_statement (:seq _forall_control_expression _statements)
  _block_forall_statement (:seq
                           (:choice block_label_start_expression :blank)
                           _forall_control_expression
                           _end_of_statement
                           (:repeat _statement)
                           (:choice statement_label :blank)
                           end_forall_statement)
  end_forall_statement (:choice
                        (:seq
                         (:alias (:pattern "[eE][nN][dD]") "end")
                         (:alias (:pattern "[fF][oO][rR][aA][lL][lL]") "forall")
                         (:choice _block_label :blank))
                        (:seq
                         (:alias (:pattern "[eE][nN][dD][fF][oO][rR][aA][lL][lL]") "endforall")
                         (:choice _block_label :blank))
                        (:alias (:pattern "[eE][nN][dD]") "end"))
  select_case_statement (:seq
                         (:choice block_label_start_expression :blank)
                         (:choice
                          (:seq
                           (:alias (:pattern "[sS][eE][lL][eE][cC][tT]") "select")
                           (:alias (:pattern "[cC][aA][sS][eE]") "case"))
                          (:alias
                           (:pattern "[sS][eE][lL][eE][cC][tT][cC][aA][sS][eE]")
                           "selectcase"))
                         selector
                         _end_of_statement
                         (:repeat
                          (:choice
                           case_statement
                           preproc_include
                           preproc_def
                           preproc_function_def
                           preproc_call
                           (:alias preproc_if_in_select_case preproc_if)
                           (:alias preproc_ifdef_in_select_case preproc_ifdef)))
                         (:choice statement_label :blank)
                         end_select_statement)
  select_type_statement (:seq
                         (:choice block_label_start_expression :blank)
                         (:choice
                          (:seq
                           (:alias (:pattern "[sS][eE][lL][eE][cC][tT]") "select")
                           (:alias (:pattern "[tT][yY][pP][eE]") "type"))
                          (:alias
                           (:pattern "[sS][eE][lL][eE][cC][tT][tT][yY][pP][eE]")
                           "selecttype"))
                         selector
                         _end_of_statement
                         (:repeat
                          (:choice
                           type_statement
                           preproc_include
                           preproc_def
                           preproc_function_def
                           preproc_call
                           (:alias preproc_if_in_select_type preproc_if)
                           (:alias preproc_ifdef_in_select_type preproc_ifdef)))
                         (:choice statement_label :blank)
                         end_select_statement)
  select_rank_statement (:seq
                         (:choice block_label_start_expression :blank)
                         (:choice
                          (:seq
                           (:alias (:pattern "[sS][eE][lL][eE][cC][tT]") "select")
                           (:alias (:pattern "[rR][aA][nN][kK]") "rank"))
                          (:alias
                           (:pattern "[sS][eE][lL][eE][cC][tT][rR][aA][nN][kK]")
                           "selectrank"))
                         selector
                         _end_of_statement
                         (:repeat
                          (:choice
                           rank_statement
                           preproc_include
                           preproc_def
                           preproc_function_def
                           preproc_call
                           (:alias preproc_if_in_select_rank preproc_if)
                           (:alias preproc_ifdef_in_select_rank preproc_ifdef)))
                         (:choice statement_label :blank)
                         end_select_statement)
  end_select_statement (:choice
                        (:seq
                         (:alias (:pattern "[eE][nN][dD]") "end")
                         (:alias (:pattern "[sS][eE][lL][eE][cC][tT]") "select")
                         (:choice _block_label :blank))
                        (:seq
                         (:alias (:pattern "[eE][nN][dD][sS][eE][lL][eE][cC][tT]") "endselect")
                         (:choice _block_label :blank))
                        (:alias (:pattern "[eE][nN][dD]") "end"))
  selector (:seq "(" (:choice _expression pointer_association_statement) ")")
  case_statement (:seq
                  (:alias (:pattern "[cC][aA][sS][eE]") "case")
                  (:choice
                   (:seq "(" case_value_range_list ")")
                   (:alias (:pattern "[dD][eE][fF][aA][uU][lL][tT]") default))
                  (:choice _block_label :blank)
                  _end_of_statement
                  (:repeat _statement))
  type_statement (:seq
                  (:choice
                   (:seq
                    (:choice
                     (:choice
                      (:seq
                       (:alias (:pattern "[tT][yY][pP][eE]") "type")
                       (:alias (:pattern "[iI][sS]") "is"))
                      (:alias (:pattern "[tT][yY][pP][eE][iI][sS]") "typeis"))
                     (:choice
                      (:seq
                       (:alias (:pattern "[cC][lL][aA][sS][sS]") "class")
                       (:alias (:pattern "[iI][sS]") "is"))
                      (:alias (:pattern "[cC][lL][aA][sS][sS][iI][sS]") "classis")))
                    (:choice (:seq "(" (:field :type (:choice intrinsic_type identifier)) ")")))
                   (:seq
                    (:alias (:pattern "[cC][lL][aA][sS][sS]") "class")
                    (:alias (:pattern "[dD][eE][fF][aA][uU][lL][tT]") default)))
                  (:choice _block_label :blank)
                  _end_of_statement
                  (:repeat _statement))
  case_value_range_list (:seq
                         (:choice _expression extent_specifier)
                         (:repeat (:seq "," (:choice _expression extent_specifier))))
  rank_statement (:prec 2
                  (:seq
                   (:alias (:pattern "[rR][aA][nN][kK]") "rank")
                   (:choice
                    (:seq "(" case_value_range_list ")")
                    (:seq "(" assumed_size ")")
                    (:alias (:pattern "[dD][eE][fF][aA][uU][lL][tT]") default))
                   (:choice _block_label :blank)
                   _end_of_statement
                   (:repeat _statement)))
  block_construct (:seq
                   (:choice block_label_start_expression :blank)
                   (:alias (:pattern "[bB][lL][oO][cC][kK]") "block")
                   _end_of_statement
                   (:repeat _specification_part)
                   (:repeat _statement)
                   end_block_construct_statement)
  end_block_construct_statement (:choice
                                 (:seq
                                  (:alias (:pattern "[eE][nN][dD]") "end")
                                  (:alias (:pattern "[bB][lL][oO][cC][kK]") "block")
                                  (:choice _block_label :blank))
                                 (:seq
                                  (:alias (:pattern "[eE][nN][dD][bB][lL][oO][cC][kK]") "endblock")
                                  (:choice _block_label :blank))
                                 (:alias (:pattern "[eE][nN][dD]") "end"))
  associate_statement (:seq
                       (:choice block_label_start_expression :blank)
                       (:alias (:pattern "[aA][sS][sS][oO][cC][iI][aA][tT][eE]") "associate")
                       association_list
                       _end_of_statement
                       (:repeat _statement)
                       end_associate_statement)
  association_list (:seq "(" (:seq association (:repeat (:seq "," association))) ")")
  association (:seq (:field :name identifier) "=>" (:field :selector _expression))
  end_associate_statement (:choice
                           (:seq
                            (:alias (:pattern "[eE][nN][dD]") "end")
                            (:alias (:pattern "[aA][sS][sS][oO][cC][iI][aA][tT][eE]") "associate")
                            (:choice _block_label :blank))
                           (:seq
                            (:alias
                             (:pattern "[eE][nN][dD][aA][sS][sS][oO][cC][iI][aA][tT][eE]")
                             "endassociate")
                            (:choice _block_label :blank))
                           (:alias (:pattern "[eE][nN][dD]") "end"))
  format_statement (:prec-dynamic 80
                    (:seq
                     (:alias (:pattern "[fF][oO][rR][mM][aA][tT]") "format")
                     "("
                     (:alias _transfer_items transfer_items)
                     ")"))
  _transfer_item (:choice
                  string_literal
                  edit_descriptor
                  hollerith_constant
                  (:seq "(" _transfer_items ")"))
  _transfer_items (:seq _transfer_item (:repeat (:seq (:choice "," :blank) _transfer_item)))
  edit_descriptor (:choice "/" (:pattern "[a-gi-zA-GI-Z0-9:.*$]+"))
  _io_arguments (:seq
                 "("
                 (:choice
                  unit_identifier
                  (:seq unit_identifier "," format_identifier)
                  (:seq
                   unit_identifier
                   ","
                   format_identifier
                   ","
                   (:seq keyword_argument (:repeat (:seq "," keyword_argument))))
                  (:seq
                   unit_identifier
                   ","
                   (:seq keyword_argument (:repeat (:seq "," keyword_argument))))
                  (:seq keyword_argument (:repeat (:seq "," keyword_argument))))
                 ")")
  read_statement (:prec 1 (:choice _simple_read_statement _parameterized_read_statement))
  _simple_read_statement (:prec 1
                          (:seq
                           (:alias (:pattern "[rR][eE][aA][dD]") "read")
                           format_identifier
                           (:choice (:seq "," input_item_list) :blank)))
  _parameterized_read_statement (:prec 1
                                 (:seq
                                  (:alias (:pattern "[rR][eE][aA][dD]") "read")
                                  _io_arguments
                                  (:choice input_item_list :blank)))
  print_statement (:seq
                   (:alias (:pattern "[pP][rR][iI][nN][tT]") "print")
                   format_identifier
                   (:choice (:seq "," output_item_list) :blank))
  open_statement (:seq
                  (:alias (:pattern "[oO][pP][eE][nN]") "open")
                  _io_arguments
                  (:choice output_item_list :blank))
  close_statement (:prec 1
                   (:seq
                    (:alias (:pattern "[cC][lL][oO][sS][eE]") "close")
                    "("
                    (:choice
                     unit_identifier
                     (:seq
                      unit_identifier
                      ","
                      (:seq keyword_argument (:repeat (:seq "," keyword_argument))))
                     (:seq keyword_argument (:repeat (:seq "," keyword_argument))))
                    ")"))
  write_statement (:prec 1
                   (:seq
                    (:alias (:pattern "[wW][rR][iI][tT][eE]") "write")
                    _io_arguments
                    (:choice "," :blank)
                    (:choice output_item_list :blank)))
  inquire_statement (:prec 1
                     (:seq
                      (:alias (:pattern "[iI][nN][qQ][uU][iI][rR][eE]") "inquire")
                      _io_arguments
                      (:choice output_item_list :blank)))
  enum (:seq enum_statement (:repeat enumerator_statement) end_enum_statement)
  enum_statement (:seq
                  (:alias (:pattern "[eE][nN][uU][mM]") "enum")
                  ","
                  language_binding
                  _end_of_statement)
  enumeration_type (:seq
                    enumeration_type_statement
                    (:repeat enumerator_statement)
                    end_enumeration_type_statement)
  enumeration_type_statement (:seq
                              (:alias
                               (:pattern "[eE][nN][uU][mM][eE][rR][aA][tT][iI][oO][nN]")
                               "enumeration")
                              (:alias (:pattern "[tT][yY][pP][eE]") "type")
                              (:choice (:seq "," access_specifier) :blank)
                              (:choice "::" :blank)
                              _type_name)
  enumerator_statement (:seq
                        (:alias (:pattern "[eE][nN][uU][mM][eE][rR][aA][tT][oO][rR]") "enumerator")
                        (:choice "::" :blank)
                        (:seq
                         (:field :declarator
                          (:choice identifier (:alias _declaration_assignment init_declarator)))
                         (:repeat
                          (:seq
                           ","
                           (:field :declarator
                            (:choice identifier (:alias _declaration_assignment init_declarator)))))))
  end_enum_statement (:choice
                      (:seq
                       (:alias (:pattern "[eE][nN][dD]") "end")
                       (:alias (:pattern "[eE][nN][uU][mM]") "enum"))
                      (:alias (:pattern "[eE][nN][dD][eE][nN][uU][mM]") "endenum")
                      (:alias (:pattern "[eE][nN][dD]") "end"))
  end_enumeration_type_statement (:choice
                                  (:seq
                                   (:alias (:pattern "[eE][nN][dD]") "end")
                                   (:alias
                                    (:pattern "[eE][nN][uU][mM][eE][rR][aA][tT][iI][oO][nN]")
                                    "enumeration")
                                   (:alias (:pattern "[tT][yY][pP][eE]") "type")
                                   (:choice _name :blank))
                                  (:seq
                                   (:alias (:pattern "[eE][nN][dD]") "end")
                                   (:alias
                                    (:pattern "[eE][nN][uU][mM][eE][rR][aA][tT][iI][oO][nN]")
                                    "enumeration"))
                                  (:alias
                                   (:pattern "[eE][nN][dD][eE][nN][uU][mM][eE][rR][aA][tT][iI][oO][nN]")
                                   "endenumeration")
                                  (:alias (:pattern "[eE][nN][dD]") "end"))
  unit_identifier (:seq
                   (:choice (:seq (:alias (:pattern "[uU][nN][iI][tT]") "unit") "=") :blank)
                   (:prec 1 (:choice number_literal _io_expressions)))
  format_identifier (:seq
                     (:choice (:seq (:alias (:pattern "[fF][mM][tT]") "fmt") "=") :blank)
                     (:choice statement_label_reference _io_expressions))
  _file_position_spec (:choice
                       unit_identifier
                       (:seq
                        "("
                        unit_identifier
                        ","
                        (:seq keyword_argument (:repeat (:seq "," keyword_argument)))
                        ")")
                       (:seq "(" (:seq keyword_argument (:repeat (:seq "," keyword_argument))) ")"))
  file_position_statement (:choice
                           (:seq
                            (:choice
                             (:alias (:pattern "[bB][aA][cC][kK][sS][pP][aA][cC][eE]") "backspace")
                             (:alias (:pattern "[eE][nN][dD][fF][iI][lL][eE]") "endfile")
                             (:alias (:pattern "[fF][lL][uU][sS][hH]") "flush")
                             (:alias (:pattern "[rR][eE][wW][iI][nN][dD]") "rewind")
                             (:alias (:pattern "[wW][aA][iI][tT]") "wait"))
                            _file_position_spec)
                           (:seq
                            (:alias (:pattern "[pP][aA][uU][sS][eE]") "pause")
                            (:choice string_literal :blank)))
  _io_expressions (:prec 1
                   (:choice
                    "*"
                    string_literal
                    identifier
                    derived_type_member_expression
                    concatenation_expression
                    math_expression
                    parenthesized_expression
                    call_expression))
  input_item_list (:prec-right 0 (:seq _expression (:repeat (:seq "," _expression))))
  output_item_list (:prec-right 0 (:seq _expression (:repeat (:seq "," _expression))))
  allocate_statement (:seq
                      (:alias (:pattern "[aA][lL][lL][oO][cC][aA][tT][eE]") "allocate")
                      "("
                      (:choice
                       (:field :type (:seq (:choice intrinsic_type identifier) "::"))
                       :blank)
                      (:seq
                       (:field :allocation
                        (:choice
                         identifier
                         derived_type_member_expression
                         sized_allocation
                         coarray_allocation))
                       (:repeat
                        (:seq
                         ","
                         (:field :allocation
                          (:choice
                           identifier
                           derived_type_member_expression
                           sized_allocation
                           coarray_allocation)))))
                      (:choice
                       (:seq "," (:seq keyword_argument (:repeat (:seq "," keyword_argument))))
                       :blank)
                      ")")
  sized_allocation (:prec-right 1 (:seq _expression (:alias argument_list size)))
  coarray_allocation (:seq
                      (:choice _expression sized_allocation)
                      (:alias coarray_index coarray_size))
  deallocate_statement (:seq
                        (:alias (:pattern "[dD][eE][aA][lL][lL][oO][cC][aA][tT][eE]") "deallocate")
                        "("
                        (:seq
                         (:choice identifier derived_type_member_expression)
                         (:repeat (:seq "," (:choice identifier derived_type_member_expression))))
                        (:choice
                         (:seq "," (:seq keyword_argument (:repeat (:seq "," keyword_argument))))
                         :blank)
                        ")")
  nullify_statement (:seq
                     (:alias (:pattern "[nN][uU][lL][lL][iI][fF][yY]") "nullify")
                     "("
                     (:seq
                      (:choice identifier derived_type_member_expression)
                      (:repeat (:seq "," (:choice identifier derived_type_member_expression))))
                     ")")
  entry_statement (:seq
                   (:alias (:pattern "[eE][nN][tT][rR][yY]") "entry")
                   (:field :name _name)
                   (:choice (:field :parameters _parameters) :blank)
                   (:choice (:repeat (:choice language_binding function_result)) :blank))
  assign_statement (:seq
                    (:alias (:pattern "[aA][sS][sS][iI][gG][nN]") "assign")
                    number_literal
                    (:alias (:pattern "[tT][oO]") "to")
                    identifier)
  _expression (:choice
               number_literal
               complex_literal
               string_literal
               boolean_literal
               array_literal
               null_literal
               identifier
               derived_type_member_expression
               logical_expression
               relational_expression
               concatenation_expression
               math_expression
               unary_expression
               parenthesized_expression
               call_expression
               implied_do_loop_expression
               coarray_expression
               conditional_expression)
  parenthesized_expression (:seq "(" _expression ")")
  derived_type_member_expression (:prec-right 100
                                  (:seq _expression "%" (:alias identifier type_member)))
  logical_expression (:choice
                      (:prec-left 10
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:alias (:pattern "\\.[oO][rR]\\.") ".or."))
                        (:field :right _expression)))
                      (:prec-left 20
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:alias (:pattern "\\.[aA][nN][dD]\\.") ".and."))
                        (:field :right _expression)))
                      (:prec-left 5
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:alias (:pattern "\\.[eE][qQ][vV]\\.") ".eqv."))
                        (:field :right _expression)))
                      (:prec-left 5
                       (:seq
                        (:field :left _expression)
                        (:field :operator (:alias (:pattern "\\.[nN][eE][qQ][vV]\\.") ".neqv."))
                        (:field :right _expression)))
                      (:prec-left 30
                       (:seq
                        (:field :operator (:alias (:pattern "\\.[nN][oO][tT]\\.") ".not."))
                        (:field :argument _expression))))
  relational_expression (:choice
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator "<")
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator (:alias (:pattern "\\.[lL][tT]\\.") ".lt."))
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator ">")
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator (:alias (:pattern "\\.[gG][tT]\\.") ".gt."))
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator "<=")
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator (:alias (:pattern "\\.[lL][eE]\\.") ".le."))
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator ">=")
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator (:alias (:pattern "\\.[gG][eE]\\.") ".ge."))
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator "==")
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator (:alias (:pattern "\\.[eE][qQ]\\.") ".eq."))
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator "/=")
                           (:field :right _expression)))
                         (:prec-left 40
                          (:seq
                           (:field :left _expression)
                           (:field :operator (:alias (:pattern "\\.[nN][eE]\\.") ".ne."))
                           (:field :right _expression))))
  concatenation_expression (:prec-right 50
                            (:seq
                             (:field :left _expression)
                             (:field :operator "//")
                             (:field :right _expression)))
  math_expression (:choice
                   (:prec-left 50
                    (:seq
                     (:field :left _expression)
                     (:field :operator "+")
                     (:field :right _expression)))
                   (:prec-left 50
                    (:seq
                     (:field :left _expression)
                     (:field :operator "-")
                     (:field :right _expression)))
                   (:prec-left 60
                    (:seq
                     (:field :left _expression)
                     (:field :operator "*")
                     (:field :right _expression)))
                   (:prec-left 60
                    (:seq
                     (:field :left _expression)
                     (:field :operator "/")
                     (:field :right _expression)))
                   (:prec-left 70
                    (:seq
                     (:field :left _expression)
                     (:field :operator "**")
                     (:field :right _expression)))
                   (:prec-left 2
                    (:seq
                     (:field :left _expression)
                     (:field :operator user_defined_operator)
                     (:field :right _expression))))
  unary_expression (:prec-right 55
                    (:seq
                     (:field :operator (:choice "-" "+" user_defined_operator))
                     (:field :argument _expression)))
  user_defined_operator (:prec-right 0 (:seq "." (:pattern "[a-zA-Z]+") "."))
  call_expression (:prec 80 (:seq _expression (:repeat1 argument_list)))
  implied_do_loop_expression (:seq
                              "("
                              (:seq _expression (:repeat (:seq "," _expression)))
                              ","
                              (:choice (:seq (:field :type intrinsic_type) "::") :blank)
                              loop_control_expression
                              ")")
  _argument_list (:prec-dynamic 1
                  (:seq
                   "("
                   (:choice
                    (:seq
                     (:choice _expression _argument_item)
                     (:repeat (:seq "," (:choice _expression _argument_item))))
                    :blank)
                   ")"))
  _argument_item (:choice
                  keyword_argument
                  extent_specifier
                  assumed_size
                  assumed_rank
                  multiple_subscript
                  multiple_subscript_triplet)
  argument_list _argument_list
  keyword_argument (:prec 1
                    (:seq
                     (:field :name identifier)
                     "="
                     (:field :value (:choice _expression assumed_size assumed_shape))))
  _extent_specifier (:seq
                     (:choice _expression :blank)
                     ":"
                     (:choice (:choice _expression assumed_size) :blank)
                     (:choice (:seq ":" _expression) :blank))
  extent_specifier _extent_specifier
  multiple_subscript (:seq "@" _expression)
  multiple_subscript_triplet (:seq "@" _extent_specifier)
  assumed_size "*"
  assumed_shape ":"
  assumed_rank ".."
  block_label_start_expression (:seq (:alias identifier "label") ":")
  _block_label (:alias identifier block_label)
  loop_control_expression (:seq
                           identifier
                           "="
                           _expression
                           ","
                           _expression
                           (:choice (:seq "," _expression) :blank))
  array_literal (:choice _array_constructor_legacy _array_constructor_f2003)
  _array_constructor_legacy (:seq "(/" _ac_value_list "/)")
  _array_constructor_f2003 (:seq "[" _ac_value_list "]")
  _type_spec (:seq (:choice intrinsic_type derived_type) "::")
  _ac_value_list (:choice
                  (:field :type _type_spec)
                  (:seq
                   (:choice (:field :type _type_spec) :blank)
                   (:seq _expression (:repeat (:seq "," _expression)))))
  complex_literal (:seq
                   "("
                   (:choice number_literal identifier unary_expression)
                   ","
                   (:choice number_literal identifier unary_expression)
                   ")")
  number_literal (:seq
                  (:choice _integer_literal _float_literal _boz_literal)
                  (:choice _kind :blank))
  boolean_literal (:seq
                   (:choice
                    (:alias (:pattern "\\.[tT][rR][uU][eE]\\.") ".true.")
                    (:alias (:pattern "\\.[fF][aA][lL][sS][eE]\\.") ".false."))
                   (:choice _kind :blank))
  _kind (:seq
         (:token-immediate "_")
         (:field :kind
          (:choice
           (:alias (:token-immediate (:pattern "[a-zA-Z]\\w*")) identifier)
           (:alias (:token-immediate (:pattern "\\d+")) number_literal))))
  null_literal (:prec 1
                (:seq
                 (:alias (:pattern "[nN][uU][lL][lL]") "null")
                 "("
                 (:choice (:field :mold (:choice identifier derived_type_member_expression)) :blank)
                 ")"))
  string_literal (:seq
                  (:choice
                   (:seq
                    (:field :kind
                     (:choice
                      (:alias _string_literal_kind identifier)
                      (:alias _integer_literal number_literal)))
                    (:token-immediate "_"))
                   :blank)
                  _string_literal)
  coarray_index (:seq
                 "["
                 (:seq
                  (:choice _expression (:alias _coarray_extent_specifier extent_specifier) "*")
                  (:repeat
                   (:seq
                    ","
                    (:choice _expression (:alias _coarray_extent_specifier extent_specifier) "*"))))
                 (:choice
                  (:seq
                   (:seq "," keyword_argument)
                   (:repeat (:seq "," (:seq "," keyword_argument))))
                  :blank)
                 "]")
  _coarray_extent_specifier (:seq
                             (:choice _expression :blank)
                             ":"
                             (:choice (:choice _expression "*") :blank)
                             (:choice (:seq ":" _expression) :blank))
  coarray_declarator (:prec-right 0
                      (:seq
                       (:choice identifier sized_declarator)
                       (:alias coarray_index coarray_size)))
  coarray_expression (:prec-right 0 (:seq _expression coarray_index))
  coarray_statement (:choice
                     (:seq
                      (:choice
                       (:alias (:pattern "[sS][yY][nN][cC]") "sync")
                       (:alias (:pattern "[fF][oO][rR][mM]") "form"))
                      (:choice
                       (:alias (:pattern "[aA][lL][lL]") "all")
                       (:alias (:pattern "[iI][mM][aA][gG][eE][sS]") "images")
                       (:alias (:pattern "[mM][eE][mM][oO][rR][yY]") "memory")
                       (:alias (:pattern "[tT][eE][aA][mM]") "team"))
                      (:choice argument_list :blank))
                     (:seq
                      (:alias (:pattern "[fF][aA][iI][lL]") "fail")
                      (:alias (:pattern "[iI][mM][aA][gG][eE]") "image"))
                     (:seq
                      (:alias (:pattern "[eE][vV][eE][nN][tT]") "event")
                      (:choice
                       (:alias (:pattern "[pP][oO][sS][tT]") "post")
                       (:alias (:pattern "[wW][aA][iI][tT]") "wait"))
                      argument_list)
                     (:prec-right 1
                      (:seq
                       (:choice
                        (:alias (:pattern "[lL][oO][cC][kK]") "lock")
                        (:alias (:pattern "[uU][nN][lL][oO][cC][kK]") "unlock"))
                       argument_list))
                     (:seq
                      (:alias (:pattern "[nN][oO][tT][iI][fF][yY]") "notify")
                      (:alias (:pattern "[wW][aA][iI][tT]") "wait")
                      argument_list))
  coarray_team_statement (:seq
                          (:choice block_label_start_expression :blank)
                          (:alias (:pattern "[cC][hH][aA][nN][gG][eE]") "change")
                          (:alias (:pattern "[tT][eE][aA][mM]") "team")
                          argument_list
                          _end_of_statement
                          (:repeat _statement)
                          end_coarray_team_statement)
  end_coarray_team_statement (:prec 2
                              (:seq
                               (:choice
                                (:seq
                                 (:alias (:pattern "[eE][nN][dD]") "end")
                                 (:alias (:pattern "[tT][eE][aA][mM]") "team"))
                                (:alias (:pattern "[eE][nN][dD][tT][eE][aA][mM]") "endteam")
                                (:alias (:pattern "[eE][nN][dD]") "end"))
                               (:choice argument_list :blank)
                               (:choice _block_label :blank)))
  coarray_critical_statement (:seq
                              (:choice block_label_start_expression :blank)
                              (:alias (:pattern "[cC][rR][iI][tT][iI][cC][aA][lL]") "critical")
                              (:choice argument_list :blank)
                              _end_of_statement
                              (:repeat _statement)
                              end_coarray_critical_statement)
  end_coarray_critical_statement (:choice
                                  (:seq
                                   (:alias (:pattern "[eE][nN][dD]") "end")
                                   (:alias (:pattern "[cC][rR][iI][tT][iI][cC][aA][lL]") "critical")
                                   (:choice _block_label :blank))
                                  (:seq
                                   (:alias
                                    (:pattern "[eE][nN][dD][cC][rR][iI][tT][iI][cC][aA][lL]")
                                    "endcritical")
                                   (:choice _block_label :blank))
                                  (:alias (:pattern "[eE][nN][dD]") "end"))
  conditional_expression (:seq
                          (:field :condition _expression)
                          "?"
                          (:field :consequence (:choice (:prec-left 0 _expression) nil_literal))
                          ":"
                          (:field :alternative (:choice (:prec-left 0 _expression) nil_literal)))
  nil_literal (:alias (:pattern "\\.[nN][iI][lL]\\.") ".nil.")
  identifier (:choice
              (:pattern "[a-zA-Z_$][\\w$]*")
              (:alias (:pattern "[aA][lL][lL][oO][cC][aA][tT][aA][bB][lL][eE]") "allocatable")
              (:alias (:pattern "[aA][sS][yY][nN][cC][hH][rR][oO][nN][oO][uU][sS]") "asynchronous")
              (:alias (:pattern "[aA][uU][tT][oO][mM][aA][tT][iI][cC]") "automatic")
              (:alias (:pattern "[bB][lL][oO][cC][kK]") "block")
              (:alias (:pattern "[bB][yY][tT][eE]") "byte")
              (:prec -1 (:alias (:pattern "[cC][aA][lL][lL]") "call"))
              (:alias (:pattern "[cC][hH][aA][nN][gG][eE]") "change")
              (:alias (:pattern "[cC][oO][nN][sS][tT][aA][nN][tT]") "constant")
              (:alias (:pattern "[cC][oO][nN][tT][iI][gG][uU][oO][uU][sS]") "contiguous")
              (:alias (:pattern "[cC][rR][iI][tT][iI][cC][aA][lL]") "critical")
              (:alias (:pattern "[cC][yY][cC][lL][eE]") "cycle")
              (:alias (:pattern "[dD][aA][tT][aA]") "data")
              (:alias (:pattern "[dD][eE][vV][iI][cC][eE]") "device")
              (:prec -1 (:alias (:pattern "[dD][iI][mM][eE][nN][sS][iI][oO][nN]") "dimension"))
              (:alias (:pattern "[dD][oO]") "do")
              (:alias (:pattern "[dD][oO][uU][bB][lL][eE]") "double")
              (:alias (:pattern "[eE][lL][sS][eE]") "else")
              (:alias (:pattern "[eE][lL][sS][eE][iI][fF]") "elseif")
              (:alias (:pattern "[eE][nN][dD]") "end")
              (:alias (:pattern "[eE][nN][dD][iI][fF]") "endif")
              (:alias (:pattern "[eE][nN][tT][rR][yY]") "entry")
              (:alias (:pattern "[eE][rR][rR][oO][rR]") "error")
              (:alias (:pattern "[eE][vV][eE][nN][tT]") "event")
              (:alias (:pattern "[eE][xX][iI][tT]") "exit")
              (:alias (:pattern "[eE][xX][tT][eE][rR][nN][aA][lL]") "external")
              (:alias (:pattern "[fF][aA][iI][lL]") "fail")
              (:prec -1 (:alias (:pattern "[fF][lL][uU][sS][hH]") "flush"))
              (:alias (:pattern "[fF][mM][tT]") "fmt")
              (:alias (:pattern "[fF][oO][rR][mM]") "form")
              (:alias (:pattern "[fF][oO][rR][mM][aA][tT]") "format")
              (:alias (:pattern "[gG][oO]") "go")
              (:alias (:pattern "[iI][fF]") "if")
              (:prec -1 (:alias (:pattern "[iI][nN][cC][lL][uU][dD][eE]") "include"))
              (:alias (:pattern "[iI][nN][qQ][uU][iI][rR][eE]") "inquire")
              (:alias (:pattern "[iI][nN][tT][rR][iI][nN][sS][iI][cC]") "intrinsic")
              (:alias (:pattern "[kK][iI][nN][dD]") "kind")
              (:alias (:pattern "[lL][eE][nN]") "len")
              (:alias (:pattern "[lL][oO][cC][kK]") "lock")
              (:alias (:pattern "[mM][oO][dD][uU][lL][eE]") "module")
              (:alias (:pattern "[nN][uU][lL][lL]") "null")
              (:prec -1 (:alias (:pattern "[oO][pP][eE][nN]") "open"))
              (:alias (:pattern "[oO][pP][tT][iI][oO][nN][aA][lL]") "optional")
              (:alias (:pattern "[pP][aA][rR][aA][mM][eE][tT][eE][rR]") "parameter")
              (:alias (:pattern "[pP][oO][iI][nN][tT][eE][rR]") "pointer")
              (:prec -1 (:alias (:pattern "[pP][rR][iI][nN][tT]") "print"))
              (:alias (:pattern "[pP][rR][iI][vV][aA][tT][eE]") "private")
              (:alias (:pattern "[pP][uU][bB][lL][iI][cC]") "public")
              (:prec -1 (:alias (:pattern "[rR][aA][nN][kK]") "rank"))
              (:alias (:pattern "[rR][eE][aA][dD]") "read")
              (:alias (:pattern "[rR][eE][aA][lL]") "real")
              (:alias (:pattern "[sS][aA][vV][eE]") "save")
              (:alias (:pattern "[sS][eE][lL][eE][cC][tT]") "select")
              (:alias (:pattern "[sS][eE][qQ][uU][eE][nN][cC][eE]") "sequence")
              (:alias (:pattern "[sS][hH][aA][rR][eE][dD]") "shared")
              (:alias (:pattern "[sS][tT][aA][tT][iI][cC]") "static")
              (:alias (:pattern "[sS][tT][oO][pP]") "stop")
              (:alias (:pattern "[sS][yY][nN][cC]") "sync")
              (:alias (:pattern "[tT][aA][rR][gG][eE][tT]") "target")
              (:alias (:pattern "[tT][eE][xX][tT][uU][rR][eE]") "texture")
              (:prec -1 (:alias (:pattern "[tT][yY][pP][eE]") "type"))
              (:alias (:pattern "[uU][nN][iI][tT]") "unit")
              (:alias (:pattern "[uU][nN][lL][oO][cC][kK]") "unlock")
              (:alias (:pattern "[vV][aA][lL][uU][eE]") "value")
              (:prec -1 (:alias (:pattern "[wW][aA][iI][tT]") "wait"))
              (:prec -1 (:alias (:pattern "[wW][hH][eE][rR][eE]") "where"))
              (:alias (:pattern "[wW][rR][iI][tT][eE]") "write"))
  comment (:token (:seq "!" (:pattern ".*")))
  custom_directive (:token (:prec -1 (:seq "@" (:pattern ".*"))))
  _end_of_statement (:choice ";" _external_end_of_statement)
  _newline "\n"}}
