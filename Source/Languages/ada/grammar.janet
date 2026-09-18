# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "ada"
 :word identifier
 :extras [(:pattern "\\s|\\\\\\r?\\n") comment]
 :conflicts [[null_procedure_declaration _subprogram_specification]
             [expression_function_declaration _subprogram_specification]
             [at_clause _name]
             [slice _discrete_range]
             [record_component_association_list positional_array_aggregate]
             [value_sequence array_component_association]
             [generic_instantiation procedure_specification]
             [_defining_identifier_list object_renaming_declaration exception_renaming_declaration]
             [_defining_identifier_list object_renaming_declaration]
             [_defining_identifier_list
              object_renaming_declaration
              loop_label
              exception_renaming_declaration]
             [_defining_identifier_list _name]
             [generic_formal_part generic_renaming_declaration]
             [derived_type_definition]
             [full_type_declaration _discriminant_part]
             [private_extension_declaration derived_type_definition]
             [formal_derived_type_definition]
             [_name _aspect_mark]
             [_name iterator_procedure_call]
             [_name package_body_stub]
             [_name _subtype_indication]
             [_name _subtype_indication component_choice_list]
             [_name _subtype_mark]
             [attribute_definition_clause _attribute_reference]
             [component_choice_list discrete_choice]
             [component_choice_list positional_array_aggregate]
             [discriminant_association _parenthesized_expression]]
 :precedences []
 :externals []
 :inline [_name_not_function_call _name_for_component_choice]
 :supertypes []
 :rules
 {compilation (:repeat compilation_unit)
  identifier (:pattern "[a-zA-Z\\u{80}-\\u{10FFFF}][0-9a-zA-Z_\\u{80}-\\u{10FFFF}]*" "u")
  gnatprep_identifier (:pattern "\\$[a-zA-Z\\u{80}-\\u{10FFFF}][0-9a-zA-Z_\\u{80}-\\u{10FFFF}]*" "u")
  comment (:token (:seq "--" (:pattern ".*")))
  string_literal (:token (:pattern "\"(\"\"|[^\"])*\""))
  character_literal (:token (:pattern "'.'"))
  numeric_literal (:token
                   (:choice
                    (:pattern "[0-9][0-9_]*(\\.[0-9_]+)?([eE][+-]?[0-9_]+)?")
                    (:pattern "[0-9]+#[0-9a-fA-F._-]+#([eE][+-]?[0-9_]+)?")))
  git_conflict_mark (:choice
                     (:token
                      (:seq
                       (:alias
                        (:token (:prec 2 (:pattern "[<<][<<][<<][<<][<<][<<][<<]")))
                        "<<<<<<<")
                       (:pattern ".*")))
                     (:token
                      (:seq
                       (:alias
                        (:token (:prec 2 (:pattern "[>>][>>][>>][>>][>>][>>][>>]")))
                        ">>>>>>>")
                       (:pattern ".*")))
                     (:token
                      (:seq
                       (:alias
                        (:token (:prec 2 (:pattern "[==][==][==][==][==][==][==]")))
                        "=======")
                       (:pattern ".*"))))
  relational_operator (:choice "=" "/=" "<" "<=" ">" ">=")
  binary_adding_operator (:choice "+" "-" "&")
  unary_adding_operator (:choice "+" "-")
  multiplying_operator (:choice "*" "/" "mod" "rem")
  tick "'"
  _name_not_function_call (:choice
                           identifier
                           gnatprep_identifier
                           selected_component
                           _attribute_reference
                           qualified_expression
                           target_name
                           slice
                           character_literal
                           string_literal)
  _name (:choice _name_not_function_call function_call)
  _name_for_component_choice (:choice identifier string_literal)
  _subtype_mark (:choice identifier selected_component _attribute_reference)
  selected_component (:prec-left 0
                      (:seq
                       (:field :prefix _name)
                       (:seq
                        "."
                        (:field :selector_name
                         (:choice identifier character_literal string_literal)))))
  target_name "@"
  _name_list (:prec-left 0 (:seq _name (:repeat (:seq "," _name))))
  _defining_identifier_list (:seq identifier (:repeat (:seq "," identifier)))
  slice (:seq (:field :prefix _name) "(" range_g ")")
  _attribute_reference (:choice
                        (:seq _name tick attribute_designator)
                        _reduction_attribute_reference)
  _reduction_attribute_reference (:seq value_sequence tick reduction_attribute_designator)
  reduction_attribute_designator (:seq identifier "(" reduction_specification ")")
  reduction_specification (:seq _name "," expression)
  value_sequence (:seq
                  "["
                  (:choice
                   (:seq
                    (:field :is_parallel
                     (:alias
                      (:token (:prec 2 (:pattern "[pP][aA][rR][aA][lL][lL][eE][lL]")))
                      "parallel"))
                    (:choice (:seq "(" chunk_specification ")") :blank))
                   :blank)
                  iterated_element_association
                  "]")
  chunk_specification (:choice
                       _simple_expression
                       (:seq
                        identifier
                        (:alias (:token (:prec 2 (:pattern "[iI][nN]"))) "in")
                        _discrete_subtype_definition))
  iterated_element_association (:seq
                                (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
                                (:choice loop_parameter_specification iterator_specification)
                                (:choice
                                 (:seq
                                  (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
                                  expression)
                                 :blank)
                                "=>"
                                expression)
  _discrete_subtype_definition (:choice _subtype_indication range_g)
  loop_parameter_specification (:seq
                                identifier
                                (:alias (:token (:prec 2 (:pattern "[iI][nN]"))) "in")
                                (:choice
                                 (:alias
                                  (:token (:prec 2 (:pattern "[rR][eE][vV][eE][rR][sS][eE]")))
                                  "reverse")
                                 :blank)
                                _discrete_subtype_definition
                                (:choice iterator_filter :blank))
  _loop_parameter_subtype_indication (:choice _subtype_indication access_definition)
  iterator_filter (:seq
                   (:alias (:token (:prec 2 (:pattern "[wW][hH][eE][nN]"))) "when")
                   (:field :condition expression))
  iterator_specification (:seq
                          identifier
                          (:choice (:seq ":" _loop_parameter_subtype_indication) :blank)
                          (:choice
                           (:alias (:token (:prec 2 (:pattern "[iI][nN]"))) "in")
                           (:alias (:token (:prec 2 (:pattern "[oO][fF]"))) "of"))
                          (:choice
                           (:alias
                            (:token (:prec 2 (:pattern "[rR][eE][vV][eE][rR][sS][eE]")))
                            "reverse")
                           :blank)
                          (:field :iterator_name _name)
                          (:choice iterator_filter :blank))
  attribute_designator (:choice
                        identifier
                        (:alias (:token (:prec 2 (:pattern "[aA][cC][cC][eE][sS][sS]"))) "access")
                        (:alias (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]"))) "delta")
                        (:alias (:token (:prec 2 (:pattern "[dD][iI][gG][iI][tT][sS]"))) "digits")
                        (:alias (:token (:prec 2 (:pattern "[mM][oO][dD]"))) "mod"))
  qualified_expression (:seq
                        (:field :subtype_name _name)
                        tick
                        (:choice _aggregate _parenthesized_expression))
  compilation_unit (:choice
                    with_clause
                    (:seq
                     (:choice
                      (:alias
                       (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]")))
                       "private")
                      :blank)
                     _declarative_item)
                    _statement
                    subunit
                    entry_declaration)
  _declarative_item (:choice _basic_declarative_item _proper_body body_stub)
  _basic_declarative_item (:choice _basic_declaration _aspect_clause use_clause)
  _basic_declaration (:choice
                      _type_declaration
                      subtype_declaration
                      object_declaration
                      number_declaration
                      subprogram_declaration
                      expression_function_declaration
                      null_procedure_declaration
                      package_declaration
                      _renaming_declaration
                      exception_declaration
                      _generic_declaration
                      generic_instantiation)
  package_declaration (:seq _package_specification ";")
  _package_specification (:seq
                          (:alias
                           (:token (:prec 2 (:pattern "[pP][aA][cC][kK][aA][gG][eE]")))
                           "package")
                          (:field :name _name)
                          (:choice aspect_specification :blank)
                          (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                          (:repeat _basic_declarative_item_pragma)
                          (:choice
                           (:seq
                            (:alias
                             (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]")))
                             "private")
                            (:repeat _basic_declarative_item_pragma))
                           :blank)
                          (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                          (:field :endname (:choice _name :blank)))
  with_clause (:seq
               (:field :is_limited
                (:choice
                 (:alias (:token (:prec 2 (:pattern "[lL][iI][mM][iI][tT][eE][dD]"))) "limited")
                 :blank))
               (:field :is_private
                (:choice
                 (:alias (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]"))) "private")
                 :blank))
               (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
               _name_list
               ";")
  use_clause (:seq
              (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
              (:choice
               (:seq
                (:field :is_all
                 (:choice (:alias (:token (:prec 2 (:pattern "[aA][lL][lL]"))) "all") :blank))
                (:field :is_type (:alias (:token (:prec 2 (:pattern "[tT][yY][pP][eE]"))) "type")))
               :blank)
              _name_list
              ";")
  subunit (:seq
           (:alias (:token (:prec 2 (:pattern "[sS][eE][pP][aA][rR][aA][tT][eE]"))) "separate")
           "("
           (:field :parent_unit_name _name)
           ")"
           _proper_body)
  _proper_body (:choice subprogram_body package_body task_body protected_body)
  subprogram_body (:seq
                   (:choice overriding_indicator :blank)
                   _subprogram_specification
                   (:choice aspect_specification :blank)
                   (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                   (:choice non_empty_declarative_part :blank)
                   (:alias (:token (:prec 2 (:pattern "[bB][eE][gG][iI][nN]"))) "begin")
                   handled_sequence_of_statements
                   (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                   (:choice (:field :endname _name) :blank)
                   ";")
  package_body (:seq
                (:alias (:token (:prec 2 (:pattern "[pP][aA][cC][kK][aA][gG][eE]"))) "package")
                (:alias (:token (:prec 2 (:pattern "[bB][oO][dD][yY]"))) "body")
                (:field :name _name)
                (:choice aspect_specification :blank)
                (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                (:choice non_empty_declarative_part :blank)
                (:choice
                 (:seq
                  (:alias (:token (:prec 2 (:pattern "[bB][eE][gG][iI][nN]"))) "begin")
                  handled_sequence_of_statements)
                 :blank)
                (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                (:choice (:field :endname _name) :blank)
                ";")
  _subtype_indication (:seq
                       (:choice null_exclusion :blank)
                       (:field :subtype_mark _name_not_function_call)
                       (:choice _constraint :blank))
  discriminant_constraint (:choice
                           _parenthesized_expression
                           (:seq
                            "("
                            (:seq
                             discriminant_association
                             (:repeat (:seq "," discriminant_association)))
                            ")"))
  discriminant_association (:seq
                            (:choice
                             (:seq
                              (:seq
                               _name_for_component_choice
                               (:repeat (:seq "|" _name_for_component_choice)))
                              "=>")
                             :blank)
                            expression)
  _constraint (:choice _scalar_constraint index_constraint discriminant_constraint)
  _scalar_constraint (:choice range_constraint digits_constraint delta_constraint)
  range_g (:choice
           (:field :range_attribute_reference
            (:seq (:field :prefix _name) tick range_attribute_designator))
           (:seq _simple_expression ".." _simple_expression))
  range_attribute_designator (:seq
                              (:alias (:token (:prec 2 (:pattern "[rR][aA][nN][gG][eE]"))) "range")
                              (:choice (:seq "(" expression ")") :blank))
  range_constraint (:seq
                    (:alias (:token (:prec 2 (:pattern "[rR][aA][nN][gG][eE]"))) "range")
                    range_g)
  expression (:choice
              (:seq
               _relation
               (:repeat
                (:seq
                 (:seq
                  (:alias (:token (:prec 2 (:pattern "[aA][nN][dD]"))) "and")
                  (:choice (:alias (:token (:prec 2 (:pattern "[tT][hH][eE][nN]"))) "then") :blank))
                 _relation)))
              (:seq
               _relation
               (:repeat
                (:seq
                 (:seq
                  (:alias (:token (:prec 2 (:pattern "[oO][rR]"))) "or")
                  (:choice (:alias (:token (:prec 2 (:pattern "[eE][lL][sS][eE]"))) "else") :blank))
                 _relation)))
              (:seq
               _relation
               (:repeat
                (:seq (:alias (:token (:prec 2 (:pattern "[xX][oO][rR]"))) "xor") _relation))))
  _relation (:choice
             (:seq
              _simple_expression
              (:choice (:seq relational_operator _simple_expression) :blank))
             relation_membership
             raise_expression)
  relation_membership (:seq
                       _simple_expression
                       (:choice (:alias (:token (:prec 2 (:pattern "[nN][oO][tT]"))) "not") :blank)
                       (:alias (:token (:prec 2 (:pattern "[iI][nN]"))) "in")
                       membership_choice_list)
  raise_expression (:prec-right 1
                    (:seq
                     (:alias (:token (:prec 2 (:pattern "[rR][aA][iI][sS][eE]"))) "raise")
                     (:field :exception_name _name)
                     (:choice
                      (:seq
                       (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                       _simple_expression)
                      :blank)))
  membership_choice_list (:prec-right 0
                          (:seq _membership_choice (:repeat (:seq "|" _membership_choice))))
  _membership_choice (:choice _simple_expression range_g)
  _simple_expression (:seq
                      (:choice unary_adding_operator :blank)
                      term
                      (:repeat (:seq binary_adding_operator term)))
  term (:seq _factor (:repeat (:seq multiplying_operator _factor)))
  _factor (:choice _primary factor_power factor_abs factor_not)
  factor_power (:seq (:field :left _primary) "**" (:field :right _primary))
  factor_abs (:seq (:alias (:token (:prec 2 (:pattern "[aA][bB][sS]"))) "abs") _primary)
  factor_not (:seq (:alias (:token (:prec 2 (:pattern "[nN][oO][tT]"))) "not") _primary)
  _parenthesized_expression (:seq
                             "("
                             (:choice
                              expression
                              _conditional_expression
                              quantified_expression
                              declare_expression)
                             ")")
  _primary (:prec 2
            (:choice
             numeric_literal
             primary_null
             _aggregate
             (:field :name _name)
             allocator
             _parenthesized_expression))
  primary_null (:alias (:token (:prec 2 (:pattern "[nN][uU][lL][lL]"))) "null")
  allocator (:seq
             (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
             (:choice subpool_specification :blank)
             (:choice _subtype_indication_paren_constraint qualified_expression))
  _subtype_indication_paren_constraint (:seq
                                        (:choice null_exclusion :blank)
                                        (:field :subtype_mark _subtype_mark)
                                        (:choice
                                         (:choice
                                          (:prec-dynamic 1 discriminant_constraint)
                                          index_constraint)
                                         :blank))
  subpool_specification (:seq "(" (:field :subpool_handle_name _name) ")")
  _access_type_definition (:seq
                           (:choice null_exclusion :blank)
                           (:choice access_to_object_definition access_to_subprogram_definition))
  access_to_subprogram_definition (:seq
                                   (:alias
                                    (:token (:prec 2 (:pattern "[aA][cC][cC][eE][sS][sS]")))
                                    "access")
                                   (:choice
                                    (:alias
                                     (:token
                                      (:prec 2 (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]")))
                                     "protected")
                                    :blank)
                                   (:choice
                                    (:seq
                                     (:alias
                                      (:token
                                       (:prec 2 (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]")))
                                      "procedure")
                                     (:choice formal_part :blank))
                                    (:seq
                                     (:alias
                                      (:token
                                       (:prec 2 (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]")))
                                      "function")
                                     _parameter_and_result_profile)))
  access_to_object_definition (:seq
                               (:alias
                                (:token (:prec 2 (:pattern "[aA][cC][cC][eE][sS][sS]")))
                                "access")
                               (:choice general_access_modifier :blank)
                               _subtype_indication)
  general_access_modifier (:choice
                           (:alias (:token (:prec 2 (:pattern "[aA][lL][lL]"))) "all")
                           (:alias
                            (:token (:prec 2 (:pattern "[cC][oO][nN][sS][tT][aA][nN][tT]")))
                            "constant"))
  access_definition (:seq
                     (:choice null_exclusion :blank)
                     (:alias (:token (:prec 2 (:pattern "[aA][cC][cC][eE][sS][sS]"))) "access")
                     (:choice
                      (:seq
                       (:choice
                        (:alias
                         (:token (:prec 2 (:pattern "[cC][oO][nN][sS][tT][aA][nN][tT]")))
                         "constant")
                        :blank)
                       (:field :subtype_mark _name))
                      (:seq
                       (:choice
                        (:alias
                         (:token (:prec 2 (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]")))
                         "protected")
                        :blank)
                       (:alias
                        (:token (:prec 2 (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]")))
                        "procedure")
                       (:choice formal_part :blank))
                      (:seq
                       (:choice
                        (:alias
                         (:token (:prec 2 (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]")))
                         "protected")
                        :blank)
                       (:alias
                        (:token (:prec 2 (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]")))
                        "function")
                       _parameter_and_result_profile)))
  actual_parameter_part (:seq
                         "("
                         (:choice
                          (:seq parameter_association (:repeat (:seq "," parameter_association)))
                          _conditional_expression
                          quantified_expression
                          declare_expression)
                         ")")
  parameter_association (:choice
                         (:seq component_choice_list "=>" (:choice expression "<>"))
                         expression
                         "<>")
  _conditional_expression (:choice if_expression case_expression)
  _conditional_quantified_declare_expression (:choice
                                              _conditional_expression
                                              quantified_expression
                                              declare_expression)
  quantified_expression (:seq
                         (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
                         quantifier
                         (:choice loop_parameter_specification iterator_specification)
                         "=>"
                         (:field :predicate expression))
  declare_expression (:seq
                      (:alias
                       (:token (:prec 2 (:pattern "[dD][eE][cC][lL][aA][rR][eE]")))
                       "declare")
                      (:repeat _declare_item)
                      (:alias (:token (:prec 2 (:pattern "[bB][eE][gG][iI][nN]"))) "begin")
                      expression)
  _declare_item (:choice object_declaration object_renaming_declaration pragma_g)
  quantifier (:choice
              (:alias (:token (:prec 2 (:pattern "[aA][lL][lL]"))) "all")
              (:alias (:token (:prec 2 (:pattern "[sS][oO][mM][eE]"))) "some"))
  case_expression (:seq
                   (:alias (:token (:prec 2 (:pattern "[cC][aA][sS][eE]"))) "case")
                   expression
                   (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                   (:seq
                    case_expression_alternative
                    (:repeat (:seq "," case_expression_alternative))))
  case_expression_alternative (:seq
                               (:alias (:token (:prec 2 (:pattern "[wW][hH][eE][nN]"))) "when")
                               discrete_choice_list
                               "=>"
                               expression)
  component_choice_list (:choice
                         (:alias (:token (:prec 2 (:pattern "[oO][tT][hH][eE][rR][sS]"))) "others")
                         (:seq
                          (:prec-dynamic 1 _name_for_component_choice)
                          (:repeat (:seq "|" (:prec-dynamic 1 _name_for_component_choice)))))
  _aggregate (:choice record_aggregate extension_aggregate _array_aggregate _delta_aggregate)
  _delta_aggregate (:choice record_delta_aggregate array_delta_aggregate)
  extension_aggregate (:seq
                       "("
                       expression
                       (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                       _record_component_association_list_or_expression
                       ")")
  record_delta_aggregate (:seq
                          "("
                          expression
                          (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                          (:alias (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]"))) "delta")
                          _record_component_association_list_or_expression
                          ")")
  array_delta_aggregate (:choice
                         (:seq
                          "("
                          expression
                          (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                          (:alias (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]"))) "delta")
                          _array_component_association_list
                          ")")
                         (:seq
                          "["
                          expression
                          (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                          (:alias (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]"))) "delta")
                          _array_component_association_list
                          "]"))
  record_aggregate (:seq "(" record_component_association_list ")")
  record_component_association_list (:choice
                                     (:seq
                                      (:alias
                                       (:token (:prec 2 (:pattern "[nN][uU][lL][lL]")))
                                       "null")
                                      (:alias
                                       (:token (:prec 2 (:pattern "[rR][eE][cC][oO][rR][dD]")))
                                       "record"))
                                     (:seq
                                      expression
                                      ","
                                      (:seq
                                       (:choice expression _named_record_component_association)
                                       (:repeat
                                        (:seq
                                         ","
                                         (:choice expression _named_record_component_association)))))
                                     (:seq
                                      _named_record_component_association
                                      (:repeat (:seq "," _named_record_component_association))))
  _record_component_association_list_or_expression (:choice
                                                    record_component_association_list
                                                    expression)
  _named_record_component_association (:seq component_choice_list "=>" (:choice expression "<>"))
  null_exclusion (:seq
                  (:alias (:token (:prec 2 (:pattern "[nN][oO][tT]"))) "not")
                  (:alias (:token (:prec 2 (:pattern "[nN][uU][lL][lL]"))) "null"))
  index_constraint (:seq "(" (:seq _discrete_range (:repeat (:seq "," _discrete_range))) ")")
  digits_constraint (:seq
                     (:alias (:token (:prec 2 (:pattern "[dD][iI][gG][iI][tT][sS]"))) "digits")
                     _simple_expression
                     (:choice range_constraint :blank))
  delta_constraint (:seq
                    (:alias (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]"))) "delta")
                    _simple_expression
                    (:choice range_constraint :blank))
  _basic_declarative_item_pragma (:choice _basic_declarative_item pragma_g)
  _type_declaration (:choice
                     full_type_declaration
                     incomplete_type_declaration
                     private_type_declaration
                     private_extension_declaration)
  full_type_declaration (:choice
                         (:seq
                          (:alias (:token (:prec 2 (:pattern "[tT][yY][pP][eE]"))) "type")
                          identifier
                          (:choice known_discriminant_part :blank)
                          (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                          _type_definition
                          (:choice aspect_specification :blank)
                          ";")
                         task_type_declaration
                         protected_type_declaration)
  private_type_declaration (:seq
                            (:alias (:token (:prec 2 (:pattern "[tT][yY][pP][eE]"))) "type")
                            identifier
                            (:choice _discriminant_part :blank)
                            (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                            (:choice
                             (:seq
                              (:choice
                               (:alias
                                (:token (:prec 2 (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]")))
                                "abstract")
                               :blank)
                              (:alias
                               (:token (:prec 2 (:pattern "[tT][aA][gG][gG][eE][dD]")))
                               "tagged"))
                             :blank)
                            (:choice
                             (:alias
                              (:token (:prec 2 (:pattern "[lL][iI][mM][iI][tT][eE][dD]")))
                              "limited")
                             :blank)
                            (:alias
                             (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]")))
                             "private")
                            (:choice aspect_specification :blank)
                            ";")
  private_extension_declaration (:seq
                                 (:alias (:token (:prec 2 (:pattern "[tT][yY][pP][eE]"))) "type")
                                 identifier
                                 (:choice _discriminant_part :blank)
                                 (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                                 (:choice
                                  (:alias
                                   (:token (:prec 2 (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]")))
                                   "abstract")
                                  :blank)
                                 (:choice
                                  (:choice
                                   (:alias
                                    (:token (:prec 2 (:pattern "[lL][iI][mM][iI][tT][eE][dD]")))
                                    "limited")
                                   (:alias
                                    (:token
                                     (:prec 2
                                      (:pattern "[sS][yY][nN][cC][hH][rR][oO][nN][iI][zZ][eE][dD]")))
                                    "synchronized"))
                                  :blank)
                                 (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                                 _subtype_indication
                                 (:choice
                                  (:seq
                                   (:alias (:token (:prec 2 (:pattern "[aA][nN][dD]"))) "and")
                                   _interface_list)
                                  :blank)
                                 (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                                 (:alias
                                  (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]")))
                                  "private")
                                 (:choice aspect_specification :blank)
                                 ";")
  _discriminant_part (:choice known_discriminant_part unknown_discriminant_part)
  unknown_discriminant_part (:seq "(" "<>" ")")
  known_discriminant_part (:seq "(" discriminant_specification_list ")")
  incomplete_type_declaration (:seq
                               (:alias (:token (:prec 2 (:pattern "[tT][yY][pP][eE]"))) "type")
                               identifier
                               (:choice _discriminant_part :blank)
                               (:choice
                                (:seq
                                 (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                                 (:alias
                                  (:token (:prec 2 (:pattern "[tT][aA][gG][gG][eE][dD]")))
                                  "tagged"))
                                :blank)
                               ";")
  discriminant_specification_list (:prec-right 0
                                   (:seq
                                    discriminant_specification
                                    (:repeat (:seq ";" discriminant_specification))))
  discriminant_specification (:seq
                              _defining_identifier_list
                              ":"
                              (:choice
                               (:seq (:choice null_exclusion :blank) (:field :subtype_mark _name))
                               access_definition)
                              (:choice _assign_value :blank)
                              (:choice aspect_specification :blank))
  _type_definition (:choice
                    enumeration_type_definition
                    _integer_type_definition
                    _real_type_definition
                    array_type_definition
                    record_type_definition
                    _access_type_definition
                    derived_type_definition
                    interface_type_definition)
  array_type_definition (:seq
                         (:alias (:token (:prec 2 (:pattern "[aA][rR][rR][aA][yY]"))) "array")
                         "("
                         (:choice _discrete_subtype_definition_list _index_subtype_definition_list)
                         ")"
                         (:alias (:token (:prec 2 (:pattern "[oO][fF]"))) "of")
                         component_definition)
  _discrete_subtype_definition_list (:seq
                                     _discrete_subtype_definition
                                     (:repeat (:seq "," _discrete_subtype_definition)))
  _discrete_range (:choice _subtype_indication range_g)
  _index_subtype_definition_list (:seq
                                  index_subtype_definition
                                  (:repeat (:seq "," index_subtype_definition)))
  index_subtype_definition (:seq
                            (:field :subtype_mark _name)
                            (:alias (:token (:prec 2 (:pattern "[rR][aA][nN][gG][eE]"))) "range")
                            "<>")
  enumeration_type_definition (:seq "(" _enumeration_literal_list ")")
  _enumeration_literal_list (:seq
                             _enumeration_literal_specification
                             (:repeat (:seq "," _enumeration_literal_specification)))
  _enumeration_literal_specification (:choice identifier character_literal)
  _integer_type_definition (:choice signed_integer_type_definition modular_type_definition)
  modular_type_definition (:seq
                           (:alias (:token (:prec 2 (:pattern "[mM][oO][dD]"))) "mod")
                           expression)
  _real_type_definition (:choice floating_point_definition _fixed_point_definition)
  floating_point_definition (:seq
                             (:alias
                              (:token (:prec 2 (:pattern "[dD][iI][gG][iI][tT][sS]")))
                              "digits")
                             expression
                             (:choice real_range_specification :blank))
  real_range_specification (:seq
                            (:alias (:token (:prec 2 (:pattern "[rR][aA][nN][gG][eE]"))) "range")
                            _simple_expression
                            ".."
                            _simple_expression)
  _fixed_point_definition (:choice ordinary_fixed_point_definition decimal_fixed_point_definition)
  decimal_fixed_point_definition (:seq
                                  (:alias
                                   (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]")))
                                   "delta")
                                  expression
                                  (:alias
                                   (:token (:prec 2 (:pattern "[dD][iI][gG][iI][tT][sS]")))
                                   "digits")
                                  expression
                                  (:choice real_range_specification :blank))
  ordinary_fixed_point_definition (:seq
                                   (:alias
                                    (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]")))
                                    "delta")
                                   expression
                                   real_range_specification)
  signed_integer_type_definition (:seq
                                  (:alias
                                   (:token (:prec 2 (:pattern "[rR][aA][nN][gG][eE]")))
                                   "range")
                                  _simple_expression
                                  ".."
                                  _simple_expression)
  derived_type_definition (:seq
                           (:choice
                            (:alias
                             (:token (:prec 2 (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]")))
                             "abstract")
                            :blank)
                           (:choice
                            (:alias
                             (:token (:prec 2 (:pattern "[lL][iI][mM][iI][tT][eE][dD]")))
                             "limited")
                            :blank)
                           (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                           _subtype_indication
                           (:choice
                            (:seq
                             (:choice
                              (:seq
                               (:alias (:token (:prec 2 (:pattern "[aA][nN][dD]"))) "and")
                               _interface_list)
                              :blank)
                             record_extension_part)
                            :blank))
  interface_type_definition (:seq
                             (:choice
                              (:choice
                               (:alias
                                (:token (:prec 2 (:pattern "[lL][iI][mM][iI][tT][eE][dD]")))
                                "limited")
                               (:alias (:token (:prec 2 (:pattern "[tT][aA][sS][kK]"))) "task")
                               (:alias
                                (:token (:prec 2 (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]")))
                                "protected")
                               (:alias
                                (:token
                                 (:prec 2
                                  (:pattern "[sS][yY][nN][cC][hH][rR][oO][nN][iI][zZ][eE][dD]")))
                                "synchronized"))
                              :blank)
                             (:alias
                              (:token (:prec 2 (:pattern "[iI][nN][tT][eE][rR][fF][aA][cC][eE]")))
                              "interface")
                             (:choice
                              (:seq
                               (:alias (:token (:prec 2 (:pattern "[aA][nN][dD]"))) "and")
                               _interface_list)
                              :blank))
  _interface_list (:seq
                   _name
                   (:repeat
                    (:seq (:alias (:token (:prec 2 (:pattern "[aA][nN][dD]"))) "and") _name)))
  record_extension_part (:seq
                         (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                         record_definition)
  record_type_definition (:seq
                          (:choice
                           (:seq
                            (:choice
                             (:alias
                              (:token (:prec 2 (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]")))
                              "abstract")
                             :blank)
                            (:alias
                             (:token (:prec 2 (:pattern "[tT][aA][gG][gG][eE][dD]")))
                             "tagged"))
                           :blank)
                          (:choice
                           (:alias
                            (:token (:prec 2 (:pattern "[lL][iI][mM][iI][tT][eE][dD]")))
                            "limited")
                           :blank)
                          record_definition)
  record_definition (:choice
                     (:seq
                      (:alias (:token (:prec 2 (:pattern "[rR][eE][cC][oO][rR][dD]"))) "record")
                      component_list
                      (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                      (:alias (:token (:prec 2 (:pattern "[rR][eE][cC][oO][rR][dD]"))) "record")
                      (:choice identifier :blank))
                     (:seq
                      (:alias (:token (:prec 2 (:pattern "[nN][uU][lL][lL]"))) "null")
                      (:alias (:token (:prec 2 (:pattern "[rR][eE][cC][oO][rR][dD]"))) "record")))
  component_list (:choice
                  (:repeat1 _component_item)
                  (:seq (:repeat _component_item) variant_part)
                  (:seq
                   (:alias (:token (:prec 2 (:pattern "[nN][uU][lL][lL]"))) "null")
                   (:alias (:token (:prec 2 (:pattern "[;;]"))) ";")))
  _component_item (:choice component_declaration _aspect_clause pragma_g)
  component_declaration (:seq
                         _defining_identifier_list
                         ":"
                         component_definition
                         (:choice _assign_value :blank)
                         (:choice aspect_specification :blank)
                         ";")
  component_definition (:seq
                        (:choice
                         (:alias
                          (:token (:prec 2 (:pattern "[aA][lL][iI][aA][sS][eE][dD]")))
                          "aliased")
                         :blank)
                        (:choice _subtype_indication access_definition))
  _array_aggregate (:choice positional_array_aggregate null_array_aggregate named_array_aggregate)
  positional_array_aggregate (:choice
                              (:seq
                               "("
                               expression
                               ","
                               (:prec-left 1 (:seq expression (:repeat (:seq "," expression))))
                               ")")
                              (:seq
                               "("
                               (:seq expression (:repeat (:seq "," expression)))
                               ","
                               (:alias
                                (:token (:prec 2 (:pattern "[oO][tT][hH][eE][rR][sS]")))
                                "others")
                               "=>"
                               (:choice expression "<>")
                               ")")
                              (:seq
                               "["
                               (:seq expression (:repeat (:seq "," expression)))
                               (:choice
                                (:seq
                                 ","
                                 (:alias
                                  (:token (:prec 2 (:pattern "[oO][tT][hH][eE][rR][sS]")))
                                  "others")
                                 "=>"
                                 (:choice expression "<>"))
                                :blank)
                               "]"))
  null_array_aggregate (:seq "[" "]")
  named_array_aggregate (:choice
                         (:seq "(" _array_component_association_list ")")
                         (:seq "[" _array_component_association_list "]"))
  _array_component_association_list (:seq
                                     array_component_association
                                     (:repeat (:seq "," array_component_association)))
  array_component_association (:choice
                               (:seq discrete_choice_list "=>" (:choice expression "<>"))
                               iterated_element_association)
  discrete_choice_list (:seq discrete_choice (:repeat (:seq "|" discrete_choice)))
  discrete_choice (:choice
                   (:prec-dynamic 1 expression)
                   _subtype_indication
                   range_g
                   (:alias (:token (:prec 2 (:pattern "[oO][tT][hH][eE][rR][sS]"))) "others"))
  aspect_association (:seq _aspect_mark (:choice (:seq "=>" _aspect_definition) :blank))
  _aspect_clause (:choice
                  attribute_definition_clause
                  enumeration_representation_clause
                  record_representation_clause
                  at_clause)
  _aspect_definition (:choice expression global_aspect_definition)
  _aspect_mark (:seq
                identifier
                (:choice
                 (:seq tick (:alias (:token (:prec 2 (:pattern "[CC][lL][aA][sS][sS]"))) "Class"))
                 :blank))
  aspect_mark_list (:seq aspect_association (:repeat (:seq "," aspect_association)))
  aspect_specification (:seq
                        (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                        aspect_mark_list)
  _assign_value (:seq ":=" expression)
  at_clause (:seq
             (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
             identifier
             (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
             (:alias (:token (:prec 2 (:pattern "[aA][tT]"))) "at")
             expression
             ";")
  attribute_definition_clause (:seq
                               (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
                               (:field :local_name _name)
                               tick
                               attribute_designator
                               (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
                               expression
                               ";")
  body_stub (:choice subprogram_body_stub package_body_stub task_body_stub protected_body_stub)
  subprogram_body_stub (:seq
                        (:choice overriding_indicator :blank)
                        _subprogram_specification
                        (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                        (:alias
                         (:token (:prec 2 (:pattern "[sS][eE][pP][aA][rR][aA][tT][eE]")))
                         "separate")
                        (:choice aspect_specification :blank)
                        ";")
  package_body_stub (:seq
                     (:alias (:token (:prec 2 (:pattern "[pP][aA][cC][kK][aA][gG][eE]"))) "package")
                     (:alias (:token (:prec 2 (:pattern "[bB][oO][dD][yY]"))) "body")
                     identifier
                     (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                     (:alias
                      (:token (:prec 2 (:pattern "[sS][eE][pP][aA][rR][aA][tT][eE]")))
                      "separate")
                     (:choice aspect_specification :blank)
                     ";")
  task_body (:seq
             (:alias (:token (:prec 2 (:pattern "[tT][aA][sS][kK]"))) "task")
             (:alias (:token (:prec 2 (:pattern "[bB][oO][dD][yY]"))) "body")
             identifier
             (:choice aspect_specification :blank)
             (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
             (:choice non_empty_declarative_part :blank)
             (:alias (:token (:prec 2 (:pattern "[bB][eE][gG][iI][nN]"))) "begin")
             handled_sequence_of_statements
             (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
             (:choice identifier :blank)
             ";")
  task_body_stub (:seq
                  (:alias (:token (:prec 2 (:pattern "[tT][aA][sS][kK]"))) "task")
                  (:alias (:token (:prec 2 (:pattern "[bB][oO][dD][yY]"))) "body")
                  identifier
                  (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                  (:alias
                   (:token (:prec 2 (:pattern "[sS][eE][pP][aA][rR][aA][tT][eE]")))
                   "separate")
                  (:choice aspect_specification :blank)
                  ";")
  _protected_operation_declaration (:choice
                                    subprogram_declaration
                                    pragma_g
                                    entry_declaration
                                    _aspect_clause)
  _protected_element_declaration (:choice _protected_operation_declaration component_declaration)
  _protected_operation_item (:choice
                             subprogram_declaration
                             subprogram_body
                             null_procedure_declaration
                             expression_function_declaration
                             entry_body
                             _aspect_clause)
  protected_definition (:seq
                        (:repeat _protected_operation_declaration)
                        (:choice
                         (:seq
                          (:alias
                           (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]")))
                           "private")
                          (:repeat _protected_element_declaration))
                         :blank)
                        (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                        (:choice identifier :blank))
  protected_type_declaration (:seq
                              (:alias
                               (:token (:prec 2 (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]")))
                               "protected")
                              (:alias (:token (:prec 2 (:pattern "[tT][yY][pP][eE]"))) "type")
                              identifier
                              (:choice known_discriminant_part :blank)
                              (:choice aspect_specification :blank)
                              (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                              (:choice
                               (:seq
                                (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                                _interface_list
                                (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with"))
                               :blank)
                              protected_definition
                              ";")
  single_protected_declaration (:seq
                                (:alias
                                 (:token
                                  (:prec 2 (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]")))
                                 "protected")
                                identifier
                                (:choice aspect_specification :blank)
                                (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                                (:choice
                                 (:seq
                                  (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                                  _interface_list
                                  (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with"))
                                 :blank)
                                protected_definition
                                ";")
  protected_body (:seq
                  (:alias
                   (:token (:prec 2 (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]")))
                   "protected")
                  (:alias (:token (:prec 2 (:pattern "[bB][oO][dD][yY]"))) "body")
                  identifier
                  (:choice aspect_specification :blank)
                  (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                  (:repeat _protected_operation_item)
                  (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                  (:choice identifier :blank)
                  ";")
  protected_body_stub (:seq
                       (:alias
                        (:token (:prec 2 (:pattern "[pP][rR][oO][tT][eE][cC][tT][eE][dD]")))
                        "protected")
                       (:alias (:token (:prec 2 (:pattern "[bB][oO][dD][yY]"))) "body")
                       identifier
                       (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                       (:alias
                        (:token (:prec 2 (:pattern "[sS][eE][pP][aA][rR][aA][tT][eE]")))
                        "separate")
                       (:choice aspect_specification :blank)
                       ";")
  choice_parameter_specification identifier
  component_clause (:seq
                    (:field :local_name _name)
                    (:alias (:token (:prec 2 (:pattern "[aA][tT]"))) "at")
                    (:field :position expression)
                    (:alias (:token (:prec 2 (:pattern "[rR][aA][nN][gG][eE]"))) "range")
                    (:field :first_bit _simple_expression)
                    ".."
                    (:field :last_bit _simple_expression)
                    ";")
  _declarative_item_pragma (:choice _declarative_item pragma_g gnatprep_declarative_if_statement)
  non_empty_declarative_part (:repeat1 _declarative_item_pragma)
  entry_declaration (:seq
                     (:choice overriding_indicator :blank)
                     (:alias (:token (:prec 2 (:pattern "[eE][nN][tT][rR][yY]"))) "entry")
                     (:field :entry_name identifier)
                     (:choice (:seq "(" _discrete_subtype_definition ")") :blank)
                     (:field :parameter_profile (:choice formal_part :blank))
                     (:choice aspect_specification :blank)
                     ";")
  entry_body (:seq
              (:alias (:token (:prec 2 (:pattern "[eE][nN][tT][rR][yY]"))) "entry")
              identifier
              (:choice (:seq "(" entry_index_specification ")") :blank)
              (:field :parameter_profile (:choice formal_part :blank))
              (:choice aspect_specification :blank)
              entry_barrier
              (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
              (:choice non_empty_declarative_part :blank)
              (:alias (:token (:prec 2 (:pattern "[bB][eE][gG][iI][nN]"))) "begin")
              handled_sequence_of_statements
              (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
              (:choice identifier :blank)
              ";")
  entry_barrier (:seq
                 (:alias (:token (:prec 2 (:pattern "[wW][hH][eE][nN]"))) "when")
                 (:field :condition expression))
  entry_index_specification (:seq
                             (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
                             identifier
                             (:alias (:token (:prec 2 (:pattern "[iI][nN]"))) "in")
                             _discrete_subtype_definition
                             (:choice aspect_specification :blank))
  enumeration_aggregate _array_aggregate
  enumeration_representation_clause (:seq
                                     (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
                                     (:field :local_name _name)
                                     (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
                                     enumeration_aggregate
                                     ";")
  exception_choice_list (:seq exception_choice (:repeat (:seq "|" exception_choice)))
  exception_choice (:choice
                    (:field :exception_name _name)
                    (:alias (:token (:prec 2 (:pattern "[oO][tT][hH][eE][rR][sS]"))) "others"))
  exception_declaration (:seq
                         _defining_identifier_list
                         ":"
                         (:alias
                          (:token (:prec 2 (:pattern "[eE][xX][cC][eE][pP][tT][iI][oO][nN]")))
                          "exception")
                         (:choice aspect_specification :blank)
                         ";")
  exception_handler (:seq
                     (:alias (:token (:prec 2 (:pattern "[wW][hH][eE][nN]"))) "when")
                     (:choice (:seq choice_parameter_specification ":") :blank)
                     exception_choice_list
                     "=>"
                     _sequence_of_statements)
  formal_part (:seq "(" _parameter_specification_list ")")
  function_specification (:seq
                          (:alias
                           (:token (:prec 2 (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]")))
                           "function")
                          (:field :name _name)
                          _parameter_and_result_profile)
  _generic_declaration (:choice generic_subprogram_declaration generic_package_declaration)
  generic_formal_part (:seq
                       (:alias
                        (:token (:prec 2 (:pattern "[gG][eE][nN][eE][rR][iI][cC]")))
                        "generic")
                       (:repeat _generic_formal_parameter_declaration))
  _generic_formal_parameter_declaration (:choice
                                         formal_object_declaration
                                         _formal_type_declaration
                                         formal_subprogram_declaration
                                         formal_package_declaration
                                         use_clause
                                         pragma_g)
  generic_subprogram_declaration (:seq
                                  generic_formal_part
                                  _subprogram_specification
                                  (:choice aspect_specification :blank)
                                  ";")
  generic_package_declaration (:seq generic_formal_part package_declaration)
  generic_instantiation (:seq
                         (:choice
                          (:seq
                           (:alias
                            (:token (:prec 2 (:pattern "[pP][aA][cC][kK][aA][gG][eE]")))
                            "package")
                           (:field :name _name))
                          (:seq
                           (:choice overriding_indicator :blank)
                           (:choice
                            (:seq
                             (:alias
                              (:token (:prec 2 (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]")))
                              "procedure")
                             (:field :name _name))
                            (:seq
                             (:alias
                              (:token (:prec 2 (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]")))
                              "function")
                             (:field :name _name)))))
                         (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                         (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                         (:field :generic_name _name)
                         (:choice aspect_specification :blank)
                         ";")
  formal_object_declaration (:choice
                             (:seq
                              (:field :name _defining_identifier_list)
                              ":"
                              (:choice non_empty_mode :blank)
                              (:choice null_exclusion :blank)
                              (:field :subtype_mark _name)
                              (:choice _assign_value :blank)
                              (:choice aspect_specification :blank)
                              ";")
                             (:seq
                              _defining_identifier_list
                              ":"
                              (:choice non_empty_mode :blank)
                              access_definition
                              (:choice _assign_value :blank)
                              (:choice aspect_specification :blank)
                              ";"))
  _formal_type_declaration (:choice
                            formal_complete_type_declaration
                            formal_incomplete_type_declaration)
  formal_complete_type_declaration (:seq
                                    (:alias (:token (:prec 2 (:pattern "[tT][yY][pP][eE]"))) "type")
                                    identifier
                                    (:choice _discriminant_part :blank)
                                    (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                                    _formal_type_definition
                                    (:choice
                                     (:seq
                                      (:alias (:token (:prec 2 (:pattern "[oO][rR]"))) "or")
                                      (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
                                      (:field :default_subtype_mark _name))
                                     :blank)
                                    (:choice aspect_specification :blank)
                                    ";")
  formal_incomplete_type_declaration (:seq
                                      (:alias
                                       (:token (:prec 2 (:pattern "[tT][yY][pP][eE]")))
                                       "type")
                                      identifier
                                      (:choice _discriminant_part :blank)
                                      (:choice
                                       (:seq
                                        (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                                        (:alias
                                         (:token (:prec 2 (:pattern "[tT][aA][gG][gG][eE][dD]")))
                                         "tagged"))
                                       :blank)
                                      (:choice
                                       (:seq
                                        (:alias (:token (:prec 2 (:pattern "[oO][rR]"))) "or")
                                        (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
                                        (:field :default_subtype_mark _name))
                                       :blank)
                                      ";")
  _formal_type_definition (:choice
                           formal_private_type_definition
                           formal_derived_type_definition
                           formal_discrete_type_definition
                           formal_signed_integer_type_definition
                           formal_modular_type_definition
                           formal_floating_point_definition
                           formal_ordinary_fixed_point_definition
                           formal_decimal_fixed_point_definition
                           formal_array_type_definition
                           formal_access_type_definition
                           formal_interface_type_definition)
  formal_private_type_definition (:seq
                                  (:choice
                                   (:seq
                                    (:choice
                                     (:alias
                                      (:token
                                       (:prec 2 (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]")))
                                      "abstract")
                                     :blank)
                                    (:alias
                                     (:token (:prec 2 (:pattern "[tT][aA][gG][gG][eE][dD]")))
                                     "tagged"))
                                   :blank)
                                  (:choice
                                   (:alias
                                    (:token (:prec 2 (:pattern "[lL][iI][mM][iI][tT][eE][dD]")))
                                    "limited")
                                   :blank)
                                  (:alias
                                   (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]")))
                                   "private"))
  formal_derived_type_definition (:seq
                                  (:choice
                                   (:alias
                                    (:token (:prec 2 (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]")))
                                    "abstract")
                                   :blank)
                                  (:choice
                                   (:choice
                                    (:alias
                                     (:token (:prec 2 (:pattern "[lL][iI][mM][iI][tT][eE][dD]")))
                                     "limited")
                                    (:alias
                                     (:token
                                      (:prec 2
                                       (:pattern "[sS][yY][nN][cC][hH][rR][oO][nN][iI][zZ][eE][dD]")))
                                     "synchronized"))
                                   :blank)
                                  (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                                  (:field :subtype_mark _name)
                                  (:choice
                                   (:seq
                                    (:choice
                                     (:seq
                                      (:alias (:token (:prec 2 (:pattern "[aA][nN][dD]"))) "and")
                                      _interface_list)
                                     :blank)
                                    (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                                    (:alias
                                     (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]")))
                                     "private"))
                                   :blank))
  formal_discrete_type_definition (:seq "(" "<>" ")")
  formal_signed_integer_type_definition (:seq
                                         (:alias
                                          (:token (:prec 2 (:pattern "[rR][aA][nN][gG][eE]")))
                                          "range")
                                         "<>")
  formal_modular_type_definition (:seq
                                  (:alias (:token (:prec 2 (:pattern "[mM][oO][dD]"))) "mod")
                                  "<>")
  formal_floating_point_definition (:seq
                                    (:alias
                                     (:token (:prec 2 (:pattern "[dD][iI][gG][iI][tT][sS]")))
                                     "digits")
                                    "<>")
  formal_ordinary_fixed_point_definition (:seq
                                          (:alias
                                           (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]")))
                                           "delta")
                                          "<>")
  formal_decimal_fixed_point_definition (:seq
                                         (:alias
                                          (:token (:prec 2 (:pattern "[dD][eE][lL][tT][aA]")))
                                          "delta")
                                         "<>"
                                         (:alias
                                          (:token (:prec 2 (:pattern "[dD][iI][gG][iI][tT][sS]")))
                                          "digits")
                                         "<>")
  formal_array_type_definition array_type_definition
  formal_access_type_definition _access_type_definition
  formal_interface_type_definition interface_type_definition
  formal_subprogram_declaration (:choice
                                 formal_concrete_subprogram_declaration
                                 formal_abstract_subprogram_declaration)
  formal_concrete_subprogram_declaration (:seq
                                          (:alias
                                           (:token (:prec 2 (:pattern "[wW][iI][tT][hH]")))
                                           "with")
                                          _subprogram_specification
                                          (:choice
                                           (:seq
                                            (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                                            subprogram_default)
                                           :blank)
                                          (:choice aspect_specification :blank)
                                          ";")
  formal_abstract_subprogram_declaration (:seq
                                          (:alias
                                           (:token (:prec 2 (:pattern "[wW][iI][tT][hH]")))
                                           "with")
                                          _subprogram_specification
                                          (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                                          (:alias
                                           (:token
                                            (:prec 2 (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]")))
                                           "abstract")
                                          (:choice subprogram_default :blank)
                                          (:choice aspect_specification :blank)
                                          ";")
  subprogram_default (:choice
                      (:field :default_name _name)
                      "<>"
                      (:alias (:token (:prec 2 (:pattern "[nN][uU][lL][lL]"))) "null"))
  formal_package_declaration (:seq
                              (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                              (:alias
                               (:token (:prec 2 (:pattern "[pP][aA][cC][kK][aA][gG][eE]")))
                               "package")
                              identifier
                              (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                              (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                              (:field :generic_package_name _name)
                              (:choice aspect_specification :blank)
                              ";")
  formal_group_designator (:choice "null" "all")
  extended_global_aspect_element (:choice
                                  (:seq
                                   (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
                                   (:field :formal_parameter_set
                                    (:choice
                                     formal_group_designator
                                     (:seq _name (:repeat (:seq "," _name)))))))
  finally_part (:seq
                (:alias (:token (:prec 2 (:pattern "[fF][iI][nN][aA][lL][lL][yY]"))) "finally")
                _sequence_of_statements)
  global_aspect_definition (:choice
                            (:seq global_mode)
                            (:seq
                             "("
                             (:seq global_aspect_element (:repeat (:seq "," global_aspect_element)))
                             ")"))
  global_aspect_element (:choice (:seq global_mode (:field :global_set _name_list)))
  global_mode (:choice
               non_empty_mode
               (:alias
                (:token (:prec 2 (:pattern "[oO][vV][eE][rR][rR][iI][dD][iI][nN][gG]")))
                "overriding"))
  handled_sequence_of_statements (:seq
                                  _sequence_of_statements
                                  (:choice
                                   (:seq
                                    (:alias
                                     (:token
                                      (:prec 2 (:pattern "[eE][xX][cC][eE][pP][tT][iI][oO][nN]")))
                                     "exception")
                                    (:repeat1 exception_handler))
                                   :blank)
                                  (:field :finally_part (:choice finally_part :blank)))
  loop_label (:seq (:field :statement_identifier identifier) ":")
  label (:seq "<<" (:field :statement_identifier identifier) ">>")
  mod_clause (:seq
              (:alias (:token (:prec 2 (:pattern "[aA][tT]"))) "at")
              (:alias (:token (:prec 2 (:pattern "[mM][oO][dD]"))) "mod")
              expression
              ";")
  non_empty_mode (:choice
                  (:alias (:token (:prec 2 (:pattern "[iI][nN]"))) "in")
                  (:seq
                   (:alias (:token (:prec 2 (:pattern "[iI][nN]"))) "in")
                   (:alias (:token (:prec 2 (:pattern "[oO][uU][tT]"))) "out"))
                  (:alias (:token (:prec 2 (:pattern "[oO][uU][tT]"))) "out"))
  null_procedure_declaration (:seq
                              (:choice overriding_indicator :blank)
                              procedure_specification
                              (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                              (:alias (:token (:prec 2 (:pattern "[nN][uU][lL][lL]"))) "null")
                              (:choice aspect_specification :blank)
                              ";")
  null_statement (:seq (:alias (:token (:prec 2 (:pattern "[nN][uU][lL][lL]"))) "null") ";")
  number_declaration (:seq
                      _defining_identifier_list
                      ":"
                      (:alias
                       (:token (:prec 2 (:pattern "[cC][oO][nN][sS][tT][aA][nN][tT]")))
                       "constant")
                      _assign_value
                      ";")
  object_declaration (:choice
                      (:seq
                       (:field :name _defining_identifier_list)
                       ":"
                       (:choice
                        (:alias
                         (:token (:prec 2 (:pattern "[aA][lL][iI][aA][sS][eE][dD]")))
                         "aliased")
                        :blank)
                       (:choice
                        (:alias
                         (:token (:prec 2 (:pattern "[cC][oO][nN][sS][tT][aA][nN][tT]")))
                         "constant")
                        :blank)
                       (:choice _subtype_indication access_definition array_type_definition)
                       (:choice _assign_value :blank)
                       (:choice aspect_specification :blank)
                       ";")
                      single_task_declaration
                      single_protected_declaration)
  single_task_declaration (:seq
                           (:alias (:token (:prec 2 (:pattern "[tT][aA][sS][kK]"))) "task")
                           identifier
                           (:choice aspect_specification :blank)
                           (:choice
                            (:seq
                             (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                             (:choice
                              (:seq
                               (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                               _interface_list
                               (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with"))
                              :blank)
                             task_definition)
                            :blank)
                           ";")
  task_type_declaration (:seq
                         (:alias (:token (:prec 2 (:pattern "[tT][aA][sS][kK]"))) "task")
                         (:alias (:token (:prec 2 (:pattern "[tT][yY][pP][eE]"))) "type")
                         identifier
                         (:choice known_discriminant_part :blank)
                         (:choice aspect_specification :blank)
                         (:choice
                          (:seq
                           (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                           (:choice
                            (:seq
                             (:alias (:token (:prec 2 (:pattern "[nN][eE][wW]"))) "new")
                             _interface_list
                             (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with"))
                            :blank)
                           task_definition)
                          :blank)
                         ";")
  _task_item (:choice entry_declaration _aspect_clause pragma_g)
  task_definition (:seq
                   (:repeat _task_item)
                   (:choice
                    (:seq
                     (:alias (:token (:prec 2 (:pattern "[pP][rR][iI][vV][aA][tT][eE]"))) "private")
                     (:repeat _task_item))
                    :blank)
                   (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                   (:field :endname (:choice identifier :blank)))
  overriding_indicator (:seq
                        (:choice (:alias (:token (:prec 2 (:pattern "[nN][oO][tT]"))) "not") :blank)
                        (:alias
                         (:token (:prec 2 (:pattern "[oO][vV][eE][rR][rR][iI][dD][iI][nN][gG]")))
                         "overriding"))
  _parameter_and_result_profile (:seq (:choice formal_part :blank) result_profile)
  parameter_specification (:seq
                           _defining_identifier_list
                           ":"
                           (:choice
                            (:seq
                             (:choice
                              (:alias
                               (:token (:prec 2 (:pattern "[aA][lL][iI][aA][sS][eE][dD]")))
                               "aliased")
                              :blank)
                             (:choice non_empty_mode :blank)
                             (:choice null_exclusion :blank)
                             (:field :subtype_mark _name))
                            access_definition)
                           (:choice _assign_value :blank)
                           (:choice aspect_specification :blank))
  _parameter_specification_list (:seq
                                 parameter_specification
                                 (:repeat (:seq ";" parameter_specification)))
  pragma_g (:seq
            (:alias (:token (:prec 2 (:pattern "[pP][rR][aA][gG][mM][aA]"))) "pragma")
            identifier
            (:choice
             (:seq
              "("
              (:choice
               (:seq pragma_argument_association (:repeat (:seq "," pragma_argument_association)))
               _conditional_quantified_declare_expression)
              ")")
             :blank)
            ";")
  pragma_argument_association (:seq (:choice (:seq _aspect_mark "=>") :blank) expression)
  if_expression (:seq
                 (:alias (:token (:prec 2 (:pattern "[iI][fF]"))) "if")
                 (:field :condition expression)
                 (:alias (:token (:prec 2 (:pattern "[tT][hH][eE][nN]"))) "then")
                 expression
                 (:repeat elsif_expression_item)
                 (:choice
                  (:seq (:alias (:token (:prec 2 (:pattern "[eE][lL][sS][eE]"))) "else") expression)
                  :blank))
  elsif_expression_item (:seq
                         (:alias (:token (:prec 2 (:pattern "[eE][lL][sS][iI][fF]"))) "elsif")
                         (:field :condition expression)
                         (:alias (:token (:prec 2 (:pattern "[tT][hH][eE][nN]"))) "then")
                         expression)
  procedure_specification (:seq
                           (:alias
                            (:token (:prec 2 (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]")))
                            "procedure")
                           (:field :name _name)
                           (:choice formal_part :blank))
  record_representation_clause (:prec-left 0
                                (:seq
                                 (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
                                 (:field :local_name _name)
                                 (:alias (:token (:prec 2 (:pattern "[uU][sS][eE]"))) "use")
                                 (:alias
                                  (:token (:prec 2 (:pattern "[rR][eE][cC][oO][rR][dD]")))
                                  "record")
                                 (:choice mod_clause :blank)
                                 (:repeat component_clause)
                                 (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                                 (:alias
                                  (:token (:prec 2 (:pattern "[rR][eE][cC][oO][rR][dD]")))
                                  "record")
                                 (:choice (:field :end_local_name _name) :blank)
                                 ";"))
  _renaming_declaration (:choice
                         object_renaming_declaration
                         exception_renaming_declaration
                         package_renaming_declaration
                         subprogram_renaming_declaration
                         generic_renaming_declaration)
  object_renaming_declaration (:choice
                               (:seq
                                identifier
                                (:choice
                                 (:seq
                                  ":"
                                  (:choice null_exclusion :blank)
                                  (:field :subtype_mark _name))
                                 :blank)
                                (:alias
                                 (:token (:prec 2 (:pattern "[rR][eE][nN][aA][mM][eE][sS]")))
                                 "renames")
                                (:field :object_name _name)
                                (:choice aspect_specification :blank)
                                ";")
                               (:seq
                                identifier
                                ":"
                                access_definition
                                (:alias
                                 (:token (:prec 2 (:pattern "[rR][eE][nN][aA][mM][eE][sS]")))
                                 "renames")
                                (:field :object_name _name)
                                (:choice aspect_specification :blank)
                                ";"))
  exception_renaming_declaration (:seq
                                  identifier
                                  ":"
                                  (:alias
                                   (:token
                                    (:prec 2 (:pattern "[eE][xX][cC][eE][pP][tT][iI][oO][nN]")))
                                   "exception")
                                  (:alias
                                   (:token (:prec 2 (:pattern "[rR][eE][nN][aA][mM][eE][sS]")))
                                   "renames")
                                  (:field :exception_name _name)
                                  (:choice aspect_specification :blank)
                                  ";")
  package_renaming_declaration (:seq
                                (:alias
                                 (:token (:prec 2 (:pattern "[pP][aA][cC][kK][aA][gG][eE]")))
                                 "package")
                                (:field :name _name)
                                (:alias
                                 (:token (:prec 2 (:pattern "[rR][eE][nN][aA][mM][eE][sS]")))
                                 "renames")
                                (:field :package_name _name)
                                (:choice aspect_specification :blank)
                                ";")
  subprogram_renaming_declaration (:seq
                                   (:choice overriding_indicator :blank)
                                   _subprogram_specification
                                   (:alias
                                    (:token (:prec 2 (:pattern "[rR][eE][nN][aA][mM][eE][sS]")))
                                    "renames")
                                   (:field :callable_entity_name _name)
                                   (:choice aspect_specification :blank)
                                   ";")
  generic_renaming_declaration (:choice
                                (:seq
                                 (:alias
                                  (:token (:prec 2 (:pattern "[gG][eE][nN][eE][rR][iI][cC]")))
                                  "generic")
                                 (:alias
                                  (:token (:prec 2 (:pattern "[pP][aA][cC][kK][aA][gG][eE]")))
                                  "package")
                                 (:field :defining_program_unit_name _name)
                                 (:alias
                                  (:token (:prec 2 (:pattern "[rR][eE][nN][aA][mM][eE][sS]")))
                                  "renames")
                                 (:field :generic_package_name _name)
                                 (:choice aspect_specification :blank)
                                 ";")
                                (:seq
                                 (:alias
                                  (:token (:prec 2 (:pattern "[gG][eE][nN][eE][rR][iI][cC]")))
                                  "generic")
                                 (:alias
                                  (:token
                                   (:prec 2 (:pattern "[pP][rR][oO][cC][eE][dD][uU][rR][eE]")))
                                  "procedure")
                                 (:field :defining_program_unit_name _name)
                                 (:alias
                                  (:token (:prec 2 (:pattern "[rR][eE][nN][aA][mM][eE][sS]")))
                                  "renames")
                                 (:field :generic_procedure_name _name)
                                 (:choice aspect_specification :blank)
                                 ";")
                                (:seq
                                 (:alias
                                  (:token (:prec 2 (:pattern "[gG][eE][nN][eE][rR][iI][cC]")))
                                  "generic")
                                 (:alias
                                  (:token (:prec 2 (:pattern "[fF][uU][nN][cC][tT][iI][oO][nN]")))
                                  "function")
                                 (:field :defining_program_unit_name _name)
                                 (:alias
                                  (:token (:prec 2 (:pattern "[rR][eE][nN][aA][mM][eE][sS]")))
                                  "renames")
                                 (:field :generic_function_name _name)
                                 (:choice aspect_specification :blank)
                                 ";"))
  result_profile (:seq
                  (:alias (:token (:prec 2 (:pattern "[rR][eE][tT][uU][rR][nN]"))) "return")
                  (:choice
                   (:seq (:choice null_exclusion :blank) (:field :subtype_mark _name))
                   access_definition))
  _sequence_of_statements (:prec-left 0 (:seq (:repeat1 _statement) (:repeat label)))
  _simple_statement (:choice
                     null_statement
                     assignment_statement
                     exit_statement
                     goto_statement
                     procedure_call_statement
                     simple_return_statement
                     requeue_statement
                     _delay_statement
                     abort_statement
                     raise_statement
                     pragma_g)
  _statement (:seq (:repeat label) (:choice _simple_statement _compound_statement))
  _compound_statement (:choice
                       if_statement
                       gnatprep_if_statement
                       case_statement
                       loop_statement
                       block_statement
                       extended_return_statement
                       parallel_block_statement
                       accept_statement
                       _select_statement)
  _select_statement (:choice
                     selective_accept
                     timed_entry_call
                     conditional_entry_call
                     asynchronous_select)
  entry_call_alternative (:seq procedure_call_statement (:choice _sequence_of_statements :blank))
  asynchronous_select (:seq
                       (:alias (:token (:prec 2 (:pattern "[sS][eE][lL][eE][cC][tT]"))) "select")
                       triggering_alternative
                       (:alias (:token (:prec 2 (:pattern "[tT][hH][eE][nN]"))) "then")
                       (:alias (:token (:prec 2 (:pattern "[aA][bB][oO][rR][tT]"))) "abort")
                       (:field :abortable_part _sequence_of_statements)
                       (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                       (:alias (:token (:prec 2 (:pattern "[sS][eE][lL][eE][cC][tT]"))) "select")
                       ";")
  triggering_alternative (:choice
                          (:seq procedure_call_statement (:choice _sequence_of_statements :blank))
                          (:seq _delay_statement (:choice _sequence_of_statements :blank)))
  conditional_entry_call (:seq
                          (:alias (:token (:prec 2 (:pattern "[sS][eE][lL][eE][cC][tT]"))) "select")
                          entry_call_alternative
                          (:alias (:token (:prec 2 (:pattern "[eE][lL][sS][eE]"))) "else")
                          _sequence_of_statements
                          (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                          (:alias (:token (:prec 2 (:pattern "[sS][eE][lL][eE][cC][tT]"))) "select")
                          ";")
  delay_alternative (:seq _delay_statement (:choice _sequence_of_statements :blank))
  timed_entry_call (:seq
                    (:alias (:token (:prec 2 (:pattern "[sS][eE][lL][eE][cC][tT]"))) "select")
                    entry_call_alternative
                    (:alias (:token (:prec 2 (:pattern "[oO][rR]"))) "or")
                    delay_alternative
                    (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                    (:alias (:token (:prec 2 (:pattern "[sS][eE][lL][eE][cC][tT]"))) "select")
                    ";")
  guard (:seq
         (:alias (:token (:prec 2 (:pattern "[wW][hH][eE][nN]"))) "when")
         (:field :condition expression)
         "=>")
  select_alternative (:choice accept_alternative delay_alternative terminate_alternative)
  accept_alternative (:seq accept_statement (:choice _sequence_of_statements :blank))
  terminate_alternative (:seq
                         (:alias
                          (:token (:prec 2 (:pattern "[tT][eE][rR][mM][iI][nN][aA][tT][eE]")))
                          "terminate")
                         ";")
  selective_accept (:seq
                    (:alias (:token (:prec 2 (:pattern "[sS][eE][lL][eE][cC][tT]"))) "select")
                    (:seq
                     (:seq (:choice guard :blank) select_alternative)
                     (:repeat
                      (:seq
                       (:alias (:token (:prec 2 (:pattern "[oO][rR]"))) "or")
                       (:seq (:choice guard :blank) select_alternative))))
                    (:choice
                     (:seq
                      (:alias (:token (:prec 2 (:pattern "[eE][lL][sS][eE]"))) "else")
                      _sequence_of_statements)
                     :blank)
                    (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                    (:alias (:token (:prec 2 (:pattern "[sS][eE][lL][eE][cC][tT]"))) "select")
                    ";")
  abort_statement (:seq
                   (:alias (:token (:prec 2 (:pattern "[aA][bB][oO][rR][tT]"))) "abort")
                   (:seq _name (:repeat (:seq "," _name)))
                   ";")
  requeue_statement (:seq
                     (:alias (:token (:prec 2 (:pattern "[rR][eE][qQ][uU][eE][uU][eE]"))) "requeue")
                     (:field :name _name)
                     (:choice
                      (:seq
                       (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                       (:alias (:token (:prec 2 (:pattern "[aA][bB][oO][rR][tT]"))) "abort"))
                      :blank)
                     ";")
  accept_statement (:seq
                    (:alias (:token (:prec 2 (:pattern "[aA][cC][cC][eE][pP][tT]"))) "accept")
                    (:field :entry_direct_name identifier)
                    (:choice (:seq "(" (:field :entry_index expression) ")") :blank)
                    (:choice (:field :parameter_profile formal_part) :blank)
                    (:choice
                     (:seq
                      (:alias (:token (:prec 2 (:pattern "[dD][oO]"))) "do")
                      handled_sequence_of_statements
                      (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                      (:choice (:field :entry_identifier identifier) :blank))
                     :blank)
                    ";")
  case_statement_alternative (:seq
                              (:alias (:token (:prec 2 (:pattern "[wW][hH][eE][nN]"))) "when")
                              discrete_choice_list
                              "=>"
                              _sequence_of_statements)
  case_statement (:seq
                  (:alias (:token (:prec 2 (:pattern "[cC][aA][sS][eE]"))) "case")
                  expression
                  (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                  (:repeat1 case_statement_alternative)
                  (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                  (:alias (:token (:prec 2 (:pattern "[cC][aA][sS][eE]"))) "case")
                  ";")
  block_statement (:seq
                   (:choice loop_label :blank)
                   (:choice
                    (:seq
                     (:alias (:token (:prec 2 (:pattern "[dD][eE][cC][lL][aA][rR][eE]"))) "declare")
                     (:choice non_empty_declarative_part :blank))
                    :blank)
                   (:alias (:token (:prec 2 (:pattern "[bB][eE][gG][iI][nN]"))) "begin")
                   handled_sequence_of_statements
                   (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                   (:choice identifier :blank)
                   ";")
  parallel_block_statement (:seq
                            (:alias
                             (:token (:prec 2 (:pattern "[pP][aA][rR][aA][lL][lL][eE][lL]")))
                             "parallel")
                            (:choice (:seq "(" chunk_specification ")") :blank)
                            (:choice aspect_specification :blank)
                            (:alias (:token (:prec 2 (:pattern "[dD][oO]"))) "do")
                            (:field :statements _sequence_of_statements)
                            (:repeat1
                             (:seq
                              (:alias (:token (:prec 2 (:pattern "[aA][nN][dD]"))) "and")
                              (:field :statements _sequence_of_statements)))
                            (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                            (:alias (:token (:prec 2 (:pattern "[dD][oO]"))) "do")
                            ";")
  if_statement (:seq
                (:alias (:token (:prec 2 (:pattern "[iI][fF]"))) "if")
                (:field :condition expression)
                (:alias (:token (:prec 2 (:pattern "[tT][hH][eE][nN]"))) "then")
                (:field :statements _sequence_of_statements)
                (:repeat elsif_statement_item)
                (:choice
                 (:seq
                  (:alias (:token (:prec 2 (:pattern "[eE][lL][sS][eE]"))) "else")
                  (:field :else_statements _sequence_of_statements))
                 :blank)
                (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                (:alias (:token (:prec 2 (:pattern "[iI][fF]"))) "if")
                ";")
  elsif_statement_item (:seq
                        (:alias (:token (:prec 2 (:pattern "[eE][lL][sS][iI][fF]"))) "elsif")
                        (:field :condition expression)
                        (:alias (:token (:prec 2 (:pattern "[tT][hH][eE][nN]"))) "then")
                        (:field :statements _sequence_of_statements))
  gnatprep_declarative_if_statement (:seq
                                     (:alias (:token (:prec 2 (:pattern "[##][iI][fF]"))) "#if")
                                     (:field :condition expression)
                                     (:alias
                                      (:token (:prec 2 (:pattern "[tT][hH][eE][nN]")))
                                      "then")
                                     (:repeat _declarative_item_pragma)
                                     (:repeat
                                      (:seq
                                       (:alias
                                        (:token (:prec 2 (:pattern "[##][eE][lL][sS][iI][fF]")))
                                        "#elsif")
                                       (:field :condition expression)
                                       (:alias
                                        (:token (:prec 2 (:pattern "[tT][hH][eE][nN]")))
                                        "then")
                                       (:repeat _declarative_item_pragma)))
                                     (:choice
                                      (:seq
                                       (:alias
                                        (:token (:prec 2 (:pattern "[##][eE][lL][sS][eE]")))
                                        "#else")
                                       (:repeat _declarative_item_pragma))
                                      :blank)
                                     (:alias
                                      (:token (:prec 2 (:pattern "[##][eE][nN][dD]")))
                                      "#end")
                                     (:alias (:token (:prec 2 (:pattern "[iI][fF]"))) "if")
                                     ";")
  gnatprep_if_statement (:seq
                         (:alias (:token (:prec 2 (:pattern "[##][iI][fF]"))) "#if")
                         (:field :condition expression)
                         (:alias (:token (:prec 2 (:pattern "[tT][hH][eE][nN]"))) "then")
                         (:repeat _statement)
                         (:repeat
                          (:seq
                           (:alias
                            (:token (:prec 2 (:pattern "[##][eE][lL][sS][iI][fF]")))
                            "#elsif")
                           (:field :condition expression)
                           (:alias (:token (:prec 2 (:pattern "[tT][hH][eE][nN]"))) "then")
                           (:repeat _statement)))
                         (:choice
                          (:seq
                           (:alias (:token (:prec 2 (:pattern "[##][eE][lL][sS][eE]"))) "#else")
                           (:repeat _statement))
                          :blank)
                         (:alias (:token (:prec 2 (:pattern "[##][eE][nN][dD]"))) "#end")
                         (:alias (:token (:prec 2 (:pattern "[iI][fF]"))) "if")
                         ";")
  exit_statement (:seq
                  (:alias (:token (:prec 2 (:pattern "[eE][xX][iI][tT]"))) "exit")
                  (:field :loop_name (:choice _name :blank))
                  (:choice
                   (:seq
                    (:alias (:token (:prec 2 (:pattern "[wW][hH][eE][nN]"))) "when")
                    (:field :condition expression))
                   :blank)
                  ";")
  goto_statement (:seq
                  (:alias (:token (:prec 2 (:pattern "[gG][oO][tT][oO]"))) "goto")
                  (:field :label_name _name)
                  ";")
  _delay_statement (:choice delay_until_statement delay_relative_statement)
  delay_until_statement (:seq
                         (:alias (:token (:prec 2 (:pattern "[dD][eE][lL][aA][yY]"))) "delay")
                         (:alias (:token (:prec 2 (:pattern "[uU][nN][tT][iI][lL]"))) "until")
                         expression
                         ";")
  delay_relative_statement (:seq
                            (:alias (:token (:prec 2 (:pattern "[dD][eE][lL][aA][yY]"))) "delay")
                            expression
                            ";")
  simple_return_statement (:seq
                           (:alias
                            (:token (:prec 2 (:pattern "[rR][eE][tT][uU][rR][nN]")))
                            "return")
                           (:choice expression :blank)
                           ";")
  extended_return_statement (:seq
                             (:alias
                              (:token (:prec 2 (:pattern "[rR][eE][tT][uU][rR][nN]")))
                              "return")
                             extended_return_object_declaration
                             (:choice
                              (:seq
                               (:alias (:token (:prec 2 (:pattern "[dD][oO]"))) "do")
                               handled_sequence_of_statements
                               (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                               (:alias
                                (:token (:prec 2 (:pattern "[rR][eE][tT][uU][rR][nN]")))
                                "return"))
                              :blank)
                             ";")
  extended_return_object_declaration (:seq
                                      identifier
                                      ":"
                                      (:choice
                                       (:alias
                                        (:token (:prec 2 (:pattern "[aA][lL][iI][aA][sS][eE][dD]")))
                                        "aliased")
                                       :blank)
                                      (:choice
                                       (:alias
                                        (:token
                                         (:prec 2 (:pattern "[cC][oO][nN][sS][tT][aA][nN][tT]")))
                                        "constant")
                                       :blank)
                                      _return_subtype_indication
                                      (:choice _assign_value :blank)
                                      (:choice aspect_specification :blank))
  _return_subtype_indication (:choice _subtype_indication access_definition)
  procedure_call_statement (:choice
                            (:seq (:field :name _name_not_function_call) ";")
                            (:seq (:field :name _name) actual_parameter_part ";"))
  function_call (:seq (:field :name _name) actual_parameter_part)
  raise_statement (:seq
                   (:alias (:token (:prec 2 (:pattern "[rR][aA][iI][sS][eE]"))) "raise")
                   (:choice
                    (:seq
                     (:field :name _name)
                     (:choice
                      (:seq
                       (:alias (:token (:prec 2 (:pattern "[wW][iI][tT][hH]"))) "with")
                       expression)
                      :blank))
                    :blank)
                   ";")
  loop_statement (:seq
                  (:choice loop_label :blank)
                  (:choice iteration_scheme :blank)
                  (:alias (:token (:prec 2 (:pattern "[lL][oO][oO][pP]"))) "loop")
                  (:field :statements _sequence_of_statements)
                  (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                  (:alias (:token (:prec 2 (:pattern "[lL][oO][oO][pP]"))) "loop")
                  (:choice identifier :blank)
                  ";")
  procedural_iterator (:seq
                       iterator_parameter_specification
                       (:alias (:token (:prec 2 (:pattern "[oO][fF]"))) "of")
                       iterator_procedure_call
                       (:choice iterator_filter :blank))
  iterator_parameter_specification (:choice
                                    formal_part
                                    (:seq "(" (:seq identifier (:repeat (:seq "," identifier))) ")"))
  iterator_procedure_call (:seq
                           (:field :name _name_not_function_call)
                           (:choice actual_parameter_part :blank))
  iteration_scheme (:choice
                    (:seq
                     (:alias (:token (:prec 2 (:pattern "[wW][hH][iI][lL][eE]"))) "while")
                     (:field :condition expression))
                    (:seq
                     (:choice
                      (:seq
                       (:field :is_parallel
                        (:alias
                         (:token (:prec 2 (:pattern "[pP][aA][rR][aA][lL][lL][eE][lL]")))
                         "parallel"))
                       (:choice aspect_specification :blank))
                      :blank)
                     (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
                     procedural_iterator)
                    (:seq
                     (:choice
                      (:seq
                       (:field :is_parallel
                        (:alias
                         (:token (:prec 2 (:pattern "[pP][aA][rR][aA][lL][lL][eE][lL]")))
                         "parallel"))
                       (:choice (:seq "(" chunk_specification ")") :blank)
                       (:choice aspect_specification :blank))
                      :blank)
                     (:alias (:token (:prec 2 (:pattern "[fF][oO][rR]"))) "for")
                     (:choice loop_parameter_specification iterator_specification)))
  assignment_statement (:seq (:field :variable_name _name) _assign_value ";")
  subprogram_declaration (:seq
                          (:choice overriding_indicator :blank)
                          _subprogram_specification
                          (:field :is_abstract
                           (:choice
                            (:seq
                             (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                             (:alias
                              (:token (:prec 2 (:pattern "[aA][bB][sS][tT][rR][aA][cC][tT]")))
                              "abstract"))
                            :blank))
                          (:choice aspect_specification :blank)
                          ";")
  expression_function_declaration (:seq
                                   (:choice overriding_indicator :blank)
                                   function_specification
                                   (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                                   (:choice _aggregate _parenthesized_expression)
                                   (:choice aspect_specification :blank)
                                   ";")
  _subprogram_specification (:choice procedure_specification function_specification)
  subtype_declaration (:seq
                       (:alias
                        (:token (:prec 2 (:pattern "[sS][uU][bB][tT][yY][pP][eE]")))
                        "subtype")
                       identifier
                       (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                       _subtype_indication
                       (:choice aspect_specification :blank)
                       ";")
  variant_part (:seq
                (:alias (:token (:prec 2 (:pattern "[cC][aA][sS][eE]"))) "case")
                identifier
                (:alias (:token (:prec 2 (:pattern "[iI][sS]"))) "is")
                variant_list
                (:alias (:token (:prec 2 (:pattern "[eE][nN][dD]"))) "end")
                (:alias (:token (:prec 2 (:pattern "[cC][aA][sS][eE]"))) "case")
                ";")
  variant_list (:repeat1 variant)
  variant (:seq
           (:alias (:token (:prec 2 (:pattern "[wW][hH][eE][nN]"))) "when")
           discrete_choice_list
           "=>"
           component_list)}}
