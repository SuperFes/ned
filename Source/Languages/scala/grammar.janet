# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "scala"
 :word _alpha_identifier
 :extras [(:pattern "\\s") comment block_comment]
 :conflicts [[_if_rest]
             [repeat_pattern _simple_expression]
             [tuple_pattern _simple_expression]
             [tuple_pattern _simple_expression binding]
             [xml_element xml_pattern]
             [_simple_expression _xml_embedded_pattern]
             [_simple_expression _xml_repeat_pattern]
             [_infix_operand vararg]
             [tuple_type parameter_types]
             [_structural_body template_body]
             [capture_ref _simple_expression]
             [extension_definition _soft_identifier]
             [binding _simple_expression]
             [binding _type_identifier]
             [_type parameter_types]
             [parameter_types _colon_bindings]
             [parameter_types bindings]
             [while_expression _simple_expression]
             [if_expression]
             [match_expression]
             [_type_identifier ascription_expression]
             [_given_constructor _type_identifier]
             [instance_expression]
             [try_expression]
             [_if_body]
             [function_definition]
             [val_definition]
             [_dot_match_expression]
             [var_definition]
             [package_clause]
             [_object_definition]
             [given_definition]
             [extension_definition]
             [while_expression]
             [for_expression]
             [enum_definition]
             [_simple_expression lambda_expression]
             [_simple_expression _single_lambda_param]
             [_single_lambda_param self_type _type_identifier]
             [_single_lambda_param _type_identifier]
             [_class_definition]
             [_class_constructor]
             [_full_enum_def]
             [identifiers val_declaration]
             [class_parameters]
             [_type compound_type]
             [_type infix_type]
             [_type match_type]
             [_variant_type_parameter type_lambda]
             [name_and_type parameter]
             [_simple_expression _type_identifier]
             [_if_condition_paren _simple_expression]
             [block _braced_template_body1]
             [_simple_expression _braced_typed_lambda]
             [self_type _simple_expression _braced_typed_lambda]
             [_self_type_ascription _braced_typed_lambda]
             [binding _simple_expression _type_identifier]
             [class_parameter _type_identifier]
             [_block_statements _indentable_expression]
             [match_expression _simple_expression]
             [self_type _simple_expression]
             [named_tuple_type _colon_bindings]
             [_annotated_type binding]
             [self_type _type_identifier _simple_expression]
             [self_type _annotated_type _simple_expression]
             [binding _annotated_type _simple_expression]
             [_annotated_type _simple_expression]
             [generic_type _simple_expression]]
 :precedences [["mod" "soft_id"]
               ["new" "structural_type"]
               ["self_type" "lambda"]
               ["annotation" "applied_constructor_type"]
               ["constructor_application" "applied_constructor_type"]]
 :externals [_automatic_semicolon
             _indent
             _outdent
             _comma_outdent
             _simple_string_start
             _simple_string_middle
             _simple_multiline_string_start
             _interpolated_string_middle
             _interpolated_multiline_string_middle
             _raw_string_start
             _raw_string_middle
             _raw_string_multiline_middle
             _single_line_string_end
             _multiline_string_end
             "else"
             "catch"
             "finally"
             "extends"
             "derives"
             "with"
             block_comment
             _suppress_block_comment
             error_sentinel
             _colon_eol
             _postfix_op
             _postfix_star
             _floating_point_with_separators
             _end_keyword
             _control_tail_gate
             _xml_tag_start
             _erased_modifier
             _open_modifier
             _opaque_modifier
             _infix_modifier
             _tracked_modifier
             _transparent_modifier
             _inline_modifier
             _into_modifier
             _update_modifier
             _consume_modifier
             "uses"
             _op_left_or
             _op_left_xor
             _op_left_and
             _op_left_eq
             _op_left_rel
             _op_left_colon
             _op_left_add
             _op_left_mul
             _op_left_other
             _op_name
             _using_directive_start]
 :inline [_definition_pattern
          _pattern
          _semicolon
          _definition
          _param_type
          _identifier
          _postfix_expression_choice
          _infix_type_choice
          _param_value_type
          _simple_type
          literal
          _exprs_in_parens
          _argument_list
          _asterisk
          _super_identifier
          _this_identifier
          _non_null_literal
          _braced_template_body
          _indented_template_body
          _xml_node
          _xml_content
          _xml_pattern_content
          _structural_type
          _refinement]
 :supertypes [expression _definition _pattern]
 :reserved
 {:global ["abstract"
            "case"
            "class"
            "def"
            "do"
            "enum"
            "export"
            "extends"
            "false"
            "final"
            "finally"
            "for"
            "given"
            "implicit"
            "import"
            "lazy"
            "macro"
            (:token-immediate "match")
            "new"
            "null"
            "object"
            "override"
            "package"
            "private"
            "protected"
            "return"
            "sealed"
            "super"
            "this"
            "throw"
            "trait"
            "true"
            "try"
            "type"
            "val"
            "var"
            "while"
            "yield"
            ":"
            "="
            "<-"
            "<%"
            "#"
            "@"
            "=>>"
            "?=>"]}
 :rules
 {compilation_unit (:seq
                    (:choice _shebang :blank)
                    (:choice
                     (:seq
                      (:seq _top_level_definition (:repeat (:seq _semicolon _top_level_definition)))
                      (:choice _semicolon :blank))
                     :blank))
  _top_level_definition (:choice _definition (:choice expression do_while_expression))
  _definition (:choice
               given_definition
               extension_definition
               class_definition
               import_declaration
               export_declaration
               object_definition
               enum_definition
               trait_definition
               val_definition
               val_declaration
               var_definition
               var_declaration
               type_definition
               function_definition
               function_declaration
               package_clause
               package_object)
  enum_definition (:seq
                   (:repeat annotation)
                   (:choice modifiers :blank)
                   "enum"
                   _class_constructor
                   (:field :extend (:choice extends_clause :blank))
                   (:field :derive (:choice (:choice derives_clause uses_clause) :blank))
                   (:field :body enum_body)
                   (:choice _end_marker_named_tail :blank))
  _enum_block (:prec-left 0
               (:seq
                (:seq
                 (:choice enum_case_definitions expression _definition)
                 (:repeat (:seq _semicolon (:choice enum_case_definitions expression _definition))))
                (:choice _semicolon :blank)))
  enum_body (:choice
             (:prec-left 1
              (:seq
               (:choice ":" (:alias _colon_eol ":"))
               _indent
               (:choice self_type :blank)
               _enum_block
               _outdent))
             (:seq "{" (:choice self_type :blank) (:choice _enum_block :blank) "}"))
  enum_case_definitions (:seq
                         (:repeat annotation)
                         (:choice modifiers :blank)
                         "case"
                         (:choice
                          (:seq simple_enum_case (:repeat (:seq "," simple_enum_case)))
                          full_enum_case))
  simple_enum_case (:prec-left 0
                    (:seq
                     (:field :name _identifier)
                     (:field :extend (:choice extends_clause :blank))))
  full_enum_case (:seq (:field :name _identifier) _full_enum_def)
  _full_enum_def (:seq
                  (:field :type_parameters (:choice type_parameters :blank))
                  (:field :class_parameters (:repeat1 class_parameters))
                  (:field :extend (:choice extends_clause :blank)))
  package_clause (:seq
                  (:prec-right 0
                   (:seq
                    "package"
                    (:field :name package_identifier)
                    (:field :body (:choice template_body :blank))))
                  (:choice _end_marker_named_tail :blank))
  package_identifier (:prec-right 0 (:seq _identifier (:repeat (:seq "." _identifier))))
  package_object (:seq "package" "object" _object_definition)
  import_declaration (:prec-left 0
                      (:seq
                       "import"
                       (:seq _namespace_expression (:repeat (:seq "," _namespace_expression)))))
  export_declaration (:prec-left 0
                      (:seq
                       "export"
                       (:seq _namespace_expression (:repeat (:seq "," _namespace_expression)))))
  _namespace_expression (:prec-left 0
                         (:choice
                          (:seq
                           (:field :path
                            (:seq
                             _namespace_path_segment
                             (:repeat (:seq "." _namespace_path_segment))))
                           (:choice
                            (:seq
                             "."
                             (:choice
                              _namespace_given_by_type
                              namespace_wildcard
                              namespace_selectors
                              as_renamed_identifier))
                            :blank))
                          as_renamed_identifier))
  _namespace_path_segment (:choice _identifier (:alias (:choice "enum" "export") identifier))
  namespace_wildcard (:prec-left 1 (:choice "*" "_" "given"))
  _namespace_given_by_type (:seq "given" _type)
  namespace_selectors (:seq
                       "{"
                       (:seq
                        (:seq
                         (:choice
                          _namespace_given_by_type
                          namespace_wildcard
                          _identifier
                          arrow_renamed_identifier
                          as_renamed_identifier)
                         (:repeat
                          (:seq
                           ","
                           (:choice
                            _namespace_given_by_type
                            namespace_wildcard
                            _identifier
                            arrow_renamed_identifier
                            as_renamed_identifier))))
                        (:choice "," :blank))
                       "}")
  _import_selectors (:alias namespace_selectors import_selectors)
  arrow_renamed_identifier (:seq
                            (:field :name _identifier)
                            (:choice "=>" (:alias "⇒" "=>"))
                            (:field :alias (:choice _identifier wildcard)))
  as_renamed_identifier (:seq
                         (:field :name _identifier)
                         "as"
                         (:field :alias (:choice _identifier wildcard)))
  object_definition (:seq
                     (:repeat annotation)
                     (:choice modifiers :blank)
                     (:choice "case" :blank)
                     "object"
                     _object_definition)
  _object_definition (:seq
                      (:prec-left 0
                       (:seq
                        (:field :name _identifier)
                        (:field :extend (:choice extends_clause :blank))
                        (:field :derive (:choice (:choice derives_clause uses_clause) :blank))
                        (:field :body (:choice _definition_body :blank))))
                      (:choice _end_marker_named_tail :blank))
  class_definition (:seq
                    (:repeat annotation)
                    (:choice modifiers :blank)
                    (:choice "case" :blank)
                    "class"
                    _class_definition)
  _class_definition (:seq
                     _class_constructor
                     (:field :extend (:choice extends_clause :blank))
                     (:field :derive (:choice (:choice derives_clause uses_clause) :blank))
                     (:field :body (:choice _definition_body :blank))
                     (:choice _end_marker_named_tail :blank))
  _definition_body (:prec-dynamic 1
                    (:seq (:choice _automatic_semicolon :blank) (:field :body template_body)))
  _class_constructor (:seq
                      (:field :name _identifier)
                      (:field :type_parameters (:choice type_parameters :blank))
                      (:choice (:alias _constructor_annotation annotation) :blank)
                      (:choice access_modifier :blank)
                      (:field :class_parameters
                       (:repeat (:seq (:choice _automatic_semicolon :blank) class_parameters))))
  trait_definition (:prec-left 0
                    (:seq (:repeat annotation) (:choice modifiers :blank) "trait" _class_definition))
  type_parameters (:seq
                   "["
                   (:seq
                    (:seq _variant_type_parameter (:repeat (:seq "," _variant_type_parameter)))
                    (:choice "," :blank))
                   "]")
  _variant_type_parameter (:seq
                           (:repeat annotation)
                           (:choice
                            covariant_type_parameter
                            contravariant_type_parameter
                            _type_parameter
                            type_lambda))
  covariant_type_parameter (:seq "+" _type_parameter)
  contravariant_type_parameter (:seq "-" _type_parameter)
  _type_parameter (:seq
                   (:field :name (:choice wildcard _identifier))
                   (:choice (:alias "^" capture_variable) :blank)
                   (:field :type_parameters (:choice type_parameters :blank))
                   (:field :bound (:choice lower_bound :blank))
                   (:field :bound (:choice upper_bound :blank))
                   (:field :bound (:choice (:repeat view_bound) :blank))
                   (:field :bound (:choice _context_bounds :blank)))
  upper_bound (:seq "<:" (:field :type (:choice _type capture_set)))
  lower_bound (:seq ">:" (:field :type (:choice _type capture_set)))
  view_bound (:seq "<%" (:field :type _type))
  _context_bounds (:choice
                   (:repeat1 (:seq ":" context_bound))
                   (:seq
                    ":"
                    "{"
                    (:seq
                     (:seq context_bound (:repeat (:seq "," context_bound)))
                     (:choice "," :blank))
                    "}"))
  context_bound (:seq (:field :type _type) (:choice (:seq "as" (:field :name _identifier)) :blank))
  template_body (:choice _indented_template_body _braced_template_body)
  _indented_template_body (:prec-left 1
                           (:seq
                            (:choice ":" (:alias _colon_eol ":"))
                            _indent
                            (:choice (:seq (:choice self_type :blank) _block) self_type)
                            (:choice _outdent _comma_outdent)))
  _braced_template_body (:prec-left 1
                         (:seq
                          "{"
                          (:choice (:choice _braced_template_body1 _braced_template_body2) :blank)
                          "}"))
  _braced_template_body1 (:choice (:seq (:choice self_type :blank) _block) self_type)
  _braced_template_body2 (:seq
                          (:choice
                           (:seq _indent (:choice self_type :blank))
                           (:seq (:choice self_type :blank) _indent))
                          (:choice _block :blank)
                          _outdent
                          (:choice _block :blank))
  with_template_body (:choice
                      (:prec-left 1 (:seq _indent (:choice self_type :blank) _block _outdent))
                      (:seq "{" (:choice _block :blank) "}"))
  _extension_template_body (:choice
                            (:prec-left 1 (:seq _indent _block _outdent))
                            (:seq "{" (:choice _block :blank) "}"))
  _end_marker_named (:seq
                     _end_keyword
                     (:choice
                      "val"
                      "given"
                      "this"
                      (:alias (:choice _alpha_identifier _backquoted_id) "_end_ident")))
  _end_marker_named_tail (:prec-dynamic 1
                          (:seq _automatic_semicolon (:alias _end_marker_named end_marker)))
  _end_marker_kw_tail (:prec-dynamic 2
                       (:seq _automatic_semicolon (:alias _end_marker_kw end_marker)))
  _end_marker_kw (:seq _end_keyword (:choice "extension" "if" "while" "for" "match" "try" "new"))
  self_type (:prec-dynamic 2
             (:prec "self_type"
              (:seq
               (:choice
                (:choice
                 (:alias _alpha_identifier identifier)
                 (:alias _backquoted_id identifier)
                 (:alias _soft_identifier identifier)
                 (:alias "this" identifier)
                 (:alias "super" identifier))
                operator_identifier
                wildcard)
               (:choice _self_type_ascription :blank)
               (:choice "=>" (:alias "⇒" "=>")))))
  _self_type_ascription (:seq ":" _type)
  annotation (:prec-right "annotation"
              (:seq
               "@"
               (:field :name _simple_type)
               (:field :arguments (:repeat (:prec "annotation" arguments)))))
  _constructor_annotation (:prec "annotation"
                           (:seq
                            "@"
                            (:field :name _simple_type)
                            (:choice
                             (:alias
                              (:seq (:token-immediate "(") (:choice _exprs_in_parens :blank) ")")
                              arguments)
                             :blank)))
  val_definition (:seq
                  _start_val
                  (:field :pattern (:choice _definition_pattern identifiers))
                  (:choice (:seq ":" (:field :type _type)) :blank)
                  "="
                  (:field :value _indentable_expression)
                  (:choice _end_marker_named_tail :blank))
  val_declaration (:seq
                   _start_val
                   (:seq (:field :name _identifier) (:repeat (:seq "," (:field :name _identifier))))
                   ":"
                   (:field :type _type))
  _start_val (:seq (:repeat annotation) (:choice modifiers :blank) "val")
  var_declaration (:seq
                   _start_var
                   (:seq (:field :name _identifier) (:repeat (:seq "," (:field :name _identifier))))
                   ":"
                   (:field :type _type))
  var_definition (:seq
                  _start_var
                  (:field :pattern (:choice _definition_pattern identifiers))
                  (:choice (:seq ":" (:field :type _type)) :blank)
                  "="
                  (:field :value _indentable_expression)
                  (:choice _end_marker_named_tail :blank))
  _start_var (:seq (:repeat annotation) (:choice modifiers :blank) "var")
  type_definition (:prec-left 0
                   (:seq
                    (:repeat annotation)
                    (:choice modifiers :blank)
                    (:choice (:alias _opaque_modifier opaque_modifier) :blank)
                    "type"
                    _type_constructor
                    (:choice (:seq "=" (:field :type (:choice _type capture_set))) :blank)))
  _type_constructor (:prec-left 0
                     (:seq
                      (:field :name _type_identifier)
                      (:choice (:alias "^" capture_variable) :blank)
                      (:field :type_parameters (:choice type_parameters :blank))
                      (:field :bound (:choice lower_bound :blank))
                      (:field :bound (:choice upper_bound :blank))
                      (:field :bound (:choice _context_bounds :blank))))
  function_definition (:seq
                       _function_declaration
                       (:choice
                        (:seq "=" (:field :body _indentable_expression))
                        (:field :body block))
                       (:choice _end_marker_named_tail :blank))
  function_declaration _function_declaration
  _function_declaration (:prec-left 0
                         (:seq
                          (:repeat annotation)
                          (:choice modifiers :blank)
                          "def"
                          _function_constructor
                          (:choice (:seq ":" (:field :return_type _type)) :blank)))
  _function_constructor (:prec-right 0
                         (:seq
                          (:field :name _identifier)
                          (:field :parameters
                           (:repeat
                            (:seq
                             (:choice _automatic_semicolon :blank)
                             (:choice parameters type_parameters))))
                          (:choice _automatic_semicolon :blank)))
  extension_definition (:prec-dynamic 1
                        (:seq
                         (:prec-left 0
                          (:seq
                           "extension"
                           (:field :type_parameters (:choice type_parameters :blank))
                           (:field :parameters (:repeat parameters))
                           (:field :body
                            (:choice
                             _extension_template_body
                             function_definition
                             function_declaration))))
                         (:choice _end_marker_kw_tail :blank)))
  given_definition (:seq
                    (:prec-left 0
                     (:seq (:repeat annotation) (:choice modifiers :blank) "given" _given_tail))
                    (:choice _end_marker_named_tail :blank))
  _given_type_and_body (:choice
                        (:field :return_type _structural_instance)
                        (:seq
                         (:field :return_type
                          (:choice
                           _annotated_type
                           (:prec-dynamic -1 literal_type)
                           (:prec-dynamic -1 infix_type)))
                         (:choice _given_body :blank)))
  _given_tail (:seq (:choice _given_constructor :blank) (:repeat _given_sig) _given_type_and_body)
  _given_body (:choice
               (:seq "=" (:field :body _indentable_expression))
               (:prec-dynamic -1 (:field :body template_body)))
  _given_sig (:seq _given_conditional (:choice "=>" (:alias "⇒" "=>")))
  _given_conditional (:choice (:alias parameters given_conditional) type_parameters)
  _given_constructor (:prec-right 0
                      (:seq
                       (:field :name (:choice _identifier :blank))
                       (:field :type_parameters (:choice type_parameters :blank))
                       (:field :parameters
                        (:repeat (:seq (:choice _automatic_semicolon :blank) parameters)))
                       (:choice _automatic_semicolon :blank)
                       ":"))
  _structural_instance (:prec-left 17
                        (:choice
                         (:seq
                          _constructor_application
                          (:choice (:choice ":" (:alias _colon_eol ":")) "with")
                          (:field :body with_template_body))
                         (:seq
                          _constructor_application
                          (:repeat1
                           (:seq
                            (:choice "with" ",")
                            (:field :extra _constructor_application_extra))))))
  _constructor_application_extra (:prec-left "constructor_application"
                                  (:choice
                                   _annotated_type
                                   compound_type
                                   (:seq
                                    _simple_type
                                    (:field :arguments
                                     (:repeat1 (:prec "constructor_application" arguments))))))
  _constructor_application (:prec-left "constructor_application"
                            (:choice
                             _annotated_type
                             compound_type
                             _structural_type
                             (:seq
                              _simple_type
                              (:field :arguments
                               (:repeat1 (:prec "constructor_application" arguments))))))
  _constructor_applications (:prec-left 0
                             (:choice
                              (:seq
                               _constructor_application
                               (:repeat (:seq "," _constructor_application)))
                              (:seq
                               _constructor_application
                               (:repeat (:seq "with" _constructor_application)))))
  modifiers (:prec-left 0
             (:repeat1
              (:prec-left 0
               (:choice
                "abstract"
                "final"
                "sealed"
                "implicit"
                "lazy"
                "override"
                access_modifier
                (:alias _inline_modifier inline_modifier)
                (:alias _erased_modifier erased_modifier)
                (:alias _infix_modifier infix_modifier)
                (:alias _into_modifier into_modifier)
                (:alias _open_modifier open_modifier)
                (:alias _tracked_modifier tracked_modifier)
                (:alias _transparent_modifier transparent_modifier)
                (:alias _update_modifier update_modifier)
                (:alias _consume_modifier consume_modifier)))))
  access_modifier (:prec-left 0
                   (:seq (:choice "private" "protected") (:choice access_qualifier :blank)))
  access_qualifier (:seq "[" _identifier "]")
  extends_clause (:prec-left 0 (:seq "extends" (:field :type _constructor_applications)))
  _derived_names (:seq
                  (:field :type (:choice _type_identifier stable_type_identifier))
                  (:repeat
                   (:seq "," (:field :type (:choice _type_identifier stable_type_identifier)))))
  derives_clause (:prec-left 0 (:seq "derives" _derived_names))
  uses_clause (:prec-left 0 (:seq "uses" _derived_names))
  class_parameters (:prec 1
                    (:seq
                     (:choice _automatic_semicolon :blank)
                     "("
                     (:choice
                      (:seq
                       "using"
                       (:choice
                        (:seq
                         (:seq class_parameter (:repeat (:seq "," class_parameter)))
                         (:choice "," :blank))
                        (:seq
                         (:seq _param_type (:repeat (:seq "," _param_type)))
                         (:choice "," :blank))))
                      (:seq
                       (:choice "implicit" :blank)
                       (:choice
                        (:seq
                         (:seq class_parameter (:repeat (:seq "," class_parameter)))
                         (:choice "," :blank))
                        :blank)))
                     ")"))
  _parameters_tail (:seq
                    (:choice
                     (:seq (:seq parameter (:repeat (:seq "," parameter))) (:choice "," :blank))
                     :blank)
                    ")")
  parameters (:choice
              (:seq "(" (:choice "implicit" :blank) _parameters_tail)
              _using_parameters_clause)
  _using_parameters_clause (:seq
                            "("
                            "using"
                            (:choice
                             (:seq
                              (:seq parameter (:repeat (:seq "," parameter)))
                              (:choice "," :blank))
                             (:seq
                              (:seq _param_type (:repeat (:seq "," _param_type)))
                              (:choice "," :blank)))
                            ")")
  class_parameter (:seq
                   (:repeat annotation)
                   (:choice modifiers :blank)
                   (:choice (:choice "val" "var") :blank)
                   (:field :name _identifier)
                   (:choice (:seq ":" (:field :type _param_type)) :blank)
                   (:choice (:seq "=" (:field :default_value expression)) :blank))
  parameter (:prec-left 1
             (:seq
              (:repeat annotation)
              (:choice (:alias _inline_modifier inline_modifier) :blank)
              (:choice
               (:choice
                (:alias _erased_modifier erased_modifier)
                (:alias _consume_modifier consume_modifier))
               :blank)
              (:choice (:alias _tracked_modifier tracked_modifier) :blank)
              (:choice (:choice "val" "var") :blank)
              (:field :name _identifier)
              ":"
              (:field :type _param_type)
              (:choice (:seq "=" (:field :default_value expression)) :blank)))
  name_and_type (:prec-left 1
                 (:seq
                  (:choice (:alias _erased_modifier erased_modifier) :blank)
                  (:field :name _identifier)
                  (:choice ":" (:alias _colon_eol ":"))
                  (:field :type _param_type)))
  _block (:prec-left 0
          (:choice (:seq (:repeat1 ";") (:choice _block_statements :blank)) _block_statements))
  _semis (:seq _semicolon (:repeat ";"))
  _block_statements (:prec-left 0
                     (:seq
                      (:seq
                       (:choice (:choice expression do_while_expression) _definition)
                       (:repeat
                        (:seq _semis (:choice (:choice expression do_while_expression) _definition))))
                      (:choice _semis :blank)))
  _indentable_expression (:prec-right 0
                          (:choice
                           indented_block
                           indented_cases
                           (:choice expression do_while_expression)))
  block (:seq
         "{"
         (:choice (:choice _block (:alias _block_lambda_expression lambda_expression)) :blank)
         "}")
  indented_block (:prec-left 1
                  (:seq
                   _indent
                   (:choice _block (:alias _indented_block_lambda lambda_expression))
                   (:choice _outdent _comma_outdent)))
  indented_cases (:prec-left 0
                  (:seq _indent (:repeat1 case_clause) (:choice _outdent _comma_outdent)))
  _type (:choice
         function_type
         compound_type
         capturing_type
         infix_type
         match_type
         _annotated_type
         literal_type
         _structural_type
         type_lambda
         existential_type)
  existential_type (:prec-left 0 (:seq (:field :type _infix_type_choice) "forSome" _refinement))
  _annotated_type (:prec-right 0 (:choice annotated_type _simple_type))
  annotated_type (:prec-right 0 (:seq (:choice _simple_type literal_type) (:repeat1 annotation)))
  _simple_type (:choice
                generic_type
                projected_type
                tuple_type
                named_tuple_type
                singleton_type
                stable_type_identifier
                _type_identifier
                applied_constructor_type
                wildcard)
  applied_constructor_type (:prec "applied_constructor_type"
                            (:seq
                             (:choice
                              generic_type
                              projected_type
                              stable_type_identifier
                              _type_identifier)
                             arguments))
  compound_type (:choice
                 (:prec-left 17
                  (:seq
                   (:field :base _annotated_type)
                   (:repeat1 (:seq "with" (:field :extra _annotated_type)))))
                 (:prec-left 0
                  (:seq (:field :base (:choice _annotated_type compound_type)) _refinement))
                 (:prec-left -1
                  (:seq
                   (:prec-left 17
                    (:seq
                     (:field :base _annotated_type)
                     (:repeat1 (:seq "with" (:field :extra _annotated_type)))))
                   _refinement)))
  capturing_type (:choice
                  (:prec-left 2
                   (:seq
                    (:field :base (:choice _annotated_type compound_type))
                    "^"
                    (:field :capture_set capture_set)))
                  (:prec-left 1 (:seq (:field :base (:choice _annotated_type compound_type)) "^")))
  capture_set (:seq "{" (:choice (:seq capture_ref (:repeat (:seq "," capture_ref))) :blank) "}")
  capture_ref (:seq
               (:choice _identifier stable_identifier "cap")
               (:repeat
                (:seq
                 "."
                 (:choice
                  (:seq (:choice "only" "except") (:field :type_arguments type_arguments))
                  "rd"))))
  _structural_type (:prec "structural_type" (:alias _structural_body structural_type))
  _structural_body (:prec-dynamic -1 _braced_template_body)
  _refinement (:alias template_body refinement)
  _infix_type_choice (:prec-left 0
                      (:choice compound_type capturing_type infix_type _annotated_type literal_type))
  infix_type (:prec-left 0
              (:seq
               (:field :left (:choice _infix_type_choice _structural_type))
               (:field :operator
                (:choice
                 (:alias (:choice _alpha_identifier _backquoted_id) identifier)
                 (:alias _op_name operator_identifier)))
               (:field :right (:choice _infix_type_choice _structural_type))))
  tuple_type (:seq "(" (:seq (:seq _type (:repeat (:seq "," _type))) (:choice "," :blank)) ")")
  named_tuple_type (:seq
                    "("
                    (:seq
                     (:seq name_and_type (:repeat (:seq "," name_and_type)))
                     (:choice "," :blank))
                    ")")
  singleton_type (:prec-left 2 (:seq (:choice _identifier stable_identifier) "." "type"))
  stable_type_identifier (:prec-left 2
                          (:seq (:choice _identifier stable_identifier) "." _type_identifier))
  stable_identifier (:prec-left 4 (:seq (:choice _identifier stable_identifier) "." _identifier))
  generic_type (:seq (:field :type _simple_type) (:field :type_arguments type_arguments))
  projected_type (:seq (:field :type _simple_type) "#" (:field :selector _type_identifier))
  match_type (:prec-left 0
              (:seq
               _infix_type_choice
               "match"
               (:choice
                (:seq _indent (:repeat1 type_case_clause) _outdent)
                (:seq "{" (:repeat1 type_case_clause) "}"))))
  type_case_clause (:prec-left 1
                    (:seq
                     "case"
                     _infix_type_choice
                     (:field :body _arrow_then_type)
                     (:choice ";" :blank)))
  function_type (:prec-dynamic -1
                 (:prec-left 0
                  (:choice
                   (:seq (:field :type_parameters type_parameters) _arrow_then_type)
                   (:seq (:field :parameter_types parameter_types) _arrow_then_type))))
  _arrow_then_type (:prec-right 0
                    (:seq
                     (:choice (:choice "=>" (:alias "⇒" "=>")) "?=>" (:choice "->" "?->"))
                     (:choice (:field :capture_set capture_set) :blank)
                     (:field :return_type _type)))
  parameter_types (:choice
                   _annotated_type
                   capturing_type
                   (:prec-dynamic 1
                    (:seq
                     "("
                     (:choice
                      (:seq
                       (:seq _param_type (:repeat (:seq "," _param_type)))
                       (:choice "," :blank))
                      :blank)
                     ")"))
                   compound_type
                   infix_type)
  _param_type (:choice lazy_parameter_type _param_value_type)
  _param_value_type (:choice (:field :type _type) repeated_parameter_type)
  repeated_parameter_type (:seq (:field :type _type) _asterisk)
  lazy_parameter_type (:seq
                       (:choice (:choice "=>" (:alias "⇒" "=>")) (:choice "->" "?->"))
                       (:choice (:field :capture_set capture_set) :blank)
                       (:field :type _param_value_type))
  _type_identifier (:alias _identifier type_identifier)
  type_lambda (:seq
               "["
               (:seq
                (:seq _type_parameter (:repeat (:seq "," _type_parameter)))
                (:choice "," :blank))
               "]"
               "=>>"
               (:field :return_type _type))
  _pattern (:choice _definition_pattern alternative_pattern typed_pattern repeat_pattern)
  _definition_pattern (:choice
                       _identifier
                       stable_identifier
                       interpolated_string_expression
                       capture_pattern
                       tuple_pattern
                       named_tuple_pattern
                       case_class_pattern
                       infix_pattern
                       given_pattern
                       quote_expression
                       literal
                       unit
                       wildcard
                       xml_pattern)
  case_class_pattern (:seq
                      (:field :type (:choice _type_identifier stable_type_identifier))
                      (:choice (:field :type_arguments type_arguments) :blank)
                      "("
                      (:choice
                       (:field :pattern
                        (:choice
                         (:seq (:seq _pattern (:repeat (:seq "," _pattern))) (:choice "," :blank))
                         :blank))
                       (:field :pattern
                        (:choice
                         (:seq
                          (:seq named_pattern (:repeat (:seq "," named_pattern)))
                          (:choice "," :blank))
                         :blank)))
                      ")")
  infix_pattern (:prec-left 12
                 (:seq
                  (:field :left _definition_pattern)
                  (:field :operator
                   (:choice
                    (:alias (:choice _alpha_identifier _backquoted_id) identifier)
                    (:alias _op_name operator_identifier)))
                  (:field :right _definition_pattern)))
  capture_pattern (:prec-right 18
                   (:seq
                    (:field :name (:choice _identifier wildcard))
                    "@"
                    (:field :pattern _pattern)))
  repeat_pattern (:prec-right 0 (:seq (:field :pattern _pattern) _asterisk))
  typed_pattern (:prec-right -1 (:seq (:field :pattern _pattern) ":" (:field :type _type)))
  given_pattern (:seq "given" (:field :type _type))
  alternative_pattern (:prec-left -2 (:seq _pattern "|" _pattern))
  tuple_pattern (:seq
                 "("
                 (:seq (:seq _pattern (:repeat (:seq "," _pattern))) (:choice "," :blank))
                 ")")
  named_pattern (:prec-left -1 (:seq _identifier "=" _pattern))
  named_tuple_pattern (:seq
                       "("
                       (:seq
                        (:seq named_pattern (:repeat (:seq "," named_pattern)))
                        (:choice "," :blank))
                       ")")
  expression (:choice
              if_expression
              match_expression
              try_expression
              assignment_expression
              lambda_expression
              postfix_expression
              ascription_expression
              _infix_operand
              return_expression
              throw_expression
              while_expression
              for_expression
              macro_body)
  _infix_operand (:choice infix_expression prefix_expression _simple_expression)
  _simple_expression (:choice
                      (:choice
                       (:alias _alpha_identifier identifier)
                       (:alias _backquoted_id identifier)
                       (:alias _soft_identifier identifier)
                       (:alias "this" identifier)
                       (:alias "super" identifier))
                      operator_identifier
                      literal
                      interpolated_string_expression
                      unit
                      tuple_expression
                      wildcard
                      block
                      splice_expression
                      case_block
                      quote_expression
                      instance_expression
                      parenthesized_expression
                      field_expression
                      generic_function
                      call_expression
                      xml_expression
                      method_value
                      (:alias _dot_match_expression match_expression))
  method_value (:prec-left 18 (:seq _simple_expression wildcard))
  _single_lambda_param (:prec-right 0
                        (:seq
                         (:choice "implicit" :blank)
                         (:choice
                          (:choice
                           (:alias _alpha_identifier identifier)
                           (:alias _backquoted_id identifier)
                           (:alias _soft_identifier identifier)
                           (:alias "this" identifier)
                           (:alias "super" identifier))
                          operator_identifier)))
  lambda_expression (:prec-dynamic 1
                     (:prec-right "lambda"
                      (:seq
                       (:choice
                        (:seq
                         (:field :type_parameters type_parameters)
                         (:choice "=>" (:alias "⇒" "=>")))
                        :blank)
                       (:field :parameters (:choice bindings wildcard _single_lambda_param))
                       (:choice (:choice "=>" (:alias "⇒" "=>")) "?=>")
                       _indentable_expression)))
  _block_lambda_expression (:prec-right "lambda"
                            (:choice
                             (:seq
                              (:field :parameters (:choice bindings wildcard _single_lambda_param))
                              (:choice (:choice "=>" (:alias "⇒" "=>")) "?=>")
                              (:choice
                               (:choice _block (:alias _braced_typed_lambda lambda_expression))
                               :blank))
                             _braced_typed_lambda))
  _indented_block_lambda (:prec-right "lambda"
                          (:seq
                           (:field :parameters (:choice bindings wildcard _single_lambda_param))
                           (:choice (:choice "=>" (:alias "⇒" "=>")) "?=>")
                           (:choice _block :blank)))
  _braced_typed_lambda (:prec-dynamic 1
                        (:prec-right "lambda"
                         (:seq
                          (:field :parameters
                           (:prec-right 0
                            (:seq
                             (:choice "implicit" :blank)
                             (:choice
                              (:choice
                               (:alias _alpha_identifier identifier)
                               (:alias _backquoted_id identifier)
                               (:alias _soft_identifier identifier)
                               (:alias "this" identifier)
                               (:alias "super" identifier))
                              operator_identifier)
                             ":"
                             _type)))
                          (:choice (:choice "=>" (:alias "⇒" "=>")) "?=>")
                          _indentable_expression)))
  if_expression (:seq (:choice (:alias _inline_modifier inline_modifier) :blank) _if_rest)
  _if_rest (:seq
            "if"
            (:choice
             (:seq
              (:field :condition _if_condition_paren)
              (:choice _automatic_semicolon :blank)
              _if_body)
             (:seq
              (:field :condition _if_condition_then)
              _if_body
              (:choice _end_marker_kw_tail :blank))))
  _if_body (:seq
            (:field :consequence _indentable_expression)
            (:choice
             (:prec-dynamic 1
              (:seq
               (:choice ";" :blank)
               _control_tail_gate
               "else"
               (:field :alternative _indentable_expression)))
             :blank))
  _if_condition_paren (:prec-dynamic 4 parenthesized_expression)
  _if_condition_then (:prec-dynamic 5
                      (:seq
                       _indentable_expression
                       (:choice _automatic_semicolon :blank)
                       _control_tail_gate
                       "then"))
  match_expression (:choice
                    (:seq
                     (:choice (:alias _inline_modifier inline_modifier) :blank)
                     (:field :value expression)
                     "match"
                     (:choice
                      (:field :body case_block)
                      (:seq (:field :body indented_cases) (:choice _end_marker_kw_tail :blank))))
                    _dot_match_expression)
  _dot_match_expression (:seq
                         (:field :value _simple_expression)
                         "."
                         (:token-immediate "match")
                         (:field :body (:choice case_block indented_cases))
                         (:choice _end_marker_kw_tail :blank))
  try_expression (:seq
                  (:prec-right 1
                   (:seq
                    "try"
                    (:field :body _indentable_expression)
                    (:choice catch_clause :blank)
                    (:choice finally_clause :blank)))
                  (:choice _end_marker_kw_tail :blank))
  catch_clause (:prec-right 0
                (:seq _control_tail_gate "catch" (:choice _indentable_expression _expr_case_clause)))
  _expr_case_clause (:prec-left 0
                     (:seq "case" _case_pattern (:field :body (:choice expression indented_block))))
  finally_clause (:prec-right 0 (:seq _control_tail_gate "finally" _indentable_expression))
  binding (:seq
           (:choice (:alias _erased_modifier erased_modifier) :blank)
           (:choice
            (:field :name
             (:choice
              (:choice
               (:alias _alpha_identifier identifier)
               (:alias _backquoted_id identifier)
               (:alias _soft_identifier identifier)
               (:alias "this" identifier)
               (:alias "super" identifier))
              operator_identifier))
            wildcard)
           (:choice (:seq (:choice ":" (:alias _colon_eol ":")) (:field :type _param_type)) :blank))
  bindings (:prec-dynamic 2
            (:seq
             "("
             (:choice
              (:seq (:seq binding (:repeat (:seq "," binding))) (:choice "," :blank))
              :blank)
             ")"))
  case_block (:choice (:prec -1 (:seq "{" "}")) (:seq "{" (:repeat1 case_clause) "}"))
  case_clause (:prec-left 0 (:seq "case" _case_pattern (:field :body (:choice _block :blank))))
  _case_pattern (:prec-dynamic 1
                 (:seq
                  (:field :pattern _pattern)
                  (:choice guard :blank)
                  (:choice "=>" (:alias "⇒" "=>"))))
  guard (:prec-left 1 (:seq "if" (:field :condition _postfix_expression_choice)))
  assignment_expression (:prec-right 3
                         (:seq
                          (:field :left (:choice prefix_expression _simple_expression))
                          "="
                          (:field :right
                           (:choice (:choice expression do_while_expression) indented_block))))
  generic_function (:prec 18
                    (:seq (:field :function expression) (:field :type_arguments type_arguments)))
  call_expression (:choice
                   (:prec-left 18
                    (:seq
                     (:field :function _simple_expression)
                     (:field :arguments (:choice arguments case_block block))))
                   (:prec-right 5
                    (:seq
                     (:field :function _postfix_expression_choice)
                     (:choice ":" (:alias _colon_eol ":"))
                     (:field :arguments colon_argument))))
  colon_argument (:prec-left 5
                  (:seq
                   (:choice
                    (:field :lambda_start
                     (:seq
                      (:choice (:alias _colon_bindings bindings) _identifier wildcard)
                      (:choice (:choice "=>" (:alias "⇒" "=>")) "?=>")))
                    :blank)
                   (:choice indented_block indented_cases)))
  _colon_bindings (:prec-dynamic 3
                   (:seq
                    "("
                    (:choice
                     (:seq
                      (:seq
                       (:choice binding (:alias name_and_type binding))
                       (:repeat (:seq "," (:choice binding (:alias name_and_type binding)))))
                      (:choice "," :blank))
                     :blank)
                    ")"))
  field_expression (:prec-left 18
                    (:seq (:field :value _simple_expression) "." (:field :field _identifier)))
  instance_expression (:choice
                       (:prec-dynamic 1
                        (:seq
                         "new"
                         _constructor_application
                         template_body
                         (:choice _end_marker_kw_tail :blank)))
                       (:prec "new" (:seq "new" template_body (:choice _end_marker_kw_tail :blank)))
                       (:prec "new"
                        (:seq "new" (:field :early_defs early_defs) "with" _constructor_application))
                       (:seq "new" _constructor_application))
  early_defs (:prec-left 1
              (:seq
               "{"
               (:choice (:choice _braced_template_body1 _braced_template_body2) :blank)
               "}"))
  ascription_expression (:prec-left 0
                         (:seq
                          (:choice _postfix_expression_choice match_expression)
                          ":"
                          (:choice
                           (:seq
                            (:field :type _param_type)
                            (:repeat
                             (:prec 1
                              (:prec-dynamic -1
                               (:seq
                                (:choice (:choice "=>" (:alias "⇒" "=>")) "?=>")
                                (:field :return_type _param_type))))))
                           (:prec-dynamic -1
                            (:seq
                             (:field :type (:alias _identifier type_identifier))
                             (:repeat1
                              (:seq
                               (:choice (:choice "=>" (:alias "⇒" "=>")) "?=>")
                               (:field :return_type _param_type)))))
                           (:repeat1 annotation))))
  infix_expression (:choice
                    (:prec-left 7
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias (:choice _alpha_identifier _backquoted_id) identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 6
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:choice
                          (:pattern "[&:+\\-*%\\u005e\\u007c#?@\\\\~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?=" "u")
                          (:pattern "[<>!](?:(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])+\\/?|\\/)=" "u")
                          (:pattern "\\/=" "u")
                          (:pattern "\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?=" "u")))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 8
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator (:alias _op_left_or operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-right 8
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:pattern "\\u007c(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 9
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator (:alias _op_left_xor operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-right 9
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:pattern "\\u005e(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 10
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator (:alias _op_left_and operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-right 10
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:pattern "&(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 11
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator (:alias _op_left_eq operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-right 11
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:pattern "[=!](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 12
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator (:alias _op_left_rel operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-right 12
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:pattern "[<>](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 13
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator (:alias _op_left_colon operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-right 13
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:pattern ":(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 14
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator (:alias _op_left_add operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-right 14
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:pattern "[+\\-](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 15
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:choice
                        (:alias _asterisk operator_identifier)
                        (:alias _op_left_mul operator_identifier)))
                      (:field :right _infix_operand)))
                    (:prec-right 15
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:choice
                          (:pattern "[*%](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u")
                          (:pattern "\\/:" "u")
                          (:pattern "\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u")))
                        operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-left 16
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator (:alias _op_left_other operator_identifier))
                      (:field :right _infix_operand)))
                    (:prec-right 16
                     (:seq
                      (:field :left _infix_operand)
                      (:field :operator
                       (:alias
                        (:token
                         (:pattern "[#?@\\\\~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                        operator_identifier))
                      (:field :right _infix_operand))))
  postfix_expression (:choice
                      (:prec-right 7
                       (:seq
                        _infix_operand
                        (:alias (:choice _alpha_identifier _backquoted_id) identifier)))
                      (:prec-left 5
                       (:seq
                        _infix_operand
                        (:alias (:choice _postfix_op _postfix_star) operator_identifier))))
  _postfix_expression_choice (:prec-left 5 (:choice postfix_expression _infix_operand))
  macro_body (:prec-left 20 (:seq "macro" _infix_operand))
  prefix_expression (:prec 17 (:seq (:choice "+" "-" "!" "~") _simple_expression))
  tuple_expression (:seq "(" expression (:repeat1 (:seq "," expression)) (:choice "," :blank) ")")
  parenthesized_expression (:seq "(" expression ")")
  type_arguments (:seq
                  "["
                  (:seq
                   (:seq
                    (:choice _type capture_set named_type_argument)
                    (:repeat (:seq "," (:choice _type capture_set named_type_argument))))
                   (:choice "," :blank))
                  "]")
  named_type_argument (:seq (:field :name _identifier) "=" (:field :type _type))
  arguments (:seq "(" (:choice (:choice _argument_list :blank) (:seq "using" _exprs_in_parens)) ")")
  _argument_list (:seq
                  (:seq
                   (:choice expression vararg)
                   (:repeat (:seq "," (:choice expression vararg))))
                  (:choice "," :blank))
  vararg (:choice
          (:prec 15 (:seq _simple_expression _postfix_star))
          (:prec-dynamic 1 (:seq _simple_expression ":" (:token (:seq "_" (:token-immediate "*"))))))
  _exprs_in_parens (:seq (:seq expression (:repeat (:seq "," expression))) (:choice "," :blank))
  splice_expression (:prec-left 20 (:choice (:seq "${" _block "}") (:seq "$[" _type "]")))
  quote_expression (:prec-left 20
                    (:seq
                     "'"
                     (:choice
                      (:seq "{" (:choice _block :blank) "}")
                      (:seq "[" (:repeat (:seq type_definition _semicolon)) _type "]")
                      identifier
                      null_literal
                      boolean_literal)))
  identifier (:choice
              _alpha_identifier
              _backquoted_id
              _soft_identifier
              _this_identifier
              _super_identifier)
  _this_identifier "this"
  _super_identifier "super"
  _soft_identifier (:prec "soft_id" "extension")
  _alpha_identifier (:token
                     (:seq
                      (:pattern "[\\p{Lu}\\p{Lt}\\p{Nl}\\p{Lo}\\p{Lm}\\$\\p{Ll}_\\u00AA\\u00BB\\u02B0-\\u02B8\\u02C0-\\u02C1\\u02E0-\\u02E4\\u037A\\u1D78\\u1D9B-\\u1DBF\\u2071\\u207F\\u2090-\\u209C\\u2C7C-\\u2C7D\\uA69C-\\uA69D\\uA770\\uA7F8-\\uA7F9\\uAB5C-\\uAB5F\\$][\\p{Lu}\\p{Lt}\\p{Nl}\\p{Lo}\\p{Lm}\\$\\p{Ll}_\\u00AA\\u00BB\\u02B0-\\u02B8\\u02C0-\\u02C1\\u02E0-\\u02E4\\u037A\\u1D78\\u1D9B-\\u1DBF\\u2071\\u207F\\u2090-\\u209C\\u2C7C-\\u2C7D\\uA69C-\\uA69D\\uA770\\uA7F8-\\uA7F9\\uAB5C-\\uAB5F0-9\\$_\\p{Ll}]*")
                      (:choice
                       (:pattern "_(?:(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])+|\\/)" "u")
                       :blank)))
  _interpolation_identifier (:pattern "[\\p{Lu}\\p{Lt}\\p{Nl}\\p{Lo}\\p{Lm}\\p{Ll}_\\u00AA\\u00BB\\u02B0-\\u02B8\\u02C0-\\u02C1\\u02E0-\\u02E4\\u037A\\u1D78\\u1D9B-\\u1DBF\\u2071\\u207F\\u2090-\\u209C\\u2C7C-\\u2C7D\\uA69C-\\uA69D\\uA770\\uA7F8-\\uA7F9\\uAB5C-\\uAB5F][\\p{Lu}\\p{Lt}\\p{Nl}\\p{Lo}\\p{Lm}\\p{Ll}_\\u00AA\\u00BB\\u02B0-\\u02B8\\u02C0-\\u02C1\\u02E0-\\u02E4\\u037A\\u1D78\\u1D9B-\\u1DBF\\u2071\\u207F\\u2090-\\u209C\\u2C7C-\\u2C7D\\uA69C-\\uA69D\\uA770\\uA7F8-\\uA7F9\\uAB5C-\\uAB5F0-9_\\p{Ll}]*")
  _backquoted_id (:pattern "`[^\\n`]+`")
  _identifier (:choice
               (:choice
                (:alias _alpha_identifier identifier)
                (:alias _backquoted_id identifier)
                (:alias _soft_identifier identifier)
                (:alias "this" identifier)
                (:alias "super" identifier))
               (:alias _op_name operator_identifier))
  identifiers (:seq
               (:choice
                (:alias _alpha_identifier identifier)
                (:alias _backquoted_id identifier)
                (:alias _soft_identifier identifier)
                (:alias "this" identifier)
                (:alias "super" identifier))
               ","
               (:seq
                (:choice
                 (:alias _alpha_identifier identifier)
                 (:alias _backquoted_id identifier)
                 (:alias _soft_identifier identifier)
                 (:alias "this" identifier)
                 (:alias "super" identifier))
                (:repeat
                 (:seq
                  ","
                  (:choice
                   (:alias _alpha_identifier identifier)
                   (:alias _backquoted_id identifier)
                   (:alias _soft_identifier identifier)
                   (:alias "this" identifier)
                   (:alias "super" identifier))))))
  wildcard "_"
  _asterisk "*"
  operator_identifier (:choice
                       _asterisk
                       _op_left_or
                       _op_left_xor
                       _op_left_and
                       _op_left_eq
                       _op_left_rel
                       _op_left_colon
                       _op_left_add
                       _op_left_mul
                       _op_left_other
                       (:token
                        (:choice
                         (:pattern "[&:+\\-*%\\u005e\\u007c#?@\\\\~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?=" "u")
                         (:pattern "[<>!](?:(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])+\\/?|\\/)=" "u")
                         (:pattern "\\/=" "u")
                         (:pattern "\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?=" "u")))
                       (:token
                        (:pattern "\\u007c(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                       (:token
                        (:pattern "\\u005e(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                       (:token
                        (:pattern "&(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                       (:token
                        (:pattern "[=!](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                       (:token
                        (:pattern "[<>](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                       (:token
                        (:pattern ":(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                       (:token
                        (:pattern "[+\\-](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u"))
                       (:token
                        (:choice
                         (:pattern "[*%](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u")
                         (:pattern "\\/:" "u")
                         (:pattern "\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u")))
                       (:token
                        (:pattern "[#?@\\\\~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*\\/?:" "u")))
  _op_left_or (:token
               (:choice
                (:pattern "\\u007c" "u")
                (:pattern "\\u007c(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")))
  _op_left_xor (:token
                (:choice
                 (:pattern "\\u005e" "u")
                 (:pattern "\\u005e(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")))
  _op_left_and (:token
                (:choice
                 (:pattern "&" "u")
                 (:pattern "&(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")))
  _op_left_eq (:token
               (:choice
                (:pattern "!" "u")
                "!="
                (:pattern "!(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")
                (:pattern "=(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])" "u")))
  _op_left_rel (:token
                (:choice
                 (:pattern "[<>]" "u")
                 "<="
                 ">="
                 (:pattern "[<>](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")))
  _op_left_colon (:token
                  (:pattern ":(?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u"))
  _op_left_add (:token
                (:choice
                 (:pattern "[+\\-]" "u")
                 (:pattern "[+\\-](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")))
  _op_left_mul (:token
                (:choice
                 (:pattern "[/%]" "u")
                 (:pattern "[*%](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")
                 (:pattern "\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]" "u")
                 (:pattern "\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")))
  _op_left_other (:token
                  (:choice
                   (:pattern "[#?\\\\~\\u00ac\\u00b1\\u00d7\\u00f7\\u2190-\\u2194\\u2200-\\u22ff\\p{So}]" "u")
                   (:pattern "[#?@\\\\~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*(?:[\\-!#%&*+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}]|\\/[\\-!#%&+\\\\<>?@\\u005e\\u007c~\\u00ac\\u00b1\\u00d7\\u00f7\\u03f6\\u0606-\\u0608\\u2044\\u2052\\u207a-\\u207c\\u208a-\\u208c\\u2118\\u2140-\\u2144\\u214b\\u2190-\\u2194\\u219a-\\u219b\\u21a0\\u21a3\\u21a6\\u21ae\\u21ce-\\u21cf\\u21d2\\u21d4\\u21f4-\\u22ff\\u2320-\\u2321\\u237c\\u239b-\\u23b3\\u23dc-\\u23e1\\u25b7\\u25c1\\u25f8-\\u25ff\\u266f\\u27c0-\\u27c4\\u27c7-\\u27e5\\u27f0-\\u27ff\\u2900-\\u2982\\u2999-\\u29d7\\u29dc-\\u29fb\\u29fe-\\u2aff\\u2b30-\\u2b44\\u2b47-\\u2b4c\\ufb29\\ufe62\\ufe64-\\ufe66\\uff0b\\uff1c-\\uff1e\\uff5c\\uff5e\\uffe2\\uffe9-\\uffec𐶎-𐶏𜻰𝛁𝛛𝛻𝜕𝜵𝝏𝝯𝞉𝞩𝟃𞻰-𞻱🣐-🣘\\p{So}])" "u")))
  _op_name (:token
            (:choice
             (:pattern "[\\u00ac\\u00b1\\u00d7\\u00f7\\u2190-\\u2194\\u2200-\\u22ff\\p{So}\\-!#%&*+/\\\\<>?\\u005e\\u007c~]" "u")
             (:pattern "[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])+" "u")
             (:pattern "\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}](?:[\\-!#%&*+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}]|\\/[\\-!#%&+\\\\:<=>?@\\u005e\\u007c~\\p{Sm}\\p{So}])*" "u")))
  xml_expression (:repeat1 _xml_node)
  _xml_node (:choice xml_element xml_comment xml_cdata xml_processing_instruction)
  xml_element (:seq _xml_open_tag (:choice "/>" (:seq ">" (:repeat _xml_content) _xml_end_tag)))
  _xml_open_tag (:seq
                 (:alias _xml_tag_start "<")
                 (:field :name
                  (:alias (:token-immediate (:pattern "[_\\p{L}][-.:_\\p{L}\\p{Nd}]*")) xml_name))
                 (:repeat xml_attribute))
  _xml_end_tag (:seq
                "</"
                (:alias (:token-immediate (:pattern "[_\\p{L}][-.:_\\p{L}\\p{Nd}]*")) xml_name)
                ">")
  xml_attribute (:seq
                 (:field :key (:alias (:token (:pattern "[_\\p{L}][-.:_\\p{L}\\p{Nd}]*")) xml_name))
                 "="
                 (:field :value
                  (:choice
                   (:alias
                    (:token (:choice (:pattern "\"[^<\"]*\"") (:pattern "'[^<']*'")))
                    xml_string)
                   block)))
  _xml_content (:choice
                xml_text
                (:alias "{{" xml_text)
                (:alias "}}" xml_text)
                block
                _xml_node
                _suppress_block_comment)
  xml_text (:token (:prec 2 (:pattern "[^<{}]+")))
  xml_comment (:token
               (:seq
                "<!--"
                (:repeat (:choice (:pattern "[^-]") (:seq (:repeat1 "-") (:pattern "[^->]"))))
                (:repeat "-")
                "-->"))
  xml_cdata (:token
             (:seq
              "<![CDATA["
              (:repeat (:choice (:pattern "[^\\]]") (:seq (:repeat1 "]") (:pattern "[^\\]>]"))))
              (:repeat "]")
              "]]>"))
  xml_processing_instruction (:token
                              (:seq
                               "<?"
                               (:repeat
                                (:choice (:pattern "[^?]") (:seq (:repeat1 "?") (:pattern "[^?>]"))))
                               (:repeat "?")
                               "?>"))
  xml_pattern (:seq
               _xml_open_tag
               (:choice "/>" (:seq ">" (:repeat _xml_pattern_content) _xml_end_tag)))
  _xml_pattern_content (:choice
                        xml_text
                        (:alias "{{" xml_text)
                        (:alias "}}" xml_text)
                        (:seq
                         "{"
                         (:seq _xml_embedded_pattern (:repeat (:seq "," _xml_embedded_pattern)))
                         "}")
                        xml_pattern
                        xml_comment
                        xml_cdata
                        xml_processing_instruction
                        _suppress_block_comment)
  _xml_embedded_pattern (:choice
                         _identifier
                         stable_identifier
                         interpolated_string_expression
                         capture_pattern
                         tuple_pattern
                         named_tuple_pattern
                         case_class_pattern
                         quote_expression
                         literal
                         wildcard
                         (:alias _xml_repeat_pattern repeat_pattern)
                         xml_pattern)
  _xml_repeat_pattern (:seq (:field :pattern (:choice wildcard _identifier)) _asterisk)
  _non_null_literal (:choice
                     integer_literal
                     floating_point_literal
                     (:alias _floating_point_with_separators floating_point_literal)
                     boolean_literal
                     character_literal
                     string)
  literal_type (:prec-left 2 _non_null_literal)
  literal (:choice _non_null_literal null_literal)
  integer_literal (:token
                   (:seq
                    (:choice (:pattern "[-]") :blank)
                    (:choice
                     (:pattern "[\\d](_?\\d)*")
                     (:pattern "0[xX][\\da-fA-F](_?[\\da-fA-F])*")
                     (:pattern "0[bB][01](_*[01])*"))
                    (:choice (:pattern "[lL]") :blank)))
  floating_point_literal (:token
                          (:seq
                           (:choice (:pattern "[-]") :blank)
                           (:choice
                            (:seq
                             (:pattern "[\\d]+\\.[\\d](_?\\d)*")
                             (:choice (:pattern "[eE][+-]?[\\d](_?\\d)*") :blank)
                             (:choice (:pattern "[dfDF]") :blank))
                            (:seq
                             (:pattern "\\.[\\d](_?\\d)*")
                             (:choice (:pattern "[eE][+-]?[\\d](_?\\d)*") :blank)
                             (:choice (:pattern "[dfDF]") :blank))
                            (:seq
                             (:pattern "[\\d]+")
                             (:pattern "[eE][+-]?[\\d](_?\\d)*")
                             (:choice (:pattern "[dfDF]") :blank))
                            (:seq
                             (:pattern "[\\d]+")
                             (:choice (:pattern "[eE][+-]?[\\d](_?\\d)*") :blank)
                             (:pattern "[dfDF]")))))
  boolean_literal (:choice "true" "false")
  character_literal (:token
                     (:seq
                      "'"
                      (:choice
                       (:choice
                        (:seq
                         "\\"
                         (:choice
                          (:pattern "[^xu]")
                          (:pattern "[uU]+[0-9a-fA-F]{4}")
                          (:pattern "x[0-9a-fA-F]{2}")))
                        (:pattern "[^\\\\'\\n]"))
                       :blank)
                      "'"))
  interpolated_string_expression (:choice
                                  (:seq
                                   (:field :interpolator (:alias _raw_string_start identifier))
                                   (:alias _raw_string interpolated_string))
                                  (:seq
                                   (:field :interpolator
                                    (:choice
                                     (:alias _alpha_identifier identifier)
                                     (:alias _backquoted_id identifier)
                                     (:alias _soft_identifier identifier)
                                     (:alias "this" identifier)
                                     (:alias "super" identifier)))
                                   interpolated_string))
  _dollar_escape (:alias (:token (:seq "$" (:choice "$" "\""))) escape_sequence)
  _aliased_interpolation_identifier (:alias _interpolation_identifier identifier)
  interpolation (:seq
                 "$"
                 (:choice
                  _aliased_interpolation_identifier
                  block
                  (:prec-dynamic -1 (:seq "{" capture_pattern "}"))))
  interpolated_string (:choice
                       (:seq
                        (:token-immediate "\"")
                        (:repeat
                         (:seq
                          _interpolated_string_middle
                          (:choice _dollar_escape interpolation escape_sequence)))
                        _single_line_string_end)
                       (:seq
                        (:token-immediate "\"\"\"")
                        (:repeat
                         (:seq
                          _interpolated_multiline_string_middle
                          (:choice _dollar_escape interpolation)))
                        _multiline_string_end))
  _raw_string (:choice
               (:seq
                _simple_string_start
                (:seq
                 (:repeat (:seq _raw_string_middle (:choice _dollar_escape interpolation)))
                 _single_line_string_end))
               (:seq
                _simple_multiline_string_start
                (:repeat (:seq _raw_string_multiline_middle (:choice _dollar_escape interpolation)))
                _multiline_string_end))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[tbnrf\"'\\\\]")
                     (:pattern "[uU]+[0-9a-fA-F]{4}")
                     (:pattern "[0-3]?[0-7]{1,2}")
                     (:pattern "[^\\r\\n]"))))
  string (:choice
          (:seq
           _simple_string_start
           (:repeat (:seq _simple_string_middle escape_sequence))
           _single_line_string_end)
          (:seq _simple_multiline_string_start _multiline_string_end))
  _semicolon (:choice ";" _automatic_semicolon)
  null_literal "null"
  unit (:prec 4 (:seq "(" ")"))
  return_expression (:prec-left 0
                     (:seq "return" (:choice (:choice expression do_while_expression) :blank)))
  throw_expression (:prec-left 0 (:seq "throw" expression))
  while_expression (:prec 2
                    (:choice
                     (:prec-right 0
                      (:seq
                       "while"
                       (:field :condition parenthesized_expression)
                       (:choice _automatic_semicolon :blank)
                       (:field :body expression)))
                     (:seq
                      (:prec-right 0
                       (:seq
                        "while"
                        (:field :condition
                         (:seq _indentable_expression (:choice _automatic_semicolon :blank) "do"))
                        (:field :body _indentable_expression)))
                      (:choice _end_marker_kw_tail :blank))))
  do_while_expression (:prec-right 0
                       (:seq
                        "do"
                        (:field :body expression)
                        "while"
                        (:field :condition parenthesized_expression)))
  for_expression (:choice
                  (:prec-right 1
                   (:seq
                    "for"
                    (:field :enumerators
                     (:choice (:seq "(" enumerators ")") (:seq "{" enumerators "}")))
                    (:choice
                     (:field :body (:choice indented_block expression))
                     (:seq "do" (:field :body _indentable_expression))
                     (:seq _control_tail_gate "yield" (:field :body _indentable_expression)))))
                  (:seq
                   (:prec-right 1
                    (:seq
                     "for"
                     (:field :enumerators enumerators)
                     (:choice
                      (:seq "do" (:field :body _indentable_expression))
                      (:seq _control_tail_gate "yield" (:field :body _indentable_expression)))))
                   (:choice _end_marker_kw_tail :blank)))
  enumerators (:choice
               (:seq
                (:seq enumerator (:repeat (:seq _semicolon enumerator)))
                (:choice _automatic_semicolon :blank))
               (:seq
                _indent
                (:seq enumerator (:repeat (:seq _semicolon enumerator)))
                (:choice _automatic_semicolon :blank)
                _outdent))
  enumerator (:choice
              (:seq
               (:choice "case" :blank)
               _pattern
               (:choice "<-" "←" "=")
               (:choice expression indented_block)
               (:choice guard :blank))
              (:repeat1 guard))
  _shebang (:alias
            (:token
             (:seq
              "#!"
              (:pattern "[^\\n]*")
              (:choice (:seq (:repeat (:seq "\n" (:pattern "[^\\n]*"))) "\n!#") :blank)))
            comment)
  comment (:seq (:token "//") (:choice using_directive _comment_text _suppress_block_comment))
  _comment_text (:token (:prec 1 (:pattern ".*")))
  using_directive (:seq
                   (:alias _using_directive_start ">")
                   (:token "using")
                   using_directive_key
                   using_directive_value)
  using_directive_key (:token (:pattern "[^\\s]+"))
  using_directive_value (:token (:pattern ".*"))}}
