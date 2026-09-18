# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "starlark"
 :word identifier
 :inherits "python"
 :extras [comment (:pattern "[\\s\\f\\uFEFF\\u2060\\u200B]|\\r?\\n") line_continuation]
 :conflicts [[primary_expression pattern]
             [primary_expression list_splat_pattern]
             [tuple tuple_pattern]
             [list list_pattern]
             [with_item _collection_elements]
             [named_expression as_pattern]
             [print_statement primary_expression]
             [type_alias_statement primary_expression]]
 :precedences []
 :externals [_newline
             _indent
             _dedent
             string_start
             _string_content
             escape_interpolation
             string_end
             comment
             "]"
             ")"
             "}"
             "except"]
 :inline [_simple_statement
          _compound_statement
          _suite
          _expressions
          _left_hand_side
          keyword_identifier]
 :supertypes [_simple_statement _compound_statement expression primary_expression pattern parameter]
 :rules
 {module (:repeat _statement)
  _statement (:choice _simple_statements _compound_statement)
  _simple_statements (:seq
                      (:seq _simple_statement (:repeat (:seq ";" _simple_statement)))
                      (:choice ";" :blank)
                      _newline)
  _simple_statement (:choice
                     print_statement
                     assert_statement
                     expression_statement
                     return_statement
                     delete_statement
                     pass_statement
                     break_statement
                     continue_statement
                     exec_statement)
  import_statement (:seq "import" _import_list)
  import_prefix (:repeat1 ".")
  relative_import (:seq import_prefix (:choice dotted_name :blank))
  future_import_statement (:seq
                           "from"
                           "__future__"
                           "import"
                           (:choice _import_list (:seq "(" _import_list ")")))
  import_from_statement (:seq
                         "from"
                         (:field :module_name (:choice relative_import dotted_name))
                         "import"
                         (:choice wildcard_import _import_list (:seq "(" _import_list ")")))
  _import_list (:seq
                (:seq
                 (:field :name (:choice dotted_name aliased_import))
                 (:repeat (:seq "," (:field :name (:choice dotted_name aliased_import)))))
                (:choice "," :blank))
  aliased_import (:seq (:field :name dotted_name) "as" (:field :alias identifier))
  wildcard_import "*"
  print_statement (:choice
                   (:prec 1
                    (:seq
                     "print"
                     chevron
                     (:repeat (:seq "," (:field :argument expression)))
                     (:choice "," :blank)))
                   (:prec -3
                    (:prec-dynamic -1
                     (:seq
                      "print"
                      (:seq
                       (:field :argument expression)
                       (:repeat (:seq "," (:field :argument expression))))
                      (:choice "," :blank)))))
  chevron (:seq ">>" expression)
  assert_statement (:seq
                    (:choice
                     (:seq
                      (:alias "assert" assert_keyword)
                      (:choice
                       (:seq "." (:alias (:choice "eq" "ne" "contains" "fails") assert_builtin))
                       :blank))
                     (:alias
                      (:choice "assert_" "assert_eq" "assert_ne" "assert_contains" "assert_fails")
                      assert_keyword))
                    (:seq expression (:repeat (:seq "," expression))))
  expression_statement (:choice
                        expression
                        (:seq
                         (:seq expression (:repeat (:seq "," expression)))
                         (:choice "," :blank))
                        assignment
                        augmented_assignment)
  named_expression (:seq (:field :name _named_expression_lhs) ":=" (:field :value expression))
  _named_expression_lhs (:choice identifier keyword_identifier)
  return_statement (:seq "return" (:choice _expressions :blank))
  delete_statement (:seq "del" _expressions)
  _expressions (:choice expression expression_list)
  raise_statement (:seq
                   "raise"
                   (:choice _expressions :blank)
                   (:choice (:seq "from" (:field :cause expression)) :blank))
  pass_statement (:prec-left 0 "pass")
  break_statement (:prec-left 0 "break")
  continue_statement (:prec-left 0 "continue")
  _compound_statement (:choice
                       if_statement
                       for_statement
                       while_statement
                       with_statement
                       function_definition
                       decorated_definition
                       match_statement)
  if_statement (:seq
                "if"
                (:field :condition expression)
                ":"
                (:field :consequence _suite)
                (:repeat (:field :alternative elif_clause))
                (:choice (:field :alternative else_clause) :blank))
  elif_clause (:seq "elif" (:field :condition expression) ":" (:field :consequence _suite))
  else_clause (:seq "else" ":" (:field :body _suite))
  match_statement (:seq
                   "match"
                   (:seq
                    (:field :subject expression)
                    (:repeat (:seq "," (:field :subject expression))))
                   (:choice "," :blank)
                   ":"
                   (:field :body (:alias _match_block block)))
  _match_block (:choice (:seq _indent (:repeat (:field :alternative case_clause)) _dedent) _newline)
  case_clause (:seq
               "case"
               (:seq case_pattern (:repeat (:seq "," case_pattern)))
               (:choice "," :blank)
               (:choice (:field :guard if_clause) :blank)
               ":"
               (:field :consequence _suite))
  for_statement (:seq
                 (:choice "async" :blank)
                 "for"
                 (:field :left _left_hand_side)
                 "in"
                 (:field :right _expressions)
                 ":"
                 (:field :body _suite)
                 (:field :alternative (:choice else_clause :blank)))
  while_statement (:seq
                   "while"
                   (:field :condition expression)
                   ":"
                   (:field :body _suite)
                   (:choice (:field :alternative else_clause) :blank))
  try_statement (:seq
                 "try"
                 ":"
                 (:field :body _suite)
                 (:choice
                  (:seq
                   (:repeat1 except_clause)
                   (:choice else_clause :blank)
                   (:choice finally_clause :blank))
                  (:seq
                   (:repeat1 except_group_clause)
                   (:choice else_clause :blank)
                   (:choice finally_clause :blank))
                  finally_clause))
  except_clause (:seq
                 "except"
                 (:choice
                  (:seq expression (:choice (:seq (:choice "as" ",") expression) :blank))
                  :blank)
                 ":"
                 _suite)
  except_group_clause (:seq
                       "except*"
                       (:seq expression (:choice (:seq "as" expression) :blank))
                       ":"
                       _suite)
  finally_clause (:seq "finally" ":" _suite)
  with_statement (:seq (:choice "async" :blank) "with" with_clause ":" (:field :body _suite))
  with_clause (:choice
               (:seq (:seq with_item (:repeat (:seq "," with_item))) (:choice "," :blank))
               (:seq "(" (:seq with_item (:repeat (:seq "," with_item))) (:choice "," :blank) ")"))
  with_item (:prec-dynamic 1 (:seq (:field :value expression)))
  function_definition (:seq
                       (:choice "async" :blank)
                       "def"
                       (:field :name identifier)
                       (:field :type_parameters (:choice type_parameter :blank))
                       (:field :parameters parameters)
                       (:choice (:seq "->" (:field :return_type type)) :blank)
                       ":"
                       (:field :body _suite))
  parameters (:seq "(" (:choice _parameters :blank) ")")
  lambda_parameters _parameters
  list_splat (:seq "*" expression)
  dictionary_splat (:seq "**" expression)
  global_statement (:seq "global" (:seq identifier (:repeat (:seq "," identifier))))
  nonlocal_statement (:seq "nonlocal" (:seq identifier (:repeat (:seq "," identifier))))
  exec_statement (:seq
                  "exec"
                  (:field :code (:choice string identifier))
                  (:choice (:seq "in" (:seq expression (:repeat (:seq "," expression)))) :blank))
  type_alias_statement (:prec-dynamic 1 (:seq "type" type "=" type))
  class_definition (:seq
                    "class"
                    (:field :name identifier)
                    (:field :type_parameters (:choice type_parameter :blank))
                    (:field :superclasses (:choice argument_list :blank))
                    ":"
                    (:field :body _suite))
  type_parameter (:seq "[" (:seq type (:repeat (:seq "," type))) (:choice "," :blank) "]")
  parenthesized_list_splat (:prec 1
                            (:seq
                             "("
                             (:choice
                              (:alias parenthesized_list_splat parenthesized_expression)
                              list_splat)
                             ")"))
  argument_list (:seq
                 "("
                 (:choice
                  (:seq
                   (:choice
                    expression
                    list_splat
                    dictionary_splat
                    (:alias parenthesized_list_splat parenthesized_expression)
                    keyword_argument)
                   (:repeat
                    (:seq
                     ","
                     (:choice
                      expression
                      list_splat
                      dictionary_splat
                      (:alias parenthesized_list_splat parenthesized_expression)
                      keyword_argument))))
                  :blank)
                 (:choice "," :blank)
                 ")")
  decorated_definition (:seq (:repeat1 decorator) (:field :definition function_definition))
  decorator (:seq "@" expression _newline)
  _suite (:choice (:alias _simple_statements block) (:seq _indent block) (:alias _newline block))
  block (:seq (:repeat _statement) _dedent)
  expression_list (:prec-right 0
                   (:seq
                    expression
                    (:choice "," (:seq (:repeat1 (:seq "," expression)) (:choice "," :blank)))))
  dotted_name (:prec 1 (:seq identifier (:repeat (:seq "." identifier))))
  case_pattern (:prec 1 (:choice (:alias _as_pattern as_pattern) keyword_pattern _simple_pattern))
  _simple_pattern (:prec 1
                   (:choice
                    class_pattern
                    splat_pattern
                    union_pattern
                    (:alias _list_pattern list_pattern)
                    (:alias _tuple_pattern tuple_pattern)
                    dict_pattern
                    string
                    concatenated_string
                    (:ref "true")
                    (:ref "false")
                    none
                    (:seq (:choice "-" :blank) (:choice integer float))
                    complex_pattern
                    dotted_name
                    "_"))
  _as_pattern (:seq case_pattern "as" identifier)
  union_pattern (:prec-right 0
                 (:seq _simple_pattern (:repeat1 (:prec-left 0 (:seq "|" _simple_pattern)))))
  _list_pattern (:seq
                 "["
                 (:choice
                  (:seq (:seq case_pattern (:repeat (:seq "," case_pattern))) (:choice "," :blank))
                  :blank)
                 "]")
  _tuple_pattern (:seq
                  "("
                  (:choice
                   (:seq (:seq case_pattern (:repeat (:seq "," case_pattern))) (:choice "," :blank))
                   :blank)
                  ")")
  dict_pattern (:seq
                "{"
                (:choice
                 (:seq
                  (:seq
                   (:choice _key_value_pattern splat_pattern)
                   (:repeat (:seq "," (:choice _key_value_pattern splat_pattern))))
                  (:choice "," :blank))
                 :blank)
                "}")
  _key_value_pattern (:seq (:field :key _simple_pattern) ":" (:field :value case_pattern))
  keyword_pattern (:seq identifier "=" _simple_pattern)
  splat_pattern (:prec 1 (:seq (:choice "*" "**") (:choice identifier "_")))
  class_pattern (:seq
                 dotted_name
                 "("
                 (:choice
                  (:seq (:seq case_pattern (:repeat (:seq "," case_pattern))) (:choice "," :blank))
                  :blank)
                 ")")
  complex_pattern (:prec 1
                   (:seq
                    (:choice "-" :blank)
                    (:choice integer float)
                    (:choice "+" "-")
                    (:choice integer float)))
  _parameters (:seq (:seq parameter (:repeat (:seq "," parameter))) (:choice "," :blank))
  _patterns (:seq (:seq pattern (:repeat (:seq "," pattern))) (:choice "," :blank))
  parameter (:choice
             identifier
             typed_parameter
             default_parameter
             typed_default_parameter
             list_splat_pattern
             tuple_pattern
             keyword_separator
             positional_separator
             dictionary_splat_pattern)
  pattern (:choice
           identifier
           keyword_identifier
           subscript
           attribute
           list_splat_pattern
           tuple_pattern
           list_pattern)
  tuple_pattern (:seq "(" (:choice _patterns :blank) ")")
  list_pattern (:seq "[" (:choice _patterns :blank) "]")
  default_parameter (:seq
                     (:field :name (:choice identifier tuple_pattern))
                     "="
                     (:field :value expression))
  typed_default_parameter (:prec -1
                           (:seq
                            (:field :name identifier)
                            ":"
                            (:field :type type)
                            "="
                            (:field :value expression)))
  list_splat_pattern (:seq "*" (:choice identifier keyword_identifier subscript attribute))
  dictionary_splat_pattern (:seq "**" (:choice identifier keyword_identifier subscript attribute))
  as_pattern (:prec-left 0
              (:seq expression "as" (:field :alias (:alias expression as_pattern_target))))
  _expression_within_for_in_clause (:choice expression (:alias lambda_within_for_in_clause lambda))
  expression (:choice
              comparison_operator
              not_operator
              boolean_operator
              lambda
              primary_expression
              conditional_expression
              named_expression
              as_pattern)
  primary_expression (:choice
                      binary_operator
                      identifier
                      keyword_identifier
                      string
                      integer
                      float
                      (:ref "true")
                      (:ref "false")
                      none
                      unary_operator
                      attribute
                      subscript
                      call
                      list
                      list_comprehension
                      dictionary
                      dictionary_comprehension
                      set
                      set_comprehension
                      tuple
                      parenthesized_expression
                      ellipsis
                      (:alias list_splat_pattern list_splat))
  not_operator (:prec 12 (:seq "not" (:field :argument expression)))
  boolean_operator (:choice
                    (:prec-left 11
                     (:seq
                      (:field :left expression)
                      (:field :operator "and")
                      (:field :right expression)))
                    (:prec-left 10
                     (:seq
                      (:field :left expression)
                      (:field :operator "or")
                      (:field :right expression))))
  binary_operator (:choice
                   (:prec-left 18
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "+")
                     (:field :right primary_expression)))
                   (:prec-left 18
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "-")
                     (:field :right primary_expression)))
                   (:prec-left 19
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "*")
                     (:field :right primary_expression)))
                   (:prec-left 19
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "@")
                     (:field :right primary_expression)))
                   (:prec-left 19
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "/")
                     (:field :right primary_expression)))
                   (:prec-left 19
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "%")
                     (:field :right primary_expression)))
                   (:prec-left 19
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "//")
                     (:field :right primary_expression)))
                   (:prec-right 21
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "**")
                     (:field :right primary_expression)))
                   (:prec-left 14
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "|")
                     (:field :right primary_expression)))
                   (:prec-left 15
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "&")
                     (:field :right primary_expression)))
                   (:prec-left 16
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "^")
                     (:field :right primary_expression)))
                   (:prec-left 17
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator "<<")
                     (:field :right primary_expression)))
                   (:prec-left 17
                    (:seq
                     (:field :left primary_expression)
                     (:field :operator ">>")
                     (:field :right primary_expression))))
  unary_operator (:prec 20
                  (:seq
                   (:field :operator (:choice "+" "-" "~"))
                   (:field :argument primary_expression)))
  _not_in (:seq "not" "in")
  _is_not (:seq "is" "not")
  comparison_operator (:prec-left 13
                       (:seq
                        primary_expression
                        (:repeat1
                         (:seq
                          (:field :operators
                           (:choice
                            "<"
                            "<="
                            "=="
                            "!="
                            ">="
                            ">"
                            "<>"
                            "in"
                            (:alias (:seq "not" "in") "not in")))
                          primary_expression))))
  lambda (:prec -2
          (:seq
           "lambda"
           (:field :parameters (:choice lambda_parameters :blank))
           ":"
           (:field :body expression)))
  lambda_within_for_in_clause (:seq
                               "lambda"
                               (:field :parameters (:choice lambda_parameters :blank))
                               ":"
                               (:field :body _expression_within_for_in_clause))
  assignment (:seq
              (:field :left _left_hand_side)
              (:choice
               (:seq "=" (:field :right _right_hand_side))
               (:seq ":" (:field :type type))
               (:seq ":" (:field :type type) "=" (:field :right _right_hand_side))))
  augmented_assignment (:seq
                        (:field :left _left_hand_side)
                        (:field :operator
                         (:choice
                          "+="
                          "-="
                          "*="
                          "/="
                          "@="
                          "//="
                          "%="
                          "**="
                          ">>="
                          "<<="
                          "&="
                          "^="
                          "|="))
                        (:field :right _right_hand_side))
  _left_hand_side (:choice pattern pattern_list)
  pattern_list (:seq
                pattern
                (:choice "," (:seq (:repeat1 (:seq "," pattern)) (:choice "," :blank))))
  _right_hand_side (:choice expression expression_list assignment augmented_assignment pattern_list)
  yield (:prec-right 0
         (:seq "yield" (:choice (:seq "from" expression) (:choice _expressions :blank))))
  attribute (:prec 22 (:seq (:field :object primary_expression) "." (:field :attribute identifier)))
  subscript (:prec 22
             (:seq
              (:field :value primary_expression)
              "["
              (:seq
               (:field :subscript (:choice expression slice))
               (:repeat (:seq "," (:field :subscript (:choice expression slice)))))
              (:choice "," :blank)
              "]"))
  slice (:seq
         (:choice expression :blank)
         ":"
         (:choice expression :blank)
         (:choice (:seq ":" (:choice expression :blank)) :blank))
  ellipsis "..."
  call (:prec 22 (:seq (:field :function primary_expression) (:field :arguments argument_list)))
  typed_parameter (:prec -1
                   (:seq
                    (:choice identifier list_splat_pattern dictionary_splat_pattern)
                    ":"
                    (:field :type type)))
  type (:choice expression splat_type generic_type union_type constrained_type member_type)
  splat_type (:prec 1 (:seq (:choice "*" "**") identifier))
  generic_type (:prec 1 (:seq identifier type_parameter))
  union_type (:prec-left 0 (:seq type "|" type))
  constrained_type (:prec-right 0 (:seq type ":" type))
  member_type (:seq type "." identifier)
  keyword_argument (:seq
                    (:field :name (:choice identifier keyword_identifier))
                    "="
                    (:field :value expression))
  list (:seq "[" (:choice _collection_elements :blank) "]")
  set (:seq "{" _collection_elements "}")
  tuple (:seq "(" (:choice _collection_elements :blank) ")")
  dictionary (:seq
              "{"
              (:choice
               (:seq
                (:choice pair dictionary_splat)
                (:repeat (:seq "," (:choice pair dictionary_splat))))
               :blank)
              (:choice "," :blank)
              "}")
  pair (:seq (:field :key expression) ":" (:field :value expression))
  list_comprehension (:seq "[" (:field :body expression) _comprehension_clauses "]")
  dictionary_comprehension (:seq "{" (:field :body pair) _comprehension_clauses "}")
  set_comprehension (:seq "{" (:field :body expression) _comprehension_clauses "}")
  generator_expression (:seq "(" (:field :body expression) _comprehension_clauses ")")
  _comprehension_clauses (:seq for_in_clause (:repeat (:choice for_in_clause if_clause)))
  parenthesized_expression (:prec 1 (:seq "(" expression ")"))
  _collection_elements (:seq
                        (:seq
                         (:choice expression list_splat parenthesized_list_splat)
                         (:repeat
                          (:seq "," (:choice expression list_splat parenthesized_list_splat))))
                        (:choice "," :blank))
  for_in_clause (:prec-left 0
                 (:seq
                  (:choice "async" :blank)
                  "for"
                  (:field :left _left_hand_side)
                  "in"
                  (:field :right
                   (:seq
                    _expression_within_for_in_clause
                    (:repeat (:seq "," _expression_within_for_in_clause))))
                  (:choice "," :blank)))
  if_clause (:seq "if" expression)
  conditional_expression (:prec-right -1 (:seq expression "if" expression "else" expression))
  concatenated_string (:seq string (:repeat1 string))
  string (:seq string_start (:repeat (:choice interpolation string_content)) string_end)
  string_content (:prec-right 0
                  (:repeat1
                   (:choice
                    escape_interpolation
                    escape_sequence
                    _not_escape_sequence
                    _string_content)))
  interpolation (:seq
                 "{"
                 (:field :expression _f_expression)
                 (:choice "=" :blank)
                 (:choice (:field :type_conversion type_conversion) :blank)
                 (:choice (:field :format_specifier format_specifier) :blank)
                 "}")
  _f_expression (:choice expression expression_list pattern_list)
  escape_sequence (:token-immediate
                   (:prec 1
                    (:seq
                     "\\"
                     (:choice
                      (:pattern "u[a-fA-F\\d]{4}")
                      (:pattern "U[a-fA-F\\d]{8}")
                      (:pattern "x[a-fA-F\\d]{2}")
                      (:pattern "\\d{1,3}")
                      (:pattern "\\r?\\n")
                      (:pattern "['\"abfrntv\\\\]")
                      (:pattern "N\\{[^}]+\\}")))))
  _not_escape_sequence (:token-immediate "\\")
  format_specifier (:seq
                    ":"
                    (:repeat
                     (:choice
                      (:token (:prec 1 (:pattern "[^{}\\n]+")))
                      (:alias interpolation format_expression))))
  type_conversion (:pattern "![a-z]")
  integer (:token
           (:choice
            (:seq
             (:choice "0x" "0X")
             (:repeat1 (:pattern "_?[A-Fa-f0-9]+"))
             (:choice (:pattern "[Ll]") :blank))
            (:seq
             (:choice "0o" "0O")
             (:repeat1 (:pattern "_?[0-7]+"))
             (:choice (:pattern "[Ll]") :blank))
            (:seq
             (:choice "0b" "0B")
             (:repeat1 (:pattern "_?[0-1]+"))
             (:choice (:pattern "[Ll]") :blank))
            (:seq
             (:repeat1 (:pattern "[0-9]+_?"))
             (:choice (:choice (:pattern "[Ll]") :blank) (:choice (:pattern "[jJ]") :blank)))))
  float (:token
         (:seq
          (:choice
           (:seq
            (:repeat1 (:pattern "[0-9]+_?"))
            "."
            (:choice (:repeat1 (:pattern "[0-9]+_?")) :blank)
            (:choice (:seq (:pattern "[eE][\\+-]?") (:repeat1 (:pattern "[0-9]+_?"))) :blank))
           (:seq
            (:choice (:repeat1 (:pattern "[0-9]+_?")) :blank)
            "."
            (:repeat1 (:pattern "[0-9]+_?"))
            (:choice (:seq (:pattern "[eE][\\+-]?") (:repeat1 (:pattern "[0-9]+_?"))) :blank))
           (:seq
            (:repeat1 (:pattern "[0-9]+_?"))
            (:seq (:pattern "[eE][\\+-]?") (:repeat1 (:pattern "[0-9]+_?")))))
          (:choice (:pattern "[jJ]") :blank)))
  identifier (:pattern "[_\\p{XID_Start}][_\\p{XID_Continue}]*")
  keyword_identifier (:choice
                      (:prec -3
                       (:alias (:choice "print" "exec" "async" "await" "match" "struct") identifier))
                      (:alias "type" identifier))
  (:ref "true") "True"
  (:ref "false") "False"
  none "None"
  await (:prec 20 (:seq "await" primary_expression))
  comment (:token (:seq "#" (:pattern ".*")))
  line_continuation (:token (:seq "\\" (:choice (:seq (:choice "\r" :blank) "\n") "\0")))
  positional_separator "/"
  keyword_separator "*"}}
