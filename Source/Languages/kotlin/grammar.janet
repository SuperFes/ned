# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "kotlin"
 :word _alpha_identifier
 :extras [line_comment multiline_comment (:pattern "\\s+")]
 :conflicts [[_primary_expression callable_reference]
             [constructor_invocation _unescaped_annotation]
             [platform_modifier simple_identifier]
             [class_modifier simple_identifier]
             [_postfix_unary_expression _expression]
             [call_expression range_expression comparison_expression]
             [call_expression elvis_expression comparison_expression]
             [call_expression check_expression comparison_expression]
             [call_expression additive_expression comparison_expression]
             [call_expression infix_expression comparison_expression]
             [call_expression multiplicative_expression comparison_expression]
             [type_arguments _comparison_operator]
             [_statement prefix_expression]
             [_statement prefix_expression modifiers]
             [prefix_expression when_subject]
             [prefix_expression value_argument]
             [user_type]
             [user_type anonymous_function]
             [user_type function_type]
             [annotated_lambda modifiers]
             [setter simple_identifier]
             [getter simple_identifier]
             [parameter_modifiers _type_modifier]
             [type_modifiers]
             [not_nullable_type]]
 :precedences []
 :externals [_automatic_semicolon
             _import_list_delimiter
             safe_nav
             multiline_comment
             _string_start
             _string_end
             string_content]
 :inline []
 :supertypes []
 :rules
 {source_file (:seq
               (:choice shebang_line :blank)
               (:repeat file_annotation)
               (:choice package_header :blank)
               (:repeat import_list)
               (:repeat (:seq _statement _semi)))
  shebang_line (:seq "#!" (:pattern "[^\\r\\n]*"))
  file_annotation (:seq
                   "@"
                   "file"
                   ":"
                   (:choice (:seq "[" (:repeat1 _unescaped_annotation) "]") _unescaped_annotation)
                   _semi)
  package_header (:seq "package" identifier _semi)
  import_list (:seq (:repeat1 import_header) _import_list_delimiter)
  import_header (:seq
                 "import"
                 (:alias _import_identifier identifier)
                 (:choice (:choice (:seq "." wildcard_import) import_alias) :blank)
                 _semi)
  wildcard_import (:token-immediate "*")
  import_alias (:seq "as" (:alias simple_identifier type_identifier))
  top_level_object (:seq _declaration (:choice _semi :blank))
  type_alias (:seq
              (:choice modifiers :blank)
              "typealias"
              (:alias simple_identifier type_identifier)
              (:choice type_parameters :blank)
              "="
              _type)
  _declaration (:choice
                class_declaration
                object_declaration
                function_declaration
                property_declaration
                getter
                setter
                type_alias)
  class_declaration (:prec-right 0
                     (:choice
                      (:seq
                       (:choice modifiers :blank)
                       (:choice "class" "interface")
                       (:alias simple_identifier type_identifier)
                       (:choice type_parameters :blank)
                       (:choice primary_constructor :blank)
                       (:choice (:seq ":" _delegation_specifiers) :blank)
                       (:choice type_constraints :blank)
                       (:choice class_body :blank))
                      (:seq
                       (:choice modifiers :blank)
                       "enum"
                       "class"
                       (:alias simple_identifier type_identifier)
                       (:choice type_parameters :blank)
                       (:choice primary_constructor :blank)
                       (:choice (:seq ":" _delegation_specifiers) :blank)
                       (:choice type_constraints :blank)
                       (:choice enum_class_body :blank))))
  primary_constructor (:seq
                       (:choice (:seq (:choice modifiers :blank) "constructor") :blank)
                       _class_parameters)
  class_body (:seq "{" (:choice _class_member_declarations :blank) "}")
  _class_parameters (:seq
                     "("
                     (:choice (:seq class_parameter (:repeat (:seq "," class_parameter))) :blank)
                     (:choice "," :blank)
                     ")")
  binding_pattern_kind (:choice "val" "var")
  class_parameter (:seq
                   (:choice modifiers :blank)
                   (:choice binding_pattern_kind :blank)
                   simple_identifier
                   ":"
                   _type
                   (:choice (:seq "=" _expression) :blank))
  _delegation_specifiers (:prec-left 0
                          (:seq delegation_specifier (:repeat (:seq "," delegation_specifier))))
  delegation_specifier (:prec-left 0
                        (:choice constructor_invocation explicit_delegation user_type function_type))
  constructor_invocation (:seq user_type value_arguments)
  _annotated_delegation_specifier (:seq (:repeat annotation) delegation_specifier)
  explicit_delegation (:seq (:choice user_type function_type) "by" _expression)
  type_parameters (:seq "<" (:seq type_parameter (:repeat (:seq "," type_parameter))) ">")
  type_parameter (:seq
                  (:choice type_parameter_modifiers :blank)
                  (:alias simple_identifier type_identifier)
                  (:choice (:seq ":" _type) :blank))
  type_constraints (:prec-right 0
                    (:seq "where" (:seq type_constraint (:repeat (:seq "," type_constraint)))))
  type_constraint (:seq (:repeat annotation) (:alias simple_identifier type_identifier) ":" _type)
  _class_member_declarations (:repeat1 (:seq _class_member_declaration _semi))
  _class_member_declaration (:choice
                             _declaration
                             companion_object
                             anonymous_initializer
                             secondary_constructor)
  anonymous_initializer (:seq "init" _block)
  companion_object (:seq
                    (:choice modifiers :blank)
                    "companion"
                    "object"
                    (:choice (:alias simple_identifier type_identifier) :blank)
                    (:choice (:seq ":" _delegation_specifiers) :blank)
                    (:choice class_body :blank))
  function_value_parameters (:seq
                             "("
                             (:choice
                              (:seq
                               _function_value_parameter
                               (:repeat (:seq "," _function_value_parameter)))
                              :blank)
                             (:choice "," :blank)
                             ")")
  _function_value_parameter (:seq
                             (:choice parameter_modifiers :blank)
                             parameter
                             (:choice (:seq "=" _expression) :blank))
  _receiver_type (:seq
                  (:choice type_modifiers :blank)
                  (:choice _type_reference parenthesized_type nullable_type))
  function_declaration (:prec-right 0
                        (:seq
                         (:choice modifiers :blank)
                         "fun"
                         (:choice type_parameters :blank)
                         (:choice (:seq _receiver_type (:choice "." :blank)) :blank)
                         simple_identifier
                         function_value_parameters
                         (:choice (:seq ":" _type) :blank)
                         (:choice type_constraints :blank)
                         (:choice function_body :blank)))
  function_body (:choice _block (:seq "=" _expression))
  variable_declaration (:prec-left 3 (:seq simple_identifier (:choice (:seq ":" _type) :blank)))
  property_declaration (:prec-right 0
                        (:seq
                         (:choice modifiers :blank)
                         binding_pattern_kind
                         (:choice type_parameters :blank)
                         (:choice (:seq _receiver_type (:choice "." :blank)) :blank)
                         (:choice variable_declaration multi_variable_declaration)
                         (:choice type_constraints :blank)
                         (:choice (:choice (:seq "=" _expression) property_delegate) :blank)
                         (:choice ";" :blank)
                         (:choice (:choice getter :blank) (:choice setter :blank))))
  property_delegate (:seq "by" _expression)
  getter (:prec-right 0
          (:seq
           (:choice modifiers :blank)
           "get"
           (:choice (:seq "(" ")" (:choice (:seq ":" _type) :blank) function_body) :blank)))
  setter (:prec-right 0
          (:seq
           (:choice modifiers :blank)
           "set"
           (:choice
            (:seq
             "("
             parameter_with_optional_type
             ")"
             (:choice (:seq ":" _type) :blank)
             function_body)
            :blank)))
  parameters_with_optional_type (:seq
                                 "("
                                 (:seq
                                  parameter_with_optional_type
                                  (:repeat (:seq "," parameter_with_optional_type)))
                                 ")")
  parameter_with_optional_type (:seq
                                (:choice parameter_modifiers :blank)
                                simple_identifier
                                (:choice (:seq ":" _type) :blank))
  parameter (:seq simple_identifier ":" _type)
  object_declaration (:prec-right 0
                      (:seq
                       (:choice modifiers :blank)
                       "object"
                       (:alias simple_identifier type_identifier)
                       (:choice (:seq ":" _delegation_specifiers) :blank)
                       (:choice class_body :blank)))
  secondary_constructor (:seq
                         (:choice modifiers :blank)
                         "constructor"
                         function_value_parameters
                         (:choice (:seq ":" constructor_delegation_call) :blank)
                         (:choice _block :blank))
  constructor_delegation_call (:seq (:choice "this" "super") value_arguments)
  enum_class_body (:seq
                   "{"
                   (:choice _enum_entries :blank)
                   (:choice (:seq ";" (:choice _class_member_declarations :blank)) :blank)
                   "}")
  _enum_entries (:seq (:seq enum_entry (:repeat (:seq "," enum_entry))) (:choice "," :blank))
  enum_entry (:seq
              (:choice modifiers :blank)
              simple_identifier
              (:choice value_arguments :blank)
              (:choice class_body :blank))
  _type (:seq
         (:choice type_modifiers :blank)
         (:choice parenthesized_type nullable_type _type_reference function_type not_nullable_type))
  _type_reference (:prec-left 1 (:choice user_type "dynamic"))
  not_nullable_type (:seq
                     (:choice type_modifiers :blank)
                     (:choice user_type parenthesized_user_type)
                     "&"
                     (:choice type_modifiers :blank)
                     (:choice user_type parenthesized_user_type))
  nullable_type (:seq (:choice _type_reference parenthesized_type) (:repeat1 _quest))
  _quest "?"
  user_type (:seq _simple_user_type (:repeat (:seq "." _simple_user_type)))
  _simple_user_type (:prec-right 2
                     (:seq
                      (:alias simple_identifier type_identifier)
                      (:choice type_arguments :blank)))
  type_projection (:choice (:seq (:choice type_projection_modifiers :blank) _type) "*")
  type_projection_modifiers (:repeat1 _type_projection_modifier)
  _type_projection_modifier variance_modifier
  function_type (:seq
                 (:choice (:seq _simple_user_type ".") :blank)
                 function_type_parameters
                 "->"
                 _type)
  function_type_parameters (:prec-left 1
                            (:seq
                             "("
                             (:choice
                              (:seq
                               (:choice parameter _type)
                               (:repeat (:seq "," (:choice parameter _type))))
                              :blank)
                             ")"))
  parenthesized_type (:seq "(" _type ")")
  parenthesized_user_type (:seq "(" (:choice user_type parenthesized_user_type) ")")
  statements (:seq _statement (:repeat (:seq _semi _statement)) (:choice _semi :blank))
  _statement (:choice
              _declaration
              (:seq
               (:repeat (:choice label annotation))
               (:choice assignment _loop_statement _expression)))
  label (:token (:seq (:pattern "[a-zA-Z_][a-zA-Z_0-9]*") "@"))
  control_structure_body (:choice _block _statement)
  _block (:prec 1 (:seq "{" (:choice statements :blank) "}"))
  _loop_statement (:choice for_statement while_statement do_while_statement)
  for_statement (:prec-right 0
                 (:seq
                  "for"
                  "("
                  (:repeat annotation)
                  (:choice variable_declaration multi_variable_declaration)
                  "in"
                  _expression
                  ")"
                  (:choice control_structure_body :blank)))
  while_statement (:seq "while" "(" _expression ")" (:choice ";" control_structure_body))
  do_while_statement (:prec-right 0
                      (:seq
                       "do"
                       (:choice control_structure_body :blank)
                       "while"
                       "("
                       _expression
                       ")"))
  _semi _automatic_semicolon
  assignment (:choice
              (:prec-left 1
               (:seq directly_assignable_expression _assignment_and_operator _expression))
              (:prec-left 1 (:seq directly_assignable_expression "=" _expression)))
  _expression (:choice _unary_expression _binary_expression _primary_expression)
  _unary_expression (:choice
                     postfix_expression
                     call_expression
                     indexing_expression
                     navigation_expression
                     prefix_expression
                     as_expression
                     spread_expression)
  postfix_expression (:prec-left 16 (:seq _expression _postfix_unary_operator))
  call_expression (:prec-left 16 (:seq _expression call_suffix))
  indexing_expression (:prec-left 16 (:seq _expression indexing_suffix))
  navigation_expression (:prec-left 16 (:seq _expression navigation_suffix))
  prefix_expression (:prec-right 0
                     (:seq (:choice annotation label _prefix_unary_operator) _expression))
  as_expression (:prec-left 13 (:seq _expression _as_operator _type))
  spread_expression (:prec-left 2 (:seq "*" _expression))
  _binary_expression (:choice
                      multiplicative_expression
                      additive_expression
                      range_expression
                      infix_expression
                      elvis_expression
                      check_expression
                      comparison_expression
                      equality_expression
                      comparison_expression
                      equality_expression
                      conjunction_expression
                      disjunction_expression)
  multiplicative_expression (:prec-left 12 (:seq _expression _multiplicative_operator _expression))
  additive_expression (:prec-left 11 (:seq _expression _additive_operator _expression))
  range_expression (:prec-left 10 (:seq _expression ".." _expression))
  infix_expression (:prec-left 9 (:seq _expression simple_identifier _expression))
  elvis_expression (:prec-left 8 (:seq _expression "?:" _expression))
  check_expression (:prec-left 7
                    (:seq
                     _expression
                     (:choice (:seq _in_operator _expression) (:seq _is_operator _type))))
  comparison_expression (:prec-left 6 (:seq _expression _comparison_operator _expression))
  equality_expression (:prec-left 5 (:seq _expression _equality_operator _expression))
  conjunction_expression (:prec-left 4 (:seq _expression "&&" _expression))
  disjunction_expression (:prec-left 3 (:seq _expression "||" _expression))
  indexing_suffix (:seq "[" (:seq _expression (:repeat (:seq "," _expression))) "]")
  navigation_suffix (:seq
                     _member_access_operator
                     (:choice simple_identifier parenthesized_expression "class"))
  call_suffix (:prec-left 0
               (:seq
                (:choice type_arguments :blank)
                (:choice (:seq (:choice value_arguments :blank) annotated_lambda) value_arguments)))
  annotated_lambda (:seq (:repeat annotation) (:choice label :blank) lambda_literal)
  type_arguments (:seq "<" (:seq type_projection (:repeat (:seq "," type_projection))) ">")
  value_arguments (:seq
                   "("
                   (:choice
                    (:seq
                     (:seq value_argument (:repeat (:seq "," value_argument)))
                     (:choice "," :blank))
                    :blank)
                   ")")
  value_argument (:seq
                  (:choice annotation :blank)
                  (:choice (:seq simple_identifier "=") :blank)
                  (:choice "*" :blank)
                  _expression)
  _primary_expression (:choice
                       parenthesized_expression
                       simple_identifier
                       _literal_constant
                       string_literal
                       callable_reference
                       _function_literal
                       object_literal
                       collection_literal
                       this_expression
                       super_expression
                       if_expression
                       when_expression
                       try_expression
                       jump_expression)
  parenthesized_expression (:seq "(" _expression ")")
  collection_literal (:seq "[" _expression (:repeat (:seq "," _expression)) "]")
  _literal_constant (:choice
                     boolean_literal
                     integer_literal
                     hex_literal
                     bin_literal
                     character_literal
                     real_literal
                     "null"
                     long_literal
                     unsigned_literal)
  string_literal (:seq _string_start (:repeat (:choice string_content _interpolation)) _string_end)
  line_string_expression (:seq "${" _expression "}")
  _interpolation (:choice
                  (:seq "${" (:alias _expression interpolated_expression) "}")
                  (:seq "$" (:alias simple_identifier interpolated_identifier)))
  lambda_literal (:prec 0
                  (:seq
                   "{"
                   (:choice (:seq (:choice lambda_parameters :blank) "->") :blank)
                   (:choice statements :blank)
                   "}"))
  multi_variable_declaration (:seq
                              "("
                              (:seq variable_declaration (:repeat (:seq "," variable_declaration)))
                              ")")
  lambda_parameters (:seq _lambda_parameter (:repeat (:seq "," _lambda_parameter)))
  _lambda_parameter (:choice variable_declaration multi_variable_declaration)
  anonymous_function (:prec-right 0
                      (:seq
                       "fun"
                       (:choice
                        (:seq (:seq _simple_user_type (:repeat (:seq "." _simple_user_type))) ".")
                        :blank)
                       function_value_parameters
                       (:choice (:seq ":" _type) :blank)
                       (:choice function_body :blank)))
  _function_literal (:choice lambda_literal anonymous_function)
  object_literal (:seq "object" (:choice (:seq ":" _delegation_specifiers) :blank) class_body)
  this_expression (:choice "this" _this_at)
  super_expression (:prec-right 0 (:choice "super" (:seq "super" "<" _type ">") _super_at))
  if_expression (:prec-right 0
                 (:seq
                  "if"
                  "("
                  _expression
                  ")"
                  (:choice
                   control_structure_body
                   ";"
                   (:seq
                    (:choice control_structure_body :blank)
                    (:choice ";" :blank)
                    "else"
                    (:choice control_structure_body ";")))))
  when_subject (:seq
                "("
                (:choice (:seq (:repeat annotation) "val" variable_declaration "=") :blank)
                _expression
                ")")
  when_expression (:seq "when" (:choice when_subject :blank) "{" (:repeat when_entry) "}")
  when_entry (:seq
              (:choice (:seq when_condition (:repeat (:seq "," when_condition))) "else")
              "->"
              control_structure_body
              (:choice _semi :blank))
  when_condition (:choice _expression range_test type_test)
  range_test (:seq _in_operator _expression)
  type_test (:seq _is_operator _type)
  try_expression (:seq
                  "try"
                  _block
                  (:choice
                   (:seq (:repeat1 catch_block) (:choice finally_block :blank))
                   finally_block))
  catch_block (:seq "catch" "(" (:repeat annotation) simple_identifier ":" _type ")" _block)
  finally_block (:seq "finally" _block)
  jump_expression (:choice
                   (:prec-right 0 (:seq "throw" _expression))
                   (:prec-right 0 (:seq (:choice "return" _return_at) (:choice _expression :blank)))
                   "continue"
                   _continue_at
                   "break"
                   _break_at)
  callable_reference (:seq
                      (:choice (:alias simple_identifier type_identifier) :blank)
                      "::"
                      (:choice simple_identifier "class"))
  _assignment_and_operator (:choice "+=" "-=" "*=" "/=" "%=")
  _equality_operator (:choice "!=" "!==" "==" "===")
  _comparison_operator (:choice "<" ">" "<=" ">=")
  _in_operator (:choice "in" "!in")
  _is_operator (:choice "is" "!is")
  _additive_operator (:choice "+" "-")
  _multiplicative_operator (:choice "*" "/" "%")
  _as_operator (:choice "as" "as?")
  _prefix_unary_operator (:choice "++" "--" "-" "+" "!")
  _postfix_unary_operator (:choice "++" "--" "!!")
  _member_access_operator (:choice "." "::" (:alias safe_nav "?."))
  _indexing_suffix (:seq "[" _expression (:repeat (:seq "," _expression)) (:choice "," :blank) "]")
  _postfix_unary_suffix (:choice _postfix_unary_operator navigation_suffix indexing_suffix)
  _postfix_unary_expression (:seq _primary_expression (:repeat _postfix_unary_suffix))
  directly_assignable_expression (:prec 1 (:choice _postfix_unary_expression simple_identifier))
  modifiers (:prec-left 0 (:repeat1 (:choice annotation _modifier)))
  parameter_modifiers (:repeat1 (:choice annotation parameter_modifier))
  _modifier (:choice
             class_modifier
             member_modifier
             visibility_modifier
             function_modifier
             property_modifier
             inheritance_modifier
             parameter_modifier
             platform_modifier)
  type_modifiers (:repeat1 _type_modifier)
  _type_modifier (:choice annotation "suspend")
  class_modifier (:choice "sealed" "annotation" "data" "inner" "value")
  member_modifier (:choice "override" "lateinit")
  visibility_modifier (:choice "public" "private" "internal" "protected")
  variance_modifier (:choice "in" "out")
  type_parameter_modifiers (:repeat1 _type_parameter_modifier)
  _type_parameter_modifier (:choice reification_modifier variance_modifier annotation)
  function_modifier (:choice "tailrec" "operator" "infix" "inline" "external" "suspend")
  property_modifier "const"
  inheritance_modifier (:choice "abstract" "final" "open")
  parameter_modifier (:choice "vararg" "noinline" "crossinline")
  reification_modifier "reified"
  platform_modifier (:choice "expect" "actual")
  annotation (:choice _single_annotation _multi_annotation)
  _single_annotation (:seq "@" (:choice use_site_target :blank) _unescaped_annotation)
  _multi_annotation (:seq
                     "@"
                     (:choice use_site_target :blank)
                     "["
                     (:repeat1 _unescaped_annotation)
                     "]")
  use_site_target (:seq
                   (:choice "field" "property" "get" "set" "receiver" "param" "setparam" "delegate")
                   ":")
  _unescaped_annotation (:choice constructor_invocation user_type)
  simple_identifier (:choice
                     _lexical_identifier
                     "expect"
                     "data"
                     "inner"
                     "value"
                     "actual"
                     "set"
                     "get")
  identifier (:seq simple_identifier (:repeat (:seq "." simple_identifier)))
  _import_identifier (:choice simple_identifier (:seq _import_identifier "." simple_identifier))
  line_comment (:token (:seq "//" (:pattern ".*")))
  _return_at (:seq "return@" (:alias _lexical_identifier label))
  _continue_at (:seq "continue@" (:alias _lexical_identifier label))
  _break_at (:seq "break@" (:alias _lexical_identifier label))
  _this_at (:seq "this@" (:alias _lexical_identifier type_identifier))
  _super_at (:choice
             (:seq "super@" (:alias _lexical_identifier type_identifier))
             (:seq
              "super"
              "<"
              _type
              ">"
              (:token-immediate "@")
              (:alias _lexical_identifier type_identifier)))
  real_literal (:token
                (:choice
                 (:seq
                  (:choice
                   (:seq
                    (:token
                     (:seq (:pattern "[0-9]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                    (:token
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:token
                       (:seq
                        (:pattern "[0-9]+")
                        (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+"))))))))
                   (:seq
                    (:choice
                     (:token
                      (:seq
                       (:pattern "[0-9]+")
                       (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
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
                     :blank)))
                  (:choice (:pattern "[fF]") :blank))
                 (:seq
                  (:token
                   (:seq (:pattern "[0-9]+") (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                  (:pattern "[fF]"))))
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
  bin_literal (:token
               (:seq
                "0"
                (:pattern "[bB]")
                (:token (:seq (:pattern "[01]") (:repeat (:seq (:pattern "_+") (:pattern "[01]")))))))
  unsigned_literal (:seq (:choice integer_literal hex_literal bin_literal) (:pattern "[uU]L?"))
  long_literal (:seq (:choice integer_literal hex_literal bin_literal) "L")
  boolean_literal (:choice "true" "false")
  character_literal (:seq "'" (:choice character_escape_seq (:pattern "[^\\n\\r'\\\\]")) "'")
  character_escape_seq (:choice _uni_character_literal _escaped_identifier)
  _lexical_identifier (:choice _alpha_identifier _backtick_identifier)
  _alpha_identifier (:pattern "[\\p{L}_][\\p{L}_\\p{Nd}]*")
  _backtick_identifier (:pattern "`[^\\r\\n`]+`")
  _uni_character_literal (:seq "\\u" (:pattern "[0-9a-fA-F]{4}"))
  _escaped_identifier (:pattern "\\\\[tbrn'\"\\\\$]")}}
