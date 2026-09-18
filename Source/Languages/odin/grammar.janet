# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "odin"
 :word identifier
 :extras [comment block_comment (:pattern "\\s") _backslash]
 :conflicts [[array_type] [_expression_no_tag struct]]
 :precedences []
 :externals [_newline _backslash _nl_comma float block_comment "{" "\""]
 :inline []
 :supertypes [declaration expression literal statement]
 :rules
 {source_file (:seq (:repeat (:seq declaration _separator)) (:choice declaration :blank))
  block (:prec 2
         (:seq
          "{"
          (:choice
           (:seq
            (:seq (:choice tag :blank) statement)
            (:repeat (:seq _separator (:choice (:seq (:choice tag :blank) statement) :blank))))
           :blank)
          "}"))
  tagged_block (:seq tag block)
  declaration (:choice
               build_tag
               package_declaration
               import_declaration
               procedure_declaration
               overloaded_procedure_declaration
               struct_declaration
               enum_declaration
               union_declaration
               bit_field_declaration
               variable_declaration
               var_declaration
               const_declaration
               const_type_declaration
               foreign_block
               when_statement
               _expression_no_tag)
  build_tag (:seq "#+" (:pattern ".+"))
  package_declaration (:seq "package" identifier)
  import_declaration (:seq
                      (:choice attributes :blank)
                      (:choice "foreign" :blank)
                      "import"
                      (:choice (:field :alias identifier) :blank)
                      (:choice
                       string
                       (:seq
                        "{"
                        (:seq
                         (:choice string identifier)
                         (:repeat (:seq "," (:choice string identifier))))
                        (:choice "," :blank)
                        "}")))
  procedure_declaration (:seq
                         (:choice attributes :blank)
                         expression
                         "::"
                         (:choice tag :blank)
                         procedure)
  procedure (:prec-right 0
             (:seq
              "proc"
              (:choice calling_convention :blank)
              parameters
              (:choice
               (:seq "->" (:choice tag :blank) (:choice type named_type) (:choice tag :blank))
               :blank)
              (:choice where_clause :blank)
              (:choice tag :blank)
              (:choice (:choice block uninitialized) :blank)))
  where_clause (:prec-right 0
                (:seq
                 "where"
                 (:seq (:prec-right 0 expression) (:repeat (:seq "," (:prec-right 0 expression))))))
  calling_convention (:choice
                      "\"odin\""
                      "\"contextless\""
                      "\"stdcall\""
                      "\"std\""
                      "\"cdecl\""
                      "\"c\""
                      "\"fastcall\""
                      "\"fast\""
                      "\"none\""
                      "\"system\"")
  overloaded_procedure_declaration (:seq
                                    (:choice attributes :blank)
                                    expression
                                    "::"
                                    "proc"
                                    "{"
                                    (:choice
                                     (:seq
                                      (:seq expression (:repeat (:seq "," expression)))
                                      (:choice "," :blank))
                                     :blank)
                                    "}")
  struct_declaration (:seq
                      (:choice attributes :blank)
                      expression
                      "::"
                      "struct"
                      (:choice polymorphic_parameters :blank)
                      (:repeat (:seq tag (:choice (:choice identifier number) :blank)))
                      (:choice where_clause :blank)
                      "{"
                      (:choice
                       (:seq (:seq field (:repeat (:seq "," field))) (:choice "," :blank))
                       :blank)
                      "}")
  enum_declaration (:seq
                    (:choice attributes :blank)
                    (:choice "using" :blank)
                    expression
                    "::"
                    "enum"
                    (:choice type :blank)
                    "{"
                    (:choice
                     (:seq
                      (:seq
                       (:seq identifier (:choice (:seq "=" expression) :blank))
                       (:repeat (:seq "," (:seq identifier (:choice (:seq "=" expression) :blank)))))
                      (:choice "," :blank))
                     :blank)
                    "}")
  union_declaration (:seq
                     (:choice attributes :blank)
                     expression
                     "::"
                     "union"
                     (:choice polymorphic_parameters :blank)
                     (:choice tag :blank)
                     "{"
                     (:choice
                      (:seq (:seq type (:repeat (:seq "," type))) (:choice "," :blank))
                      :blank)
                     "}")
  bit_field_declaration (:seq
                         (:choice attributes :blank)
                         expression
                         "::"
                         "bit_field"
                         type
                         "{"
                         (:choice
                          (:seq
                           (:seq
                            (:seq identifier ":" type "|" expression)
                            (:repeat (:seq "," (:seq identifier ":" type "|" expression))))
                           (:choice "," :blank))
                          :blank)
                         "}")
  variable_declaration (:seq
                        (:choice attributes :blank)
                        (:seq expression (:repeat (:seq "," expression)))
                        ":="
                        (:seq
                         (:choice expression procedure)
                         (:repeat (:seq "," (:choice expression procedure))))
                        (:choice "," :blank))
  const_declaration (:seq
                     (:choice attributes :blank)
                     (:seq expression (:repeat (:seq "," expression)))
                     "::"
                     (:choice tag :blank)
                     (:seq
                      (:choice
                       expression
                       (:seq (:alias "#type" tag) type)
                       array_type
                       bit_set_type
                       pointer_type)
                      (:repeat
                       (:seq
                        ","
                        (:choice
                         expression
                         (:seq (:alias "#type" tag) type)
                         array_type
                         bit_set_type
                         pointer_type)))))
  const_type_declaration (:prec 1
                          (:seq (:choice attributes :blank) expression ":" type ":" expression))
  foreign_block (:seq (:choice attributes :blank) "foreign" (:choice identifier :blank) block)
  attributes (:repeat1 attribute)
  attribute (:seq
             "@"
             (:choice
              identifier
              (:seq
               "("
               (:seq
                (:seq identifier (:choice (:seq "=" expression) :blank))
                (:repeat (:seq "," (:seq identifier (:choice (:seq "=" expression) :blank)))))
               ")")))
  parameters (:seq
              "("
              (:choice
               (:seq
                (:seq
                 (:choice parameter default_parameter)
                 (:repeat (:seq "," (:choice parameter default_parameter))))
                (:choice "," :blank))
               :blank)
              ")")
  parameter (:prec-right 0
             (:seq
              (:seq _param_header (:repeat (:seq "," _param_header)))
              (:choice _param_type :blank)))
  _param_header (:seq
                 (:choice tag :blank)
                 (:choice "using" :blank)
                 (:choice "$" :blank)
                 (:choice
                  identifier
                  variadic_type
                  array_type
                  pointer_type
                  field_type
                  _procedure_type))
  _param_type (:seq
               ":"
               (:choice tag :blank)
               type
               (:choice identifier :blank)
               (:choice (:seq "=" expression) :blank))
  default_parameter (:seq (:choice tag :blank) (:choice "using" :blank) identifier ":=" expression)
  polymorphic_parameters (:seq
                          "("
                          (:seq
                           (:seq
                            (:seq
                             (:seq (:choice "$" :blank) identifier)
                             (:repeat (:seq "," (:seq (:choice "$" :blank) identifier))))
                            ":"
                            type)
                           (:repeat
                            (:seq
                             ","
                             (:seq
                              (:seq
                               (:seq (:choice "$" :blank) identifier)
                               (:repeat (:seq "," (:seq (:choice "$" :blank) identifier))))
                              ":"
                              type))))
                          ")")
  field (:prec-right 0
         (:seq
          (:seq
           (:seq (:choice tag :blank) (:choice "using" :blank) identifier)
           (:repeat (:seq "," (:seq (:choice tag :blank) (:choice "using" :blank) identifier))))
          ":"
          (:choice tag :blank)
          type
          (:choice string :blank)))
  statement (:prec 1
             (:choice
              procedure_declaration
              overloaded_procedure_declaration
              struct_declaration
              enum_declaration
              union_declaration
              bit_field_declaration
              const_declaration
              import_declaration
              assignment_statement
              update_statement
              if_statement
              when_statement
              for_statement
              switch_statement
              defer_statement
              break_statement
              continue_statement
              fallthrough_statement
              label_statement
              using_statement
              return_statement
              _expression_no_tag
              var_declaration
              foreign_block
              tagged_block
              block))
  assignment_statement (:prec 1
                        (:seq
                         (:choice (:seq attributes (:choice tag :blank)) :blank)
                         (:seq expression (:repeat (:seq "," expression)))
                         (:choice "=" ":=")
                         (:choice tag :blank)
                         (:seq
                          (:choice expression procedure)
                          (:repeat (:seq "," (:choice expression procedure))))))
  update_statement (:seq
                    (:seq expression (:repeat (:seq "," expression)))
                    (:choice "+=" "-=" "*=" "/=" "%=" "&=" "|=" "^=" "<<=" ">>=" "||=" "&&=" "&~=")
                    (:seq expression (:repeat (:seq "," expression))))
  if_statement (:prec-right 0
                (:seq
                 "if"
                 (:choice
                  (:seq
                   (:choice
                    (:field :initializer
                     (:choice assignment_statement update_statement var_declaration))
                    :blank)
                   ";")
                  :blank)
                 (:choice tag :blank)
                 (:field :condition expression)
                 (:choice (:field :consequence block) (:seq "do" (:field :consequence statement)))
                 (:repeat else_if_clause)
                 (:choice else_clause :blank)))
  else_if_clause (:seq
                  "else"
                  "if"
                  (:choice
                   (:seq (:choice (:field :initializer assignment_statement) :blank) ";")
                   :blank)
                  (:field :condition expression)
                  (:choice (:field :consequence block) (:seq "do" (:field :consequence statement))))
  else_clause (:seq
               "else"
               (:choice (:field :consequence block) (:seq "do" (:field :consequence statement))))
  when_statement (:prec-right 0
                  (:seq
                   "when"
                   expression
                   (:choice block (:seq "do" statement))
                   (:repeat else_when_clause)
                   (:choice else_clause :blank)))
  else_when_clause (:seq "else" "when" expression block)
  for_statement (:seq
                 "for"
                 (:choice
                  (:choice
                   (:seq
                    (:choice
                     (:seq
                      (:choice
                       (:field :initializer
                        (:choice assignment_statement update_statement var_declaration))
                       :blank)
                      ";")
                     :blank)
                    (:choice (:field :condition expression) :blank)
                    (:choice
                     (:seq
                      ";"
                      (:choice
                       (:field :post
                        (:choice
                         update_statement
                         (:alias _simple_assignment_statement assignment_statement)))
                       :blank))
                     :blank))
                   _for_in_expression)
                  :blank)
                 (:field :consequence (:choice block (:seq "do" statement))))
  _for_in_expression (:seq
                      (:choice (:seq expression (:repeat (:seq "," expression))) :blank)
                      "in"
                      expression)
  _simple_assignment_statement (:seq
                                (:choice attributes :blank)
                                (:seq expression (:repeat (:seq "," expression)))
                                (:choice "=" ":=")
                                (:seq
                                 (:choice expression)
                                 (:repeat (:seq "," (:choice expression)))))
  switch_statement (:seq
                    "switch"
                    (:choice
                     (:seq
                      (:choice "in" :blank)
                      (:field :condition
                       (:choice
                        expression
                        (:seq assignment_statement _separator (:choice expression :blank)))))
                     :blank)
                    "{"
                    (:repeat switch_case)
                    "}")
  switch_case (:seq
               "case"
               (:choice
                (:seq
                 (:field :condition (:choice expression array_type pointer_type))
                 (:repeat
                  (:seq "," (:field :condition (:choice expression array_type pointer_type)))))
                :blank)
               ":"
               (:choice
                (:seq
                 (:seq (:choice tag :blank) statement)
                 (:repeat (:seq _separator (:choice (:seq (:choice tag :blank) statement) :blank))))
                :blank))
  defer_statement (:seq "defer" statement)
  break_statement (:seq "break" (:choice identifier :blank))
  continue_statement (:seq "continue" (:choice identifier :blank))
  fallthrough_statement "fallthrough"
  var_declaration (:prec-right 0
                   (:seq
                    (:choice attributes :blank)
                    (:seq expression (:repeat (:seq "," expression)))
                    ":"
                    (:choice tag :blank)
                    (:choice
                     (:seq
                      type
                      (:choice
                       (:seq (:choice "=" ":") (:seq expression (:repeat (:seq "," expression))))
                       :blank)))))
  return_statement (:prec-right 1
                    (:seq
                     "return"
                     (:choice tag :blank)
                     (:choice
                      (:seq
                       (:seq
                        (:choice expression _procedure_type)
                        (:repeat
                         (:seq
                          (:choice "," (:alias _nl_comma ","))
                          (:choice expression _procedure_type))))
                       (:choice "," :blank))
                      :blank)))
  label_statement (:seq expression ":" (:choice if_statement for_statement switch_statement block))
  using_statement (:seq "using" expression)
  expression (:prec-left 0 (:choice _expression_no_tag tag))
  _expression_no_tag (:choice
                      unary_expression
                      binary_expression
                      ternary_expression
                      call_expression
                      selector_call_expression
                      member_expression
                      index_expression
                      slice_expression
                      range_expression
                      cast_expression
                      parenthesized_expression
                      in_expression
                      variadic_expression
                      or_return_expression
                      or_continue_expression
                      or_break_expression
                      identifier
                      address
                      map_type
                      distinct_type
                      matrix_type
                      literal
                      "?")
  unary_expression (:prec-right 16
                    (:seq
                     (:field :operator (:choice "+" "-" "~" "!" "&"))
                     (:field :argument expression)))
  binary_expression (:choice
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "||")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "or_else")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator "&&")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator ">")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator ">=")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator "<")
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
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "~=")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
                       (:field :right expression)))
                     (:prec-left 8
                      (:seq
                       (:field :left expression)
                       (:field :operator "~")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "&~")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator "<<")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>")
                       (:field :right expression)))
                     (:prec-left 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "+")
                       (:field :right expression)))
                     (:prec-left 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "-")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator "*")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator "/")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator "%%")
                       (:field :right expression))))
  ternary_expression (:prec-right 0
                      (:seq
                       (:field :condition (:choice _expression_no_tag struct))
                       (:choice
                        (:prec 2
                         (:seq
                          "?"
                          (:field :consequence expression)
                          ":"
                          (:field :alternative expression)))
                        (:seq
                         (:choice "if" "when")
                         (:field :consequence expression)
                         "else"
                         (:field :alternative expression)))))
  call_expression (:prec-left 17
                   (:seq
                    (:field :function (:choice (:seq tag identifier) _expression_no_tag tag))
                    "("
                    (:choice
                     (:seq
                      (:seq
                       (:seq
                        (:field :argument
                         (:choice expression array_type struct_type pointer_type procedure))
                        (:choice (:seq "=" (:choice expression)) :blank))
                       (:repeat
                        (:seq
                         ","
                         (:seq
                          (:field :argument
                           (:choice expression array_type struct_type pointer_type procedure))
                          (:choice (:seq "=" (:choice expression)) :blank)))))
                      (:choice "," :blank))
                     :blank)
                    ")"))
  selector_call_expression (:prec-left 17 (:seq (:field :function expression) "->" call_expression))
  member_expression (:prec-left 18 (:seq (:choice expression :blank) "." expression))
  index_expression (:prec-left 18
                    (:seq expression "[" expression (:choice (:seq "," expression) :blank) "]"))
  slice_expression (:prec-left 18
                    (:seq
                     expression
                     "["
                     (:choice (:field :start expression) :blank)
                     ":"
                     (:choice (:field :end expression) :blank)
                     "]"))
  range_expression (:prec-left 18 (:seq expression (:choice "..=" "..<") expression))
  cast_expression (:prec-left 14
                   (:seq
                    (:choice
                     (:seq
                      "("
                      (:choice pointer_type array_type _procedure_type)
                      ")"
                      (:choice expression :blank))
                     (:seq (:choice "cast" "transmute") "(" type ")" expression)
                     (:seq "auto_cast" expression))))
  in_expression (:prec-right -1 (:seq expression (:choice "in" "not_in") expression))
  variadic_expression (:prec-left 20 (:seq ".." expression))
  parenthesized_expression (:seq "(" expression ")")
  or_return_expression (:seq expression "or_return")
  or_continue_expression (:prec-right 0
                          (:seq
                           expression
                           "or_continue"
                           (:field :label (:choice identifier :blank))))
  or_break_expression (:prec-right 0 (:seq expression "or_break" (:choice expression :blank)))
  address (:seq expression "^")
  type (:prec-right 0
        (:choice
         identifier
         pointer_type
         variadic_type
         array_type
         map_type
         union_type
         bit_set_type
         matrix_type
         field_type
         tuple_type
         struct_type
         enum_type
         bit_field_type
         constant_type
         specialized_type
         _procedure_type
         distinct_type
         empty_type
         polymorphic_type
         conditional_type
         "..."))
  pointer_type (:prec-left 0 (:seq "^" type))
  variadic_type (:prec-left 0 (:seq ".." type))
  array_type (:prec 1
              (:seq
               (:choice tag :blank)
               "["
               (:choice (:seq (:choice "$" :blank) (:choice "dynamic" "^" "?" expression)) :blank)
               "]"
               (:choice type :blank)))
  map_type (:prec-right 0 (:seq "map" "[" type "]" type))
  union_type (:prec-right 0
              (:seq "union" "{" (:seq type (:repeat (:seq "," type))) (:choice "," :blank) "}"))
  bit_set_type (:seq
                "bit_set"
                "["
                (:choice constant_type expression)
                (:choice (:seq ";" type) :blank)
                "]")
  matrix_type (:prec-left 0
               (:seq
                "matrix"
                "["
                (:choice constant_type expression)
                ","
                (:choice constant_type expression)
                "]"
                type))
  field_type (:seq identifier (:repeat1 (:seq (:token-immediate ".") identifier)))
  tuple_type (:seq
              "("
              (:choice
               (:seq
                (:seq
                 (:choice type named_type default_type)
                 (:repeat (:seq "," (:choice type named_type default_type))))
                (:choice "," :blank))
               :blank)
              ")")
  struct_type (:prec 1
               (:seq
                "struct"
                (:choice polymorphic_parameters :blank)
                (:repeat (:seq tag (:choice number :blank)))
                (:repeat1 (:seq "{" (:choice _struct_members :blank) "}"))))
  _struct_members (:seq
                   (:seq struct_member (:repeat (:seq "," struct_member)))
                   (:choice "," :blank))
  struct_member (:seq
                 (:seq
                  (:seq (:choice "using" :blank) identifier)
                  (:repeat (:seq "," (:seq (:choice "using" :blank) identifier))))
                 ":"
                 (:choice tag :blank)
                 type
                 (:choice string :blank))
  enum_type (:seq
             "enum"
             (:choice (:field :underlying_type type) :blank)
             "{"
             (:seq
              (:seq identifier (:choice (:seq "=" expression) :blank))
              (:repeat (:seq "," (:seq identifier (:choice (:seq "=" expression) :blank)))))
             (:choice "," :blank)
             "}")
  bit_field_type (:seq
                  "bit_field"
                  type
                  "{"
                  (:seq
                   (:seq identifier ":" type "|" expression)
                   (:repeat (:seq "," (:seq identifier ":" type "|" expression))))
                  (:choice "," :blank)
                  "}")
  named_type (:prec-right 0 (:seq identifier ":" type (:choice (:seq "=" literal) :blank)))
  default_type (:seq identifier ":=" expression)
  constant_type (:prec-right 0 (:seq "$" type))
  specialized_type (:prec-right 0 (:seq type "/" type))
  _procedure_type (:alias procedure procedure_type)
  distinct_type (:prec-right 0 (:seq "distinct" (:choice tag :blank) type))
  empty_type "!"
  polymorphic_type (:seq
                    type
                    "("
                    (:seq (:choice type literal) (:repeat (:seq "," (:choice type literal))))
                    ")")
  conditional_type (:seq "(" type "when" expression "else" type ")")
  literal (:prec-right 0
           (:choice
            struct
            map
            bit_set
            matrix
            float
            number
            string
            character
            boolean
            (:ref "nil")
            uninitialized))
  struct (:seq
          (:choice
           (:choice
            (:seq "[" (:choice (:choice "dynamic" "^" "?" expression) :blank) "]" type)
            (:seq
             (:choice identifier field_identifier)
             (:choice
              (:seq "(" (:choice (:seq identifier (:repeat (:seq "," identifier))) :blank) ")")
              :blank)))
           :blank)
          "{"
          (:choice
           (:seq (:seq struct_field (:repeat (:seq "," struct_field))) (:choice "," :blank))
           :blank)
          "}")
  map (:seq
       "map"
       "["
       type
       "]"
       type
       "{"
       (:choice
        (:seq
         (:seq
          (:seq expression "=" expression)
          (:repeat (:seq "," (:seq expression "=" expression))))
         (:choice "," :blank))
        :blank)
       "}")
  bit_set (:seq
           "bit_set"
           "["
           expression
           (:choice (:seq ";" (:field :underlying_type type)) :blank)
           "]"
           "{"
           (:choice (:seq expression (:repeat (:seq "," expression))) :blank)
           "}")
  matrix (:seq
          "matrix"
          "["
          expression
          ","
          expression
          "]"
          type
          "{"
          (:choice
           (:seq (:seq expression (:repeat (:seq "," expression))) (:choice "," :blank))
           :blank)
          "}")
  struct_field (:prec-right 0
                (:seq expression (:choice (:seq "=" (:choice expression _procedure_type)) :blank)))
  number (:token
          (:choice
           (:seq (:choice "-" :blank) (:pattern "[0-9][0-9_]*[ijk]?"))
           (:seq (:choice "-" :blank) (:pattern "0[xh][0-9a-fA-F_]+[ijk]?"))
           (:seq (:choice "-" :blank) (:pattern "0o[0-7][0-7]*[ijk]?"))
           (:seq (:choice "-" :blank) (:pattern "0b[01][01_]*[ijk]?"))))
  string (:choice _string_literal _raw_string_literal)
  _string_literal (:seq "\"" (:repeat (:choice string_content escape_sequence)) "\"")
  _raw_string_literal (:seq "`" (:repeat (:alias _raw_string_content string_content)) "`")
  character (:seq "'" (:choice (:pattern "[^'\\\\]") escape_sequence) "'")
  string_content (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+")))
  _raw_string_content (:token-immediate (:prec 1 (:pattern "[^`]+")))
  _escape_sequence (:choice
                    (:prec 2 (:token-immediate (:seq "\\" (:pattern "[^abfnrtvxu'\\\"\\\\\\?]"))))
                    (:prec 1 escape_sequence))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu0-7]")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9a-fA-F]{2}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}")
                     (:pattern "U[0-9a-fA-F]{8}"))))
  boolean (:choice "true" "false")
  (:ref "nil") "nil"
  uninitialized "---"
  tag (:token
       (:seq (:pattern "#[a-zA-Z_][a-zA-Z0-9_]*") (:choice (:seq "(" (:pattern "\\w*") ")") :blank)))
  identifier (:pattern "[_\\p{XID_Start}][_\\p{XID_Continue}]*" "u")
  field_identifier (:prec -1 (:seq identifier (:repeat1 (:seq "." identifier))))
  keyword_identifier (:prec -3 (:choice "nil" "false" "true"))
  _separator (:choice _newline ";")
  comment (:token (:seq "//" (:pattern "[^\\r\\n]*")))}}
