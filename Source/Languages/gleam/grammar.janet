# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "gleam"
 :extras [(:pattern "\\r?\\n") (:pattern "\\s") module_comment statement_comment comment]
 :conflicts [[_maybe_record_expression _maybe_tuple_expression]
             [_maybe_record_expression _maybe_tuple_expression remote_constructor_name]
             [case_subjects]
             [source_file]
             [_constant_value _case_clause_guard_unit]
             [integer]]
 :precedences []
 :externals [quoted_content doc_comment_content]
 :inline []
 :supertypes []
 :rules
 {source_file (:repeat (:choice target_group _module_statement _statement_seq))
  _module_statement (:choice
                     import
                     constant
                     external_type
                     external_function
                     function
                     type_definition
                     type_alias
                     attribute)
  module_comment (:prec 3 (:seq "////" doc_comment_content))
  statement_comment (:prec 2 (:seq "///" doc_comment_content))
  comment (:choice (:prec 1 (:seq "//" (:pattern ".*"))) (:prec 4 (:seq "/////" (:pattern ".*"))))
  target_group (:seq "if" (:field :target target) "{" (:repeat _module_statement) "}")
  target (:choice "erlang" "javascript")
  attribute (:seq
             "@"
             (:field :name identifier)
             (:choice (:field :arguments (:alias _attribute_arguments arguments)) :blank))
  _attribute_arguments (:seq
                        "("
                        (:seq
                         attribute_value
                         (:repeat (:seq "," attribute_value))
                         (:choice "," :blank))
                        ")")
  attribute_value (:choice
                   _constant_value
                   (:seq (:field :label label) ":" (:field :value _constant_value)))
  import (:seq
          "import"
          (:field :module module)
          (:choice (:seq "." (:field :imports unqualified_imports)) :blank)
          (:choice (:seq "as" (:field :alias (:choice identifier discard))) :blank))
  module (:seq _name (:repeat (:seq "/" _name)))
  unqualified_imports (:seq
                       "{"
                       (:choice
                        (:seq
                         unqualified_import
                         (:repeat (:seq "," unqualified_import))
                         (:choice "," :blank))
                        :blank)
                       "}")
  unqualified_import (:choice
                      (:seq
                       (:field :name identifier)
                       (:choice (:seq "as" (:field :alias identifier)) :blank))
                      (:seq
                       "type"
                       (:field :name type_identifier)
                       (:choice (:seq "as" (:field :alias type_identifier)) :blank))
                      (:seq
                       (:field :name constructor_name)
                       (:choice (:seq "as" (:field :alias constructor_name)) :blank)))
  constant (:seq
            (:choice visibility_modifier :blank)
            "const"
            (:field :name identifier)
            (:choice _constant_type_annotation :blank)
            "="
            (:field :value _constant_value))
  _constant_value (:choice
                   string
                   float
                   integer
                   (:alias constant_tuple tuple)
                   (:alias constant_list list)
                   (:alias _constant_bit_string bit_string)
                   (:alias constant_record record)
                   identifier
                   (:alias constant_field_access field_access)
                   (:alias constant_binary_expression binary_expression))
  constant_tuple (:seq
                  "#"
                  "("
                  (:choice
                   (:seq _constant_value (:repeat (:seq "," _constant_value)) (:choice "," :blank))
                   :blank)
                  ")")
  constant_list (:seq
                 "["
                 (:choice
                  (:seq _constant_value (:repeat (:seq "," _constant_value)) (:choice "," :blank))
                  :blank)
                 "]")
  _constant_bit_string (:seq
                        "<<"
                        (:choice
                         (:seq
                          (:alias constant_bit_string_segment bit_string_segment)
                          (:repeat
                           (:seq "," (:alias constant_bit_string_segment bit_string_segment)))
                          (:choice "," :blank))
                         :blank)
                        ">>")
  constant_bit_string_segment (:seq
                               (:field :value _constant_value)
                               (:choice
                                (:field :options
                                 (:seq
                                  ":"
                                  (:alias
                                   constant_bit_string_segment_options
                                   bit_string_segment_options)))
                                :blank))
  constant_bit_string_segment_options (:seq
                                       _constant_bit_string_segment_option
                                       (:repeat (:seq "-" _constant_bit_string_segment_option))
                                       (:choice "-" :blank))
  _constant_bit_string_segment_option (:choice _constant_bit_string_named_segment_option integer)
  _constant_bit_string_named_segment_option (:alias
                                             (:choice
                                              _bit_string_segment_option
                                              _constant_bit_string_segment_option_size)
                                             bit_string_segment_option)
  _constant_bit_string_segment_option_size (:seq "size" "(" integer ")")
  constant_record (:seq
                   (:field :name (:choice constructor_name remote_constructor_name))
                   (:choice (:field :arguments (:alias constant_record_arguments arguments)) :blank))
  constant_record_arguments (:seq
                             "("
                             (:choice
                              (:seq
                               (:alias constant_record_argument argument)
                               (:repeat (:seq "," (:alias constant_record_argument argument)))
                               (:choice "," :blank))
                              :blank)
                             ")")
  constant_record_argument (:choice
                            (:seq
                             (:choice (:seq (:field :label label) ":") :blank)
                             (:field :value _constant_value))
                            (:seq (:field :label label) ":"))
  constant_binary_expression (:choice
                              (:prec-left 7
                               (:seq
                                (:field :left _constant_value)
                                (:field :operator "<>")
                                (:field :right _constant_value))))
  constant_field_access (:seq (:field :record identifier) "." (:field :field label))
  _constant_type (:choice
                  type_hole
                  (:alias constant_tuple_type tuple_type)
                  (:alias constant_function_type function_type)
                  (:alias constant_type type))
  _constant_type_annotation (:seq ":" (:field :type _constant_type))
  constant_tuple_type (:seq
                       "#"
                       "("
                       (:choice
                        (:seq
                         _constant_type
                         (:repeat (:seq "," _constant_type))
                         (:choice "," :blank))
                        :blank)
                       ")")
  constant_function_type (:seq
                          "fn"
                          (:choice
                           (:field :parameter_types
                            (:alias constant_function_parameter_types function_parameter_types))
                           :blank)
                          "->"
                          (:field :return_type _constant_type))
  constant_function_parameter_types (:seq
                                     "("
                                     (:choice
                                      (:seq
                                       _constant_type
                                       (:repeat (:seq "," _constant_type))
                                       (:choice "," :blank))
                                      :blank)
                                     ")")
  constant_type (:seq
                 (:field :name (:choice type_identifier remote_type_identifier))
                 (:choice
                  (:field :arguments (:alias constant_type_arguments type_arguments))
                  :blank))
  constant_type_arguments (:seq
                           "("
                           (:choice
                            (:seq
                             (:alias constant_type_argument type_argument)
                             (:repeat (:seq "," (:alias constant_type_argument type_argument)))
                             (:choice "," :blank))
                            :blank)
                           ")")
  constant_type_argument _constant_type
  external_type (:prec-right 0
                 (:seq
                  (:choice visibility_modifier :blank)
                  (:choice "external" :blank)
                  "type"
                  type_name))
  external_function (:seq
                     (:choice visibility_modifier :blank)
                     "external"
                     "fn"
                     (:field :name identifier)
                     (:field :parameters (:alias external_function_parameters function_parameters))
                     "->"
                     (:field :return_type _type)
                     "="
                     (:field :body external_function_body))
  external_function_parameters (:seq
                                "("
                                (:choice
                                 (:seq
                                  (:alias external_function_parameter function_parameter)
                                  (:repeat
                                   (:seq
                                    ","
                                    (:alias external_function_parameter function_parameter)))
                                  (:choice "," :blank))
                                 :blank)
                                ")")
  external_function_parameter (:seq
                               (:choice (:seq (:field :name identifier) ":") :blank)
                               (:field :type _type))
  external_function_body (:seq string string)
  function (:prec-right 0
            (:seq
             (:choice visibility_modifier :blank)
             "fn"
             (:field :name identifier)
             (:field :parameters function_parameters)
             (:choice (:seq "->" (:field :return_type _type)) :blank)
             (:choice (:field :body block) :blank)))
  function_parameters (:seq
                       "("
                       (:choice
                        (:seq
                         function_parameter
                         (:repeat (:seq "," function_parameter))
                         (:choice "," :blank))
                        :blank)
                       ")")
  function_parameter (:seq
                      (:choice
                       _labeled_discard_param
                       _discard_param
                       _labeled_name_param
                       _name_param)
                      (:choice _type_annotation :blank))
  _labeled_discard_param (:seq (:field :label label) (:field :name discard))
  _discard_param (:field :name discard)
  _labeled_name_param (:seq (:field :label label) (:field :name identifier))
  _name_param (:field :name identifier)
  _statement_seq (:repeat1 _statement)
  _statement (:choice _expression let let_assert use assert)
  _expression (:choice _expression_unit binary_expression)
  binary_expression (:choice
                     (:prec-left 1
                      (:seq
                       (:field :left _expression)
                       (:field :operator "||")
                       (:field :right _expression)))
                     (:prec-left 2
                      (:seq
                       (:field :left _expression)
                       (:field :operator "&&")
                       (:field :right _expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left _expression)
                       (:field :operator "==")
                       (:field :right _expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left _expression)
                       (:field :operator "!=")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator "<")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator "<=")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator "<.")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator "<=.")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator ">")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator ">=")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator ">.")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator ">=.")
                       (:field :right _expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left _expression)
                       (:field :operator "|>")
                       (:field :right (:choice pipeline_echo _expression))))
                     (:prec-left 6
                      (:seq
                       (:field :left _expression)
                       (:field :operator "+")
                       (:field :right _expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left _expression)
                       (:field :operator "+.")
                       (:field :right _expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left _expression)
                       (:field :operator "-")
                       (:field :right _expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left _expression)
                       (:field :operator "-.")
                       (:field :right _expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left _expression)
                       (:field :operator "*")
                       (:field :right _expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left _expression)
                       (:field :operator "*.")
                       (:field :right _expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left _expression)
                       (:field :operator "/")
                       (:field :right _expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left _expression)
                       (:field :operator "/.")
                       (:field :right _expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left _expression)
                       (:field :operator "%")
                       (:field :right _expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left _expression)
                       (:field :operator "<>")
                       (:field :right _expression))))
  _expression_unit (:choice
                    string
                    integer
                    float
                    record
                    identifier
                    todo
                    panic
                    tuple
                    echo
                    list
                    (:alias _expression_bit_string bit_string)
                    anonymous_function
                    block
                    case
                    boolean_negation
                    integer_negation
                    record_update
                    tuple_access
                    field_access
                    function_call)
  record (:seq
          (:field :name (:choice constructor_name remote_constructor_name))
          (:choice (:field :arguments arguments) :blank))
  todo (:prec-left 0
        (:seq
         "todo"
         (:choice
          (:choice
           (:seq "(" (:field :message string) ")")
           (:seq "as" (:field :message _expression)))
          :blank)))
  panic (:prec-left 0
         (:seq
          "panic"
          (:choice
           (:choice
            (:seq "(" (:field :message string) ")")
            (:seq "as" (:field :message _expression)))
           :blank)))
  pipeline_echo (:prec-left 0
                 (:seq "echo" (:choice (:seq "as" (:field :message _expression_unit)) :blank)))
  echo (:prec-left 0
        (:seq "echo" _expression (:choice (:seq "as" (:field :message _expression)) :blank)))
  tuple (:seq
         "#"
         "("
         (:choice (:seq _expression (:repeat (:seq "," _expression)) (:choice "," :blank)) :blank)
         ")")
  list (:seq
        "["
        (:choice
         (:seq
          _expression
          (:choice (:repeat (:seq "," _expression)) :blank)
          (:choice "," :blank)
          (:choice (:seq ".." (:field :spread _expression)) :blank))
         :blank)
        "]")
  _expression_bit_string (:seq
                          "<<"
                          (:choice
                           (:seq
                            (:alias expression_bit_string_segment bit_string_segment)
                            (:repeat
                             (:seq "," (:alias expression_bit_string_segment bit_string_segment)))
                            (:choice "," :blank))
                           :blank)
                          ">>")
  expression_bit_string_segment (:seq
                                 (:field :value _expression_unit)
                                 (:choice
                                  (:field :options
                                   (:seq
                                    ":"
                                    (:alias
                                     expression_bit_string_segment_options
                                     bit_string_segment_options)))
                                  :blank))
  expression_bit_string_segment_options (:seq
                                         _expression_bit_string_segment_option
                                         (:repeat (:seq "-" _expression_bit_string_segment_option))
                                         (:choice "-" :blank))
  _expression_bit_string_segment_option (:choice
                                         _expression_bit_string_named_segment_option
                                         integer)
  _expression_bit_string_named_segment_option (:alias
                                               (:choice
                                                _bit_string_segment_option
                                                _expression_bit_string_segment_option_size)
                                               bit_string_segment_option)
  _expression_bit_string_segment_option_size (:seq "size" "(" _expression ")")
  anonymous_function (:seq
                      "fn"
                      (:field :parameters
                       (:alias anonymous_function_parameters function_parameters))
                      (:choice (:seq "->" (:field :return_type _type)) :blank)
                      (:field :body block))
  anonymous_function_parameters (:seq
                                 "("
                                 (:choice
                                  (:seq
                                   (:alias anonymous_function_parameter function_parameter)
                                   (:repeat
                                    (:seq
                                     ","
                                     (:alias anonymous_function_parameter function_parameter)))
                                   (:choice "," :blank))
                                  :blank)
                                 ")")
  anonymous_function_parameter (:seq
                                (:choice _discard_param _name_param)
                                (:choice _type_annotation :blank))
  block (:seq "{" (:choice _statement_seq :blank) "}")
  case (:seq
        "case"
        (:field :subjects case_subjects)
        "{"
        (:choice (:field :clauses case_clauses) :blank)
        "}")
  case_subjects (:seq (:seq _expression (:repeat (:seq "," _expression)) (:choice "," :blank)))
  case_clauses (:repeat1 case_clause)
  case_clause (:seq
               (:field :patterns case_clause_patterns)
               (:choice (:field :guard case_clause_guard) :blank)
               "->"
               (:field :value _expression))
  case_clause_patterns (:seq
                        (:seq
                         case_clause_pattern
                         (:repeat (:seq "|" case_clause_pattern))
                         (:choice "|" :blank)))
  case_clause_pattern (:seq _pattern (:repeat (:seq "," _pattern)) (:choice "," :blank))
  case_clause_guard (:seq "if" _case_clause_guard_expression)
  _case_clause_guard_expression (:choice
                                 _case_clause_guard_unit
                                 (:alias _case_clause_guard_binary_expression binary_expression)
                                 boolean_negation)
  _case_clause_guard_binary_expression (:choice
                                        (:prec-left 1
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "||")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 2
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "&&")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 3
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "==")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 3
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "!=")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 4
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "<")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 4
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "<=")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 4
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "<.")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 4
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "<=.")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 4
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator ">")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 4
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator ">=")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 4
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator ">.")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 4
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator ">=.")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 5
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "+")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 5
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "+.")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 5
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "-")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 5
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "-.")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 6
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "*")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 6
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "*.")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 6
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "/")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 6
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "/.")
                                          (:field :right _case_clause_guard_expression)))
                                        (:prec-left 6
                                         (:seq
                                          (:field :left _case_clause_guard_expression)
                                          (:field :operator "%")
                                          (:field :right _case_clause_guard_expression))))
  _case_clause_guard_unit (:choice
                           identifier
                           (:prec 1 (:alias _case_clause_tuple_access tuple_access))
                           (:seq "{" _case_clause_guard_expression "}")
                           _constant_value)
  _case_clause_tuple_access (:seq (:field :tuple identifier) "." (:field :index integer))
  let_assert (:seq
              "let"
              "assert"
              _assignment
              (:choice (:seq "as" (:field :message _expression)) :blank))
  assert (:seq
          "assert"
          (:field :value _expression)
          (:choice (:seq "as" (:field :message _expression)) :blank))
  let (:seq "let" _assignment)
  use (:seq
       "use"
       (:choice (:field :assignments use_assignments) :blank)
       "<-"
       (:field :value _expression))
  use_assignments (:seq use_assignment (:repeat (:seq "," use_assignment)) (:choice "," :blank))
  use_assignment (:seq _pattern (:choice _type_annotation :blank))
  boolean_negation (:seq "!" _expression_unit)
  integer_negation (:seq "-" _expression_unit)
  _assignment (:seq
               (:field :pattern _pattern)
               (:choice _type_annotation :blank)
               "="
               (:field :value _expression))
  record_update (:seq
                 (:field :constructor (:choice constructor_name remote_constructor_name))
                 "("
                 ".."
                 (:field :spread _expression)
                 ","
                 (:field :arguments record_update_arguments)
                 ")")
  record_update_arguments (:seq
                           record_update_argument
                           (:repeat (:seq "," record_update_argument))
                           (:choice "," :blank))
  record_update_argument (:choice
                          (:seq (:field :label label) ":" (:field :value _expression))
                          (:seq (:field :label label) ":"))
  _maybe_tuple_expression (:choice
                           identifier
                           function_call
                           tuple
                           block
                           case
                           field_access
                           tuple_access)
  tuple_access (:prec-left 0
                (:seq (:field :tuple _maybe_tuple_expression) "." (:field :index integer)))
  _maybe_record_expression (:choice
                            record
                            identifier
                            function_call
                            block
                            case
                            record_update
                            field_access
                            tuple_access)
  field_access (:prec-left 0
                (:seq (:field :record _maybe_record_expression) "." (:field :field label)))
  _maybe_function_expression (:choice
                              identifier
                              anonymous_function
                              block
                              case
                              tuple_access
                              field_access
                              function_call)
  arguments (:seq
             "("
             (:choice (:seq argument (:repeat (:seq "," argument)) (:choice "," :blank)) :blank)
             ")")
  argument (:choice
            (:seq
             (:choice (:seq (:field :label label) ":") :blank)
             (:field :value (:choice hole _expression)))
            (:seq (:field :label label) ":"))
  hole _discard_name
  function_call (:seq (:field :function _maybe_function_expression) (:field :arguments arguments))
  _pattern_expression (:choice
                       identifier
                       discard
                       record_pattern
                       string
                       integer
                       float
                       tuple_pattern
                       (:alias _pattern_bit_string bit_string_pattern)
                       list_pattern
                       (:alias _pattern_binary_expression binary_expression))
  _pattern_binary_expression (:choice
                              (:prec-left 1
                               (:seq
                                (:field :left _pattern_expression)
                                (:field :operator "<>")
                                (:field :right _pattern_expression)))
                              (:prec-left 1
                               (:seq
                                (:field :left string)
                                (:field :operator "as")
                                (:field :right identifier))))
  _pattern (:seq _pattern_expression (:choice (:field :assign (:seq "as" identifier)) :blank))
  record_pattern (:seq
                  (:field :name (:choice constructor_name remote_constructor_name))
                  (:choice (:field :arguments record_pattern_arguments) :blank))
  record_pattern_arguments (:seq
                            "("
                            (:choice
                             (:seq
                              record_pattern_argument
                              (:repeat (:seq "," record_pattern_argument))
                              (:choice "," :blank))
                             :blank)
                            (:choice pattern_spread :blank)
                            ")")
  record_pattern_argument (:choice
                           (:seq
                            (:choice (:seq (:field :label label) ":") :blank)
                            (:field :pattern _pattern))
                           (:seq (:field :label label) ":"))
  pattern_spread (:seq ".." (:choice "," :blank))
  tuple_pattern (:seq
                 "#"
                 "("
                 (:choice (:seq _pattern (:repeat (:seq "," _pattern)) (:choice "," :blank)) :blank)
                 ")")
  _pattern_bit_string (:seq
                       "<<"
                       (:choice
                        (:seq
                         (:alias pattern_bit_string_segment bit_string_segment)
                         (:repeat (:seq "," (:alias pattern_bit_string_segment bit_string_segment)))
                         (:choice "," :blank))
                        :blank)
                       ">>")
  pattern_bit_string_segment (:seq
                              (:field :value _pattern)
                              (:choice
                               (:field :options
                                (:seq
                                 ":"
                                 (:alias
                                  pattern_bit_string_segment_options
                                  bit_string_segment_options)))
                               :blank))
  pattern_bit_string_segment_options (:seq
                                      _pattern_bit_string_segment_option
                                      (:repeat (:seq "-" _pattern_bit_string_segment_option))
                                      (:choice "-" :blank))
  _pattern_bit_string_segment_option (:choice _pattern_bit_string_named_segment_option integer)
  _pattern_bit_string_named_segment_option (:alias
                                            (:choice
                                             _bit_string_segment_option
                                             _pattern_bit_string_segment_option_size)
                                            bit_string_segment_option)
  _pattern_bit_string_segment_option_size (:seq "size" "(" _pattern_bit_string_segment_argument ")")
  _pattern_bit_string_segment_argument (:choice identifier integer)
  list_pattern (:seq
                "["
                (:choice (:seq _pattern (:repeat (:seq "," _pattern)) (:choice "," :blank)) :blank)
                (:choice list_pattern_tail :blank)
                "]")
  list_pattern_tail (:seq ".." (:choice (:choice identifier discard) :blank))
  visibility_modifier "pub"
  opacity_modifier "opaque"
  type_definition (:seq
                   (:choice visibility_modifier :blank)
                   (:choice opacity_modifier :blank)
                   "type"
                   type_name
                   "{"
                   data_constructors
                   "}")
  data_constructors (:repeat1 data_constructor)
  data_constructor (:seq
                    (:choice attribute :blank)
                    (:field :name constructor_name)
                    (:choice (:field :arguments data_constructor_arguments) :blank))
  data_constructor_arguments (:seq
                              "("
                              (:choice
                               (:seq
                                data_constructor_argument
                                (:repeat (:seq "," data_constructor_argument))
                                (:choice "," :blank))
                               :blank)
                              ")")
  data_constructor_argument (:seq
                             (:choice (:seq (:field :label label) ":") :blank)
                             (:field :value _type))
  type_alias (:seq
              (:choice visibility_modifier :blank)
              (:choice opacity_modifier :blank)
              "type"
              type_name
              "="
              _type)
  string (:seq "\"" (:repeat (:choice escape_sequence quoted_content)) (:token-immediate "\""))
  escape_sequence (:choice
                   (:token-immediate (:pattern "\\\\[efnrt\\\"\\\\]"))
                   (:token-immediate (:pattern "\\\\u\\{[0-9a-fA-F]{1,6}\\}")))
  float (:pattern "-?[0-9_]+\\.[0-9_]*(e-?[0-9_]+)?")
  integer (:seq (:choice "-" :blank) (:choice _hex _decimal _octal _binary))
  _hex (:pattern "0[xX][0-9a-fA-F_]+")
  _decimal (:pattern "[0-9][0-9_]*")
  _octal (:pattern "0[oO][0-7_]+")
  _binary (:pattern "0[bB][0-1_]+")
  _bit_string_segment_option (:choice
                              "binary"
                              "bytes"
                              "int"
                              "float"
                              "bit_string"
                              "bits"
                              "utf8"
                              "utf16"
                              "utf32"
                              "utf8_codepoint"
                              "utf16_codepoint"
                              "utf32_codepoint"
                              "signed"
                              "unsigned"
                              "big"
                              "little"
                              "native"
                              (:seq "unit" "(" integer ")"))
  _type (:choice type_hole tuple_type function_type type type_var)
  _type_annotation (:seq ":" (:field :type _type))
  type_hole _discard_name
  tuple_type (:seq
              "#"
              "("
              (:choice (:seq _type (:repeat (:seq "," _type)) (:choice "," :blank)) :blank)
              ")")
  function_type (:seq
                 "fn"
                 (:choice (:field :parameter_types function_parameter_types) :blank)
                 "->"
                 (:field :return_type _type))
  function_parameter_types (:seq
                            "("
                            (:choice
                             (:seq _type (:repeat (:seq "," _type)) (:choice "," :blank))
                             :blank)
                            ")")
  type (:seq
        (:field :name (:choice type_identifier remote_type_identifier))
        (:choice (:field :arguments type_arguments) :blank))
  type_arguments (:seq
                  "("
                  (:choice
                   (:seq type_argument (:repeat (:seq "," type_argument)) (:choice "," :blank))
                   :blank)
                  ")")
  type_argument _type
  type_var _name
  type_name (:seq
             (:field :name (:choice type_identifier remote_type_identifier))
             (:choice (:field :parameters type_parameters) :blank))
  type_parameters (:seq
                   "("
                   (:choice
                    (:seq type_parameter (:repeat (:seq "," type_parameter)) (:choice "," :blank))
                    :blank)
                   ")")
  type_parameter _name
  identifier _name
  label _name
  discard _discard_name
  type_identifier _upname
  remote_type_identifier (:seq (:field :module identifier) "." (:field :name type_identifier))
  constructor_name _upname
  remote_constructor_name (:seq (:field :module identifier) "." (:field :name constructor_name))
  _discard_name (:pattern "_[_0-9a-z]*")
  _name (:pattern "[_a-z][_0-9a-z]*")
  _upname (:pattern "[A-Z][0-9a-zA-Z]*")}}
