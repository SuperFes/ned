# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "solidity"
 :word identifier
 :extras [comment (:pattern "[\\s\\uFEFF\\u2060\\u200B\\u00A0]")]
 :conflicts [[_primary_expression type_name]
             [_primary_expression _identifier_path]
             [_primary_expression member_expression _identifier_path]
             [member_expression _identifier_path]
             [layout_specifier struct_expression]
             [_nameless_parameter parameter]
             [_primary_expression type_cast_expression]
             [variable_declaration_tuple tuple_expression]
             [_yul_expression yul_assignment]
             [yul_label yul_identifier]
             [fallback_receive_definition _function_type]]
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source_file (:seq (:repeat _source_unit))
  _source_unit (:choice _directive _declaration)
  _directive (:choice pragma_directive import_directive)
  pragma_directive (:seq "pragma" (:choice solidity_pragma_token any_pragma_token) _semicolon)
  solidity_pragma_token (:prec 10
                         (:seq
                          _solidity
                          (:repeat
                           (:seq
                            (:field :version_constraint _pragma_version_constraint)
                            (:choice (:choice "||" "-") :blank)))))
  any_pragma_token (:seq identifier pragma_value)
  _solidity (:prec 1 "solidity")
  pragma_value (:prec 0 (:pattern "[^;]+"))
  _pragma_version_constraint (:seq
                              (:choice solidity_version_comparison_operator :blank)
                              solidity_version)
  solidity_version (:pattern "\"?\\.? ?(\\d|\\*)+(\\. ?(\\d|\\*)+ ?(\\.(\\d|\\*)+)?)?\"?")
  solidity_version_comparison_operator (:choice "<=" "<" "^" ">" ">=" "~" "=")
  import_directive (:seq
                    "import"
                    (:choice _source_import (:seq _import_clause _from_clause))
                    _semicolon)
  _source_import (:seq (:field :source string) (:choice _import_alias :blank))
  _import_clause (:choice _single_import _multiple_import)
  _from_clause (:seq "from" (:field :source string))
  _single_import (:seq
                  (:choice "*" (:field :import_name identifier))
                  (:choice _import_alias :blank))
  _multiple_import (:seq
                    "{"
                    (:choice
                     (:seq
                      _import_declaration
                      (:repeat (:seq "," _import_declaration))
                      (:choice "," :blank))
                     :blank)
                    "}")
  _import_declaration (:seq (:field :import_name identifier) (:choice _import_alias :blank))
  _import_alias (:seq "as" (:field :alias identifier))
  _declaration (:choice
                contract_declaration
                interface_declaration
                error_declaration
                library_declaration
                struct_declaration
                enum_declaration
                function_definition
                constant_variable_declaration
                user_defined_type_definition
                event_definition
                using_directive)
  user_defined_type_definition (:seq
                                "type"
                                (:field :name identifier)
                                "is"
                                primitive_type
                                _semicolon)
  constant_variable_declaration (:seq
                                 (:field :type type_name)
                                 "constant"
                                 (:field :name identifier)
                                 "="
                                 (:field :value expression)
                                 _semicolon)
  contract_declaration (:seq
                        (:choice "abstract" :blank)
                        "contract"
                        (:field :name identifier)
                        (:repeat (:choice _class_heritage layout_specifier))
                        (:field :body contract_body))
  error_declaration (:seq
                     "error"
                     (:field :name identifier)
                     "("
                     (:choice
                      (:seq
                       error_parameter
                       (:repeat (:seq "," error_parameter))
                       (:choice "," :blank))
                      :blank)
                     ")"
                     _semicolon)
  error_parameter (:seq (:field :type type_name) (:field :name (:choice identifier :blank)))
  interface_declaration (:seq
                         "interface"
                         (:field :name identifier)
                         (:choice _class_heritage :blank)
                         (:field :body contract_body))
  library_declaration (:seq "library" (:field :name identifier) (:field :body contract_body))
  _class_heritage (:seq
                   "is"
                   (:seq
                    inheritance_specifier
                    (:repeat (:seq "," inheritance_specifier))
                    (:choice "," :blank)))
  layout_specifier (:seq "layout" "at" expression)
  inheritance_specifier (:seq
                         (:field :ancestor user_defined_type)
                         (:choice (:field :ancestor_arguments _call_arguments) :blank))
  contract_body (:seq "{" (:repeat _contract_member) "}")
  _contract_member (:choice
                    function_definition
                    modifier_definition
                    error_declaration
                    state_variable_declaration
                    struct_declaration
                    enum_declaration
                    event_definition
                    using_directive
                    constructor_definition
                    fallback_receive_definition
                    user_defined_type_definition)
  struct_declaration (:seq "struct" (:field :name identifier) (:field :body struct_body))
  struct_member (:seq (:field :type type_name) (:field :name identifier) _semicolon)
  struct_body (:seq "{" (:repeat1 struct_member) "}")
  enum_declaration (:seq "enum" (:field :name identifier) (:field :body enum_body))
  enum_body (:seq
             "{"
             (:choice
              (:seq
               (:alias identifier enum_value)
               (:repeat (:seq "," (:alias identifier enum_value)))
               (:choice "," :blank))
              :blank)
             "}")
  event_definition (:seq
                    "event"
                    (:field :name identifier)
                    _event_parameter_list
                    (:choice "anonymous" :blank)
                    _semicolon)
  _event_parameter_list (:seq
                         "("
                         (:choice
                          (:seq
                           event_parameter
                           (:repeat (:seq "," event_parameter))
                           (:choice "," :blank))
                          :blank)
                         ")")
  event_parameter (:seq
                   (:field :type type_name)
                   (:choice "indexed" :blank)
                   (:choice (:field :name identifier) :blank))
  user_definable_operator (:choice
                           "&"
                           "~"
                           "|"
                           "^"
                           "+"
                           "-"
                           "/"
                           "%"
                           "*"
                           "-"
                           "=="
                           ">"
                           ">="
                           "<"
                           "<="
                           "!=")
  using_directive (:seq
                   "using"
                   (:choice
                    (:alias user_defined_type type_alias)
                    (:seq
                     "{"
                     (:seq using_alias (:repeat (:seq "," using_alias)) (:choice "," :blank))
                     "}"))
                   "for"
                   (:field :source (:choice any_source_type type_name))
                   (:choice "global" :blank)
                   _semicolon)
  using_alias (:seq user_defined_type (:choice (:seq "as" user_definable_operator) :blank))
  any_source_type "*"
  statement (:choice
             block_statement
             expression_statement
             variable_declaration_statement
             if_statement
             for_statement
             while_statement
             do_while_statement
             continue_statement
             break_statement
             try_statement
             return_statement
             emit_statement
             assembly_statement
             revert_statement)
  assembly_statement (:seq
                      "assembly"
                      (:choice "\"evmasm\"" :blank)
                      (:choice assembly_flags :blank)
                      "{"
                      (:repeat _yul_statement)
                      "}")
  assembly_flags (:seq
                  "("
                  (:choice (:seq string (:repeat (:seq "," string)) (:choice "," :blank)) :blank)
                  ")")
  _yul_statement (:choice
                  yul_block
                  yul_variable_declaration
                  yul_assignment
                  yul_function_call
                  yul_if_statement
                  yul_for_statement
                  yul_switch_statement
                  yul_leave
                  yul_break
                  yul_continue
                  yul_function_definition
                  yul_label
                  _yul_literal)
  yul_label (:seq identifier ":")
  yul_leave "leave"
  yul_break "break"
  yul_continue "continue"
  yul_identifier identifier
  _yul_expression (:choice yul_path yul_function_call _yul_literal)
  yul_path (:prec-left 0 (:seq yul_identifier (:repeat (:seq "." yul_identifier))))
  _yul_literal (:choice
                yul_decimal_number
                yul_string_literal
                yul_hex_number
                yul_boolean
                yul_hex_string_literal)
  yul_decimal_number (:pattern "0|([1-9][0-9]*)")
  yul_string_literal string
  yul_hex_number (:pattern "0x[0-9A-Fa-f]*")
  yul_boolean (:choice "true" "false")
  yul_hex_string_literal (:seq
                          "hex"
                          (:choice
                           (:seq
                            "\""
                            (:choice
                             (:seq _hex_digit (:repeat (:seq (:choice "_" :blank) _hex_digit)))
                             :blank)
                            "\"")
                           (:seq
                            "'"
                            (:choice
                             (:seq _hex_digit (:repeat (:seq (:choice "_" :blank) _hex_digit)))
                             :blank)
                            "'")))
  yul_block (:seq "{" (:repeat _yul_statement) "}")
  yul_variable_declaration (:prec-left 1
                            (:choice
                             (:seq
                              "let"
                              (:field :left yul_identifier)
                              (:choice (:seq ":=" (:field :right _yul_expression)) :blank))
                             (:seq
                              "let"
                              (:field :left
                               (:choice
                                (:seq
                                 yul_identifier
                                 (:repeat (:seq "," yul_identifier))
                                 (:choice "," :blank))
                                (:seq
                                 "("
                                 (:seq
                                  yul_identifier
                                  (:repeat (:seq "," yul_identifier))
                                  (:choice "," :blank))
                                 ")")))
                              (:choice (:seq ":=" (:field :right yul_function_call)) :blank))))
  _yul_assignment_operator (:choice ":=" (:seq ":" "="))
  yul_assignment (:prec-left 0
                  (:choice
                   (:seq yul_path _yul_assignment_operator _yul_expression)
                   (:seq
                    (:seq yul_path (:repeat (:seq "," yul_path)) (:choice "," :blank))
                    (:choice (:seq _yul_assignment_operator yul_function_call) :blank))))
  yul_function_call (:choice
                     (:seq
                      (:field :function (:choice yul_identifier yul_evm_builtin))
                      "("
                      (:choice
                       (:seq
                        _yul_expression
                        (:repeat (:seq "," _yul_expression))
                        (:choice "," :blank))
                       :blank)
                      ")")
                     (:field :function yul_evm_builtin))
  yul_if_statement (:seq "if" _yul_expression yul_block)
  yul_for_statement (:seq "for" yul_block _yul_expression yul_block yul_block)
  yul_switch_statement (:seq
                        "switch"
                        _yul_expression
                        (:choice
                         (:seq "default" yul_block)
                         (:seq
                          (:repeat1 (:seq "case" _yul_literal yul_block))
                          (:choice (:seq "default" yul_block) :blank))))
  yul_function_definition (:seq
                           "function"
                           yul_identifier
                           "("
                           (:choice
                            (:seq
                             yul_identifier
                             (:repeat (:seq "," yul_identifier))
                             (:choice "," :blank))
                            :blank)
                           ")"
                           (:choice
                            (:seq
                             "->"
                             (:seq
                              yul_identifier
                              (:repeat (:seq "," yul_identifier))
                              (:choice "," :blank)))
                            :blank)
                           yul_block)
  yul_evm_builtin (:prec 1
                   (:choice
                    "stop"
                    "add"
                    "sub"
                    "mul"
                    "div"
                    "sdiv"
                    "mod"
                    "smod"
                    "exp"
                    "not"
                    "lt"
                    "gt"
                    "slt"
                    "sgt"
                    "eq"
                    "iszero"
                    "and"
                    "or"
                    "xor"
                    "byte"
                    "shl"
                    "shr"
                    "sar"
                    "addmod"
                    "mulmod"
                    "signextend"
                    "keccak256"
                    "pop"
                    "mload"
                    "mcopy"
                    "tload"
                    "tstore"
                    "mstore"
                    "mstore8"
                    "sload"
                    "sstore"
                    "msize"
                    "gas"
                    "address"
                    "balance"
                    "selfbalance"
                    "caller"
                    "callvalue"
                    "calldataload"
                    "calldatasize"
                    "calldatacopy"
                    "extcodesize"
                    "extcodecopy"
                    "returndatasize"
                    "returndatacopy"
                    "extcodehash"
                    "create"
                    "create2"
                    "call"
                    "callcode"
                    "delegatecall"
                    "staticcall"
                    "return"
                    "revert"
                    "selfdestruct"
                    "invalid"
                    "log0"
                    "log1"
                    "log2"
                    "log3"
                    "log4"
                    "chainid"
                    "origin"
                    "gasprice"
                    "blockhash"
                    "blobhash"
                    "basefee"
                    "blobfee"
                    "coinbase"
                    "timestamp"
                    "number"
                    "difficulty"
                    "gaslimit"
                    "prevrandao"
                    "blobbasefee"
                    "blobfee"))
  unchecked "unchecked"
  block_statement (:seq (:choice unchecked :blank) "{" (:repeat statement) "}")
  variable_declaration_statement (:prec 1
                                  (:seq
                                   (:choice
                                    (:seq
                                     variable_declaration
                                     (:choice (:seq "=" (:field :value expression)) :blank))
                                    (:seq variable_declaration_tuple "=" (:field :value expression)))
                                   _semicolon))
  variable_declaration (:seq
                        (:field :type type_name)
                        (:field :location (:choice (:choice "memory" "storage" "calldata") :blank))
                        (:field :name identifier))
  variable_declaration_tuple (:prec 3
                              (:choice
                               (:seq
                                "("
                                (:choice
                                 (:seq
                                  (:choice variable_declaration :blank)
                                  (:repeat (:seq "," (:choice variable_declaration :blank)))
                                  (:choice "," :blank))
                                 :blank)
                                ")")
                               (:seq
                                "var"
                                "("
                                (:choice
                                 (:seq
                                  (:choice identifier :blank)
                                  (:repeat (:seq "," (:choice identifier :blank)))
                                  (:choice "," :blank))
                                 :blank)
                                ")")))
  expression_statement (:seq expression _semicolon)
  if_statement (:prec-right 0
                (:seq
                 "if"
                 "("
                 (:field :condition expression)
                 ")"
                 (:field :body statement)
                 (:field :else (:choice (:seq "else" (:field :body statement)) :blank))))
  for_statement (:seq
                 "for"
                 "("
                 (:field :initial
                  (:choice variable_declaration_statement expression_statement _semicolon))
                 (:field :condition (:choice expression_statement _semicolon))
                 (:field :update (:choice expression :blank))
                 ")"
                 (:field :body statement))
  while_statement (:seq "while" "(" (:field :condition expression) ")" (:field :body statement))
  do_while_statement (:seq
                      "do"
                      (:field :body statement)
                      "while"
                      "("
                      (:field :condition expression)
                      ")"
                      _semicolon)
  continue_statement (:seq "continue" _semicolon)
  break_statement (:seq "break" _semicolon)
  revert_statement (:prec 13
                    (:seq
                     "revert"
                     (:choice (:field :error expression) :blank)
                     (:choice (:alias _call_arguments revert_arguments) :blank)
                     _semicolon))
  try_statement (:seq
                 "try"
                 (:field :attempt expression)
                 (:choice (:seq "returns" _parameter_list) :blank)
                 (:field :body block_statement)
                 (:repeat1 catch_clause))
  catch_clause (:seq
                "catch"
                (:choice (:seq (:choice identifier :blank) _parameter_list) :blank)
                (:field :body block_statement))
  return_statement (:seq "return" (:choice expression :blank) _semicolon)
  emit_statement (:seq "emit" (:field :name expression) _call_arguments _semicolon)
  state_variable_declaration (:seq
                              (:field :type type_name)
                              (:repeat
                               (:choice
                                (:field :visibility visibility)
                                "constant"
                                override_specifier
                                immutable
                                (:field :location state_location)))
                              (:field :name identifier)
                              (:choice (:seq "=" (:field :value expression)) :blank)
                              _semicolon)
  visibility (:choice "public" "internal" "private" "external")
  state_mutability (:choice "pure" "view" "payable")
  state_location (:choice "transient")
  immutable "immutable"
  override_specifier (:seq
                      "override"
                      (:choice
                       (:seq
                        "("
                        (:seq
                         user_defined_type
                         (:repeat (:seq "," user_defined_type))
                         (:choice "," :blank))
                        ")")
                       :blank))
  modifier_definition (:seq
                       "modifier"
                       (:field :name identifier)
                       (:choice _parameter_list :blank)
                       (:repeat (:choice virtual override_specifier))
                       (:choice _semicolon (:field :body function_body)))
  constructor_definition (:seq
                          "constructor"
                          _parameter_list
                          (:repeat
                           (:choice modifier_invocation "payable" (:choice "internal" "public")))
                          (:field :body function_body))
  fallback_receive_definition (:seq
                               (:choice (:seq (:choice "fallback" "receive" "function")) "function")
                               _parameter_list
                               (:repeat
                                (:choice
                                 visibility
                                 modifier_invocation
                                 state_mutability
                                 virtual
                                 override_specifier))
                               (:choice (:seq "returns" _parameter_list) :blank)
                               (:choice _semicolon (:field :body function_body)))
  function_definition (:seq
                       "function"
                       (:field :name identifier)
                       _parameter_list
                       (:repeat
                        (:choice
                         modifier_invocation
                         visibility
                         state_mutability
                         virtual
                         override_specifier))
                       (:field :return_type (:choice return_type_definition :blank))
                       (:choice _semicolon (:field :body function_body)))
  return_type_definition (:seq "returns" _parameter_list)
  virtual "virtual"
  modifier_invocation (:seq _identifier_path (:choice _call_arguments :blank))
  _call_arguments (:prec 4
                   (:seq
                    "("
                    (:choice
                     (:seq call_argument (:repeat (:seq "," call_argument)) (:choice "," :blank))
                     :blank)
                    ")"))
  call_argument (:choice
                 expression
                 (:seq
                  "{"
                  (:choice
                   (:seq
                    call_struct_argument
                    (:repeat (:seq "," call_struct_argument))
                    (:choice "," :blank))
                   :blank)
                  "}"))
  call_struct_argument (:seq (:field :name identifier) ":" (:field :value expression))
  function_body (:seq "{" (:repeat statement) "}")
  expression (:choice
              binary_expression
              unary_expression
              update_expression
              call_expression
              payable_conversion_expression
              meta_type_expression
              _primary_expression
              struct_expression
              ternary_expression
              type_cast_expression)
  _primary_expression (:choice
                       parenthesized_expression
                       member_expression
                       array_access
                       slice_access
                       primitive_type
                       assignment_expression
                       augmented_assignment_expression
                       user_defined_type
                       tuple_expression
                       inline_array_expression
                       identifier
                       _literal
                       new_expression)
  type_cast_expression (:prec-left 0 (:seq primitive_type _call_arguments))
  ternary_expression (:prec-left 0 (:seq expression "?" expression ":" expression))
  new_expression (:prec-left 0
                  (:seq "new" (:field :name type_name) (:choice _call_arguments :blank)))
  tuple_expression (:prec 1
                    (:seq
                     "("
                     (:choice
                      (:seq
                       (:choice expression :blank)
                       (:repeat (:seq "," (:choice expression :blank)))
                       (:choice "," :blank))
                      :blank)
                     ")"))
  inline_array_expression (:seq
                           "["
                           (:choice
                            (:seq expression (:repeat (:seq "," expression)) (:choice "," :blank))
                            :blank)
                           "]")
  binary_expression (:choice
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
                       (:field :operator "==")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "!=")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator "<")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator ">")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator ">=")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
                       (:field :right expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "^")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left 8
                      (:seq
                       (:field :left expression)
                       (:field :operator "<<")
                       (:field :right expression)))
                     (:prec-left 8
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "+")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "-")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "*")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "/")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator "**")
                       (:field :right expression))))
  unary_expression (:choice
                    (:prec-left 12 (:seq (:field :operator "-") (:field :argument expression)))
                    (:prec-left 12 (:seq (:field :operator "delete") (:field :argument expression)))
                    (:prec-left 12 (:seq (:field :operator "!") (:field :argument expression)))
                    (:prec-left 12 (:seq (:field :operator "~") (:field :argument expression))))
  update_expression (:choice
                     (:prec-left 13
                      (:seq (:field :argument expression) (:field :operator (:choice "++" "--"))))
                     (:prec-left 12
                      (:seq (:field :operator (:choice "++" "--")) (:field :argument expression))))
  member_expression (:prec-dynamic 1
                     (:seq
                      (:field :object (:choice expression identifier))
                      "."
                      (:field :property identifier)))
  array_access (:seq (:field :base expression) "[" (:choice (:field :index expression) :blank) "]")
  slice_access (:seq
                (:field :base expression)
                "["
                (:choice (:field :from expression) :blank)
                ":"
                (:choice (:field :to expression) :blank)
                "]")
  struct_expression (:seq
                     (:field :type expression)
                     "{"
                     (:choice
                      (:seq
                       struct_field_assignment
                       (:repeat (:seq "," struct_field_assignment))
                       (:choice "," :blank))
                      :blank)
                     "}")
  struct_field_assignment (:seq (:field :name identifier) ":" (:field :value expression))
  parenthesized_expression (:prec 2 (:seq "(" expression ")"))
  assignment_expression (:prec-right 0
                         (:seq (:field :left expression) "=" (:field :right expression)))
  augmented_assignment_expression (:prec-right 0
                                   (:seq
                                    (:field :left expression)
                                    (:choice "+=" "-=" "*=" "/=" "%=" "^=" "&=" "|=" ">>=" "<<=")
                                    (:field :right expression)))
  call_expression (:prec-right 13 (:seq (:field :function expression) _call_arguments))
  payable_conversion_expression (:seq "payable" _call_arguments)
  meta_type_expression (:seq "type" "(" type_name ")")
  type_name (:choice primitive_type user_defined_type _mapping _array_type _function_type)
  _array_type (:prec 1 (:seq type_name "[" (:choice expression :blank) "]"))
  _function_type (:prec-right 0
                  (:seq
                   "function"
                   (:field :parameters _parameter_list)
                   (:repeat (:choice visibility state_mutability))
                   (:choice _return_parameters :blank)))
  _parameter_list (:seq
                   "("
                   (:choice
                    (:seq parameter (:repeat (:seq "," parameter)) (:choice "," :blank))
                    :blank)
                   ")")
  _return_parameters (:seq
                      "returns"
                      "("
                      (:seq
                       (:alias _nameless_parameter return_parameter)
                       (:repeat (:seq "," (:alias _nameless_parameter return_parameter)))
                       (:choice "," :blank))
                      ")")
  _nameless_parameter (:seq
                       (:field :type type_name)
                       (:field :location (:choice _storage_location :blank)))
  parameter (:seq
             (:field :type type_name)
             (:choice (:field :location _storage_location) :blank)
             (:choice (:field :name identifier) :blank))
  _storage_location (:choice "memory" "storage" "calldata")
  user_defined_type _identifier_path
  _identifier_path (:prec-left 0 (:seq identifier (:repeat (:seq "." identifier))))
  _mapping (:seq
            "mapping"
            "("
            (:field :key_type _mapping_key)
            (:choice (:field :key_identifier identifier) :blank)
            "=>"
            (:field :value_type type_name)
            (:choice (:field :value_identifier identifier) :blank)
            ")")
  _mapping_key (:choice primitive_type user_defined_type)
  primitive_type (:prec-left 0
                  (:choice
                   (:seq "address" (:choice "payable" :blank))
                   "bool"
                   "string"
                   "var"
                   _int
                   _uint
                   _bytes
                   _fixed
                   _ufixed))
  _int (:choice
        "int"
        "int8"
        "int16"
        "int24"
        "int32"
        "int40"
        "int48"
        "int56"
        "int64"
        "int72"
        "int80"
        "int88"
        "int96"
        "int104"
        "int112"
        "int120"
        "int128"
        "int136"
        "int144"
        "int152"
        "int160"
        "int168"
        "int176"
        "int184"
        "int192"
        "int200"
        "int208"
        "int216"
        "int224"
        "int232"
        "int240"
        "int248"
        "int256")
  _uint (:choice
         "uint"
         "uint8"
         "uint16"
         "uint24"
         "uint32"
         "uint40"
         "uint48"
         "uint56"
         "uint64"
         "uint72"
         "uint80"
         "uint88"
         "uint96"
         "uint104"
         "uint112"
         "uint120"
         "uint128"
         "uint136"
         "uint144"
         "uint152"
         "uint160"
         "uint168"
         "uint176"
         "uint184"
         "uint192"
         "uint200"
         "uint208"
         "uint216"
         "uint224"
         "uint232"
         "uint240"
         "uint248"
         "uint256")
  _bytes (:choice
          "byte"
          "bytes"
          "bytes1"
          "bytes2"
          "bytes3"
          "bytes4"
          "bytes5"
          "bytes6"
          "bytes7"
          "bytes8"
          "bytes9"
          "bytes10"
          "bytes11"
          "bytes12"
          "bytes13"
          "bytes14"
          "bytes15"
          "bytes16"
          "bytes17"
          "bytes18"
          "bytes19"
          "bytes20"
          "bytes21"
          "bytes22"
          "bytes23"
          "bytes24"
          "bytes25"
          "bytes26"
          "bytes27"
          "bytes28"
          "bytes29"
          "bytes30"
          "bytes31"
          "bytes32")
  _fixed (:choice "fixed" (:pattern "fixed([0-9]+)x([0-9]+)"))
  _ufixed (:choice "ufixed" (:pattern "ufixed([0-9]+)x([0-9]+)"))
  _semicolon ";"
  identifier (:pattern "[a-zA-Z$_][a-zA-Z0-9$_]*")
  number (:pattern "\\d+")
  _literal (:choice
            string_literal
            number_literal
            boolean_literal
            hex_string_literal
            unicode_string_literal)
  string_literal (:prec-left 0 (:repeat1 string))
  number_literal (:seq (:choice _decimal_number _hex_number) (:choice number_unit :blank))
  _decimal_number (:choice
                   (:pattern "(\\d|_)+(\\.(\\d|_)+)?([eE](-)?(\\d|_)+)?")
                   (:pattern "\\.(\\d|_)+([eE](-)?(\\d|_)+)?"))
  _hex_number (:prec 10 (:pattern "0[xX]([a-fA-F0-9][a-fA-F0-9]?_?)+"))
  _hex_digit (:pattern "([a-fA-F0-9][a-fA-F0-9])")
  number_unit (:choice
               "wei"
               "szabo"
               "finney"
               "gwei"
               "ether"
               "seconds"
               "minutes"
               "hours"
               "days"
               "weeks"
               "years")
  (:ref "true") "true"
  (:ref "false") "false"
  boolean_literal (:choice (:ref "true") (:ref "false"))
  hex_string_literal (:prec-left 0
                      (:repeat1
                       (:seq
                        "hex"
                        (:choice
                         (:seq
                          "\""
                          (:choice
                           (:seq _hex_digit (:repeat (:seq (:choice "_" :blank) _hex_digit)))
                           :blank)
                          "\"")
                         (:seq
                          "'"
                          (:choice
                           (:seq _hex_digit (:repeat (:seq (:choice "_" :blank) _hex_digit)))
                           :blank)
                          "'")))))
  _escape_sequence (:token-immediate
                    (:seq
                     "\\"
                     (:choice
                      (:pattern "[^xu0-7]")
                      (:pattern "[0-7]{1,3}")
                      (:pattern "x[0-9a-fA-F]{2}")
                      (:pattern "u[0-9a-fA-F]{4}")
                      (:pattern "u\\{[0-9a-fA-F]+\\}"))))
  _single_quoted_unicode_char (:token-immediate (:prec 2 (:pattern "[^'\\\\\\n]+|\\\\\\r?\\n")))
  _double_quoted_unicode_char (:token-immediate (:prec 2 (:pattern "[^\"\\\\\\n]+|\\\\\\r?\\n")))
  unicode_string_literal (:prec-left 0
                          (:repeat1
                           (:seq
                            "unicode"
                            (:choice
                             (:seq "\"" (:repeat _double_quoted_unicode_char) "\"")
                             (:seq "'" (:repeat _single_quoted_unicode_char) "'")))))
  string (:choice
          (:seq
           "\""
           (:repeat (:choice _string_immediate_elt_inside_double_quote _escape_sequence))
           "\"")
          (:seq "'" (:repeat (:choice _string_immediate_elt_inside_quote _escape_sequence)) "'"))
  _string_immediate_elt_inside_double_quote (:token-immediate
                                             (:prec 2 (:pattern "[^\"\\\\\\n]+|\\\\\\r?\\n")))
  _string_immediate_elt_inside_quote (:token-immediate
                                      (:prec 2 (:pattern "[^'\\\\\\n]+|\\\\\\r?\\n")))
  comment (:token
           (:prec 1
            (:choice
             (:seq "//" (:pattern "([^\\r\\n])*"))
             (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/"))))}}
