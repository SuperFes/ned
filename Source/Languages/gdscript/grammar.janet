# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "gdscript"
 :word _identifier
 :extras [comment (:pattern "[\\s\\uFEFF\\u2060\\u200B]") line_continuation]
 :conflicts []
 :precedences []
 :externals [_newline
             _indent
             _dedent
             _string_start
             _string_content
             _string_end
             _string_name_start
             _node_path_start
             "]"
             ")"
             "}"
             ","
             _body_end]
 :inline [_simple_statement _compound_statement]
 :supertypes [_compound_statement
              _pattern
              _expression
              _primary_expression
              _attribute_expression
              _parameters]
 :rules
 {source (:repeat _statement)
  _identifier (:pattern "[a-zA-Z_][a-zA-Z_0-9]*")
  identifier _identifier
  name _identifier
  region_start (:seq (:token (:prec 100 "#region")) (:choice region_label :blank))
  region_end (:token (:seq (:prec 100 "#endregion") (:choice (:pattern "[^\\r\\n]*") :blank)))
  region_label (:pattern "[^\\r\\n]+")
  comment (:token (:seq "#" (:pattern ".*")))
  (:ref "true") "true"
  (:ref "false") "false"
  null "null"
  static_keyword "static"
  remote_keyword (:choice "remote" "master" "puppet" "remotesync" "mastersync" "puppetsync")
  escape_sequence (:token
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "u[a-fA-F\\d]{4}")
                     (:pattern "U[a-fA-F\\d]{6}")
                     (:pattern "x[a-fA-F\\d]{2}")
                     (:pattern "o\\d{3}")
                     (:pattern "\\r\\n")
                     (:pattern "[^uxo]"))))
  string (:seq
          (:alias _string_start "\"")
          (:repeat (:choice escape_sequence _string_content))
          (:alias _string_end "\""))
  float (:token
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
           (:seq (:pattern "[eE][\\+-]?") (:repeat1 (:pattern "[0-9]+_?"))))))
  integer (:token
           (:choice
            (:seq (:choice "0x" "0X") (:repeat1 (:pattern "_?[A-Fa-f0-9]+")))
            (:seq (:choice "0o" "0O") (:repeat1 (:pattern "_?[0-7]+")))
            (:seq (:choice "0b" "0B") (:repeat1 (:pattern "_?[0-1]+")))
            (:repeat1 (:pattern "[0-9]+_?"))))
  string_name (:seq
               (:alias _string_name_start "&\"")
               (:repeat (:choice escape_sequence _string_content))
               (:alias _string_end "\""))
  node_path (:seq
             (:alias _node_path_start "^\"")
             (:repeat (:choice escape_sequence _string_content))
             (:alias _string_end "\""))
  get_node (:prec-right 0
            (:seq
             (:choice
              (:seq
               "$"
               (:choice
                (:alias string "value")
                (:seq (:choice "/" :blank) _identifier (:repeat (:seq "/" _identifier)))))
              (:seq
               "%"
               (:choice (:alias string "value") (:seq _identifier (:repeat (:seq "/" _identifier))))))))
  type (:prec 20 (:choice attribute identifier subscript))
  _statement (:choice _simple_statements _compound_statement)
  body (:choice
        _simple_statements
        _newline
        _body_end
        (:seq _indent (:repeat _statement) (:choice _body_end _dedent)))
  _simple_statements (:seq
                      (:seq
                       (:seq _simple_statement (:repeat (:seq (:repeat1 ";") _simple_statement)))
                       (:choice (:repeat1 ";") :blank))
                      (:choice _newline _body_end))
  _simple_statement (:choice
                     _annotations
                     signal_statement
                     class_name_statement
                     extends_statement
                     expression_statement
                     export_variable_statement
                     onready_variable_statement
                     variable_statement
                     const_statement
                     return_statement
                     pass_statement
                     break_statement
                     breakpoint_statement
                     continue_statement
                     region_start
                     region_end)
  expression_statement (:choice _expression assignment augmented_assignment)
  annotation (:prec-right 0 (:seq "@" identifier (:choice (:field :arguments arguments) :blank)))
  _annotations (:repeat1 annotation)
  annotations _annotations
  inferred_type (:choice ":=" (:seq ":" "="))
  _variable_assignment (:seq "=" (:field :value _rhs_expression))
  _variable_inferred_type_assignment (:seq
                                      (:field :type inferred_type)
                                      (:field :value _rhs_expression))
  _variable_typed_assignment (:seq ":" (:field :type type) "=" (:field :value _rhs_expression))
  _variable_typed_definition (:choice (:seq ":" (:field :type type)) _variable_typed_assignment)
  set_body (:seq "set" parameters ":" (:field :body body))
  get_body (:seq "get" (:choice (:alias parameters "()") :blank) ":" (:field :body body))
  _set_assign (:seq "set" "=" (:field :set setter))
  _get_assign (:seq "get" "=" (:field :get getter))
  _setget_assign (:choice
                  (:seq _set_assign (:choice (:seq "," _get_assign) :blank))
                  (:seq _get_assign (:choice (:seq "," _set_assign) :blank)))
  _setget_body (:seq
                ":"
                (:choice
                 _setget_assign
                 (:seq
                  _indent
                  (:choice
                   (:seq (:field :set set_body) (:choice (:field :get get_body) :blank))
                   (:seq (:field :get get_body) (:choice (:field :set set_body) :blank))
                   _setget_assign)
                  _dedent)))
  setter _identifier
  getter _identifier
  setget (:choice
          _setget_body
          (:seq "setget" (:choice setter (:seq setter "," getter) (:seq "," getter))))
  _variable_statement (:seq
                       (:choice annotations :blank)
                       (:choice (:field :static static_keyword) :blank)
                       "var"
                       (:field :name name)
                       (:choice
                        (:choice
                         _variable_typed_definition
                         _variable_inferred_type_assignment
                         _variable_assignment)
                        :blank)
                       (:choice (:field :setget setget) :blank))
  variable_statement (:seq (:choice remote_keyword :blank) _variable_statement)
  export_variable_statement (:seq
                             "export"
                             (:choice (:field :arguments arguments) :blank)
                             (:choice (:choice "onready" remote_keyword) :blank)
                             _variable_statement)
  onready_variable_statement (:seq "onready" _variable_statement)
  const_statement (:seq
                   "const"
                   (:field :name name)
                   (:choice
                    _variable_inferred_type_assignment
                    _variable_typed_assignment
                    _variable_assignment))
  return_statement (:seq "return" (:choice _rhs_expression :blank))
  pass_statement (:prec-left 0 "pass")
  break_statement (:prec-left 0 "break")
  breakpoint_statement "breakpoint"
  continue_statement (:prec-left 0 "continue")
  signal_statement (:seq
                    "signal"
                    (:field :name name)
                    (:choice (:field :parameters parameters) :blank))
  class_name_statement (:seq
                        (:choice annotations :blank)
                        "class_name"
                        (:field :name name)
                        (:choice (:seq "," (:field :icon_path string)) :blank)
                        (:field :extends (:choice extends_statement :blank)))
  extends_statement (:prec 20 (:seq "extends" (:choice string type)))
  _compound_statement (:choice
                       if_statement
                       for_statement
                       while_statement
                       function_definition
                       constructor_definition
                       class_definition
                       enum_definition
                       match_statement)
  if_statement (:seq
                "if"
                (:field :condition _expression)
                ":"
                (:field :body body)
                (:repeat (:field :alternative elif_clause))
                (:choice (:field :alternative else_clause) :blank))
  elif_clause (:seq "elif" (:field :condition _expression) ":" (:field :body body))
  else_clause (:seq "else" ":" (:field :body body))
  for_statement (:seq
                 "for"
                 (:field :left identifier)
                 (:choice (:seq ":" (:field :type type)) :blank)
                 "in"
                 (:field :right _expression)
                 ":"
                 (:field :body body))
  while_statement (:seq "while" (:field :condition _expression) ":" (:field :body body))
  class_definition (:seq
                    (:choice annotations :blank)
                    "class"
                    (:field :name name)
                    (:choice (:field :extends extends_statement) :blank)
                    ":"
                    (:field :body class_body))
  class_body (:choice
              _class_member
              _newline
              _body_end
              (:seq _indent (:repeat _class_member) (:choice _body_end _dedent)))
  _class_member (:choice _simple_class_members _compound_class_member)
  _simple_class_members (:seq
                         (:seq
                          (:seq
                           _simple_class_member
                           (:repeat (:seq (:repeat1 ";") _simple_class_member)))
                          (:choice (:repeat1 ";") :blank))
                         (:choice _newline _body_end))
  _simple_class_member (:choice
                        const_statement
                        extends_statement
                        pass_statement
                        signal_statement
                        variable_statement)
  _compound_class_member (:choice class_definition enum_definition function_definition)
  enum_definition (:seq "enum" (:choice (:field :name name) :blank) (:field :body enumerator_list))
  enumerator_list (:seq
                   "{"
                   (:seq (:seq enumerator (:repeat (:seq "," enumerator))) (:choice "," :blank))
                   "}")
  _enumerator_expression (:choice
                          integer
                          binary_operator
                          identifier
                          unary_operator
                          attribute
                          subscript
                          call
                          parenthesized_expression)
  enumerator (:seq
              (:field :left identifier)
              (:choice (:seq "=" (:field :right _enumerator_expression)) :blank))
  match_statement (:seq "match" (:field :value _expression) ":" (:field :body match_body))
  match_body (:seq
              _indent
              (:repeat1
               (:seq
                (:choice (:repeat (:seq annotation (:choice _newline :blank))) :blank)
                pattern_section))
              _dedent)
  pattern_guard (:seq "when" _expression)
  pattern_section (:seq
                   (:seq _pattern (:repeat (:seq "," _pattern)))
                   (:choice pattern_guard :blank)
                   ":"
                   (:field :body body))
  _pattern (:choice _primary_expression conditional_expression pattern_binding)
  pattern_binding (:seq "var" identifier)
  pattern_open_ending ".."
  _expression (:choice _primary_expression conditional_expression)
  _primary_expression (:choice
                       binary_operator
                       identifier
                       string
                       integer
                       float
                       (:ref "true")
                       (:ref "false")
                       null
                       unary_operator
                       string_name
                       node_path
                       get_node
                       attribute
                       subscript
                       base_call
                       call
                       array
                       dictionary
                       parenthesized_expression
                       await_expression)
  _rhs_expression (:choice _expression lambda)
  _attribute_expression (:prec 19
                         (:choice
                          binary_operator
                          identifier
                          string
                          integer
                          float
                          (:ref "true")
                          (:ref "false")
                          null
                          unary_operator
                          node_path
                          get_node
                          subscript
                          base_call
                          call
                          array
                          dictionary
                          parenthesized_expression))
  binary_operator (:choice
                   (:prec-left 5
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op (:seq "not" "in"))
                     (:field :right _primary_expression)))
                   (:prec-left 5
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "in")
                     (:field :right _primary_expression)))
                   (:prec-left 4
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "and")
                     (:field :right _primary_expression)))
                   (:prec-left 4
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "&&")
                     (:field :right _primary_expression)))
                   (:prec-left 3
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "or")
                     (:field :right _primary_expression)))
                   (:prec-left 3
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "||")
                     (:field :right _primary_expression)))
                   (:prec-left 11
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "+")
                     (:field :right _primary_expression)))
                   (:prec-left 11
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "-")
                     (:field :right _primary_expression)))
                   (:prec-left 12
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "*")
                     (:field :right _primary_expression)))
                   (:prec-left 12
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "/")
                     (:field :right _primary_expression)))
                   (:prec-left 12
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "**")
                     (:field :right _primary_expression)))
                   (:prec-left 12
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "%")
                     (:field :right _primary_expression)))
                   (:prec-left 7
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "|")
                     (:field :right _primary_expression)))
                   (:prec-left 8
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "&")
                     (:field :right _primary_expression)))
                   (:prec-left 9
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "^")
                     (:field :right _primary_expression)))
                   (:prec-left 10
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "<<")
                     (:field :right _primary_expression)))
                   (:prec-left 10
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op ">>")
                     (:field :right _primary_expression)))
                   (:prec-left 6
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "<")
                     (:field :right _primary_expression)))
                   (:prec-left 6
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "<=")
                     (:field :right _primary_expression)))
                   (:prec-left 6
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "==")
                     (:field :right _primary_expression)))
                   (:prec-left 6
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "!=")
                     (:field :right _primary_expression)))
                   (:prec-left 6
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op ">=")
                     (:field :right _primary_expression)))
                   (:prec-left 6
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op ">")
                     (:field :right _primary_expression)))
                   (:prec-left 16
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "as")
                     (:field :right _primary_expression)))
                   (:prec-left 15
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op (:seq "is" "not"))
                     (:field :right _primary_expression)))
                   (:prec-left 15
                    (:seq
                     (:field :left _primary_expression)
                     (:field :op "is")
                     (:field :right _primary_expression))))
  unary_operator (:choice
                  (:prec 14 (:seq (:choice "not" "!") _primary_expression))
                  (:prec 14 (:seq "-" _primary_expression))
                  (:prec 14 (:seq "+" _primary_expression))
                  (:prec 14 (:seq "~" _primary_expression)))
  subscript_arguments (:seq
                       "["
                       (:seq
                        (:seq _rhs_expression (:repeat (:seq "," _rhs_expression)))
                        (:choice "," :blank))
                       "]")
  subscript (:prec 18 (:seq _primary_expression (:field :arguments subscript_arguments)))
  attribute_call (:prec 18 (:seq identifier (:field :arguments arguments)))
  attribute_subscript (:prec 18 (:seq identifier (:field :arguments subscript_arguments)))
  attribute (:prec 18
             (:seq
              _attribute_expression
              (:repeat1 (:seq "." (:choice attribute_subscript attribute_call identifier)))))
  conditional_expression (:prec-right -1
                          (:seq
                           (:field :left _expression)
                           "if"
                           (:field :condition _expression)
                           "else"
                           (:field :right _expression)))
  parenthesized_expression (:prec 1 (:seq "(" _rhs_expression ")"))
  await_expression (:seq "await" _expression)
  assignment (:seq (:field :left _expression) "=" (:field :right _rhs_expression))
  augmented_assignment (:seq
                        (:field :left _expression)
                        (:field :op
                         (:choice "+=" "-=" "*=" "/=" "**=" "%=" ">>=" "<<=" "&=" "^=" "|="))
                        (:field :right _rhs_expression))
  pair (:seq
        (:choice (:seq (:field :left _rhs_expression) ":") (:seq (:field :left identifier) "="))
        (:field :value (:choice _rhs_expression pattern_binding)))
  dictionary (:seq
              "{"
              (:choice
               (:seq
                (:seq
                 (:choice pair _primary_expression)
                 (:repeat (:seq "," (:choice pair _primary_expression))))
                (:choice "," :blank))
               :blank)
              (:choice pattern_open_ending :blank)
              "}")
  array (:seq
         "["
         (:choice
          (:seq
           (:seq
            (:choice _rhs_expression pattern_binding)
            (:repeat (:seq "," (:choice _rhs_expression pattern_binding))))
           (:choice "," :blank))
          :blank)
         (:choice pattern_open_ending :blank)
         "]")
  typed_parameter (:prec -1 (:seq identifier ":" (:field :type type)))
  default_parameter (:seq identifier "=" (:field :value _rhs_expression))
  typed_default_parameter (:prec -1
                           (:choice
                            (:seq
                             identifier
                             ":"
                             (:field :type type)
                             "="
                             (:field :value _rhs_expression))
                            (:seq
                             identifier
                             (:field :type inferred_type)
                             (:field :value _rhs_expression))))
  variadic_parameter (:seq "..." _parameters)
  _parameters (:choice
               identifier
               typed_parameter
               default_parameter
               typed_default_parameter
               variadic_parameter)
  parameters (:seq
              "("
              (:choice
               (:seq (:seq _parameters (:repeat (:seq "," _parameters))) (:choice "," :blank))
               :blank)
              ")")
  _return_type (:seq "->" (:field :return_type type))
  function_definition (:seq
                       (:choice annotations :blank)
                       (:choice (:choice static_keyword remote_keyword) :blank)
                       "func"
                       (:choice (:field :name name) :blank)
                       (:field :parameters parameters)
                       (:choice _return_type :blank)
                       (:choice (:seq ":" (:field :body body)) _newline _body_end))
  lambda (:seq
          "func"
          (:choice (:field :name name) :blank)
          (:field :parameters parameters)
          (:choice _return_type :blank)
          ":"
          (:field :body body))
  constructor_definition (:seq
                          "func"
                          "_init"
                          (:field :parameters parameters)
                          (:choice (:seq "." (:field :arguments arguments)) :blank)
                          (:choice _return_type :blank)
                          ":"
                          (:field :body body))
  arguments (:seq
             "("
             (:choice
              (:seq
               (:seq _rhs_expression (:repeat (:seq "," _rhs_expression)))
               (:choice "," :blank))
              :blank)
             ")")
  base_call (:prec 17 (:seq "." identifier (:field :arguments arguments)))
  call (:prec 17 (:seq _primary_expression (:field :arguments arguments)))
  line_continuation (:token (:seq "\\" (:pattern "\\r?\\n")))}}
