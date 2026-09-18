# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "vala"
 :word identifier
 :extras [(:pattern "\\s|\\\\\\r?\\n") comment _preprocessor_statement]
 :conflicts [[member_declaration_modifier class_declaration]
             [member_declaration_modifier type_declaration_modifier]
             [member_declaration_modifier object_creation_expression]
             [member_declaration_modifier signal_declaration_modifier]
             [member_declaration_modifier type_declaration_modifier signal_declaration_modifier]
             [member_declaration_modifier delegate_declaration_modifier]
             [array_creation_expression member_declaration_modifier]
             [array_creation_expression object_creation_expression member_declaration_modifier]
             [member_declaration_modifier
              delegate_declaration_modifier
              constructor_declaration_modifier]
             [member_declaration_modifier
              signal_declaration_modifier
              constructor_declaration_modifier]
             [member_declaration_modifier constructor_declaration_modifier]
             [member_declaration_modifier
              type_declaration_modifier
              constructor_declaration_modifier]
             [member_declaration_modifier
              type_declaration_modifier
              signal_declaration_modifier
              constructor_declaration_modifier]
             [symbol member_access_expression]
             [symbol member_access_expression lambda_expression]
             [type]
             [array_type]
             [symbol type]
             [_expression method_call_expression]
             [_contained_expression method_call_expression]
             [_expression element_access_expression]
             [element_access_expression]
             [initializer block]
             [if_statement]
             [_expression _left_contained_expression]]
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source_file (:seq (:repeat using_directive) (:repeat (:choice namespace_member _statement)))
  comment (:token
           (:prec 1
            (:choice
             (:seq "//" (:pattern "(\\\\(.|\\r?\\n)|[^\\\\\\n])*"))
             (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/"))))
  using_directive (:seq "using" symbol (:repeat (:seq "," symbol)) ";")
  symbol (:choice (:seq symbol "." identifier) (:seq (:choice "global::" :blank) identifier))
  identifier (:pattern "[@A-Za-z_]\\w*")
  namespace_member (:seq
                    (:repeat _attribute_list)
                    (:choice
                     namespace_declaration
                     class_declaration
                     interface_declaration
                     struct_declaration
                     enum_declaration
                     errordomain_declaration
                     delegate_declaration
                     method_declaration
                     signal_declaration
                     field_declaration
                     constant_declaration))
  _attribute_list (:seq "[" attribute (:repeat (:seq "," attribute)) "]")
  attribute (:seq
             identifier
             (:choice
              (:seq "(" attribute_argument (:repeat (:seq "," attribute_argument)) ")")
              :blank))
  attribute_argument (:seq identifier "=" _expression)
  _expression (:choice
               _contained_expression
               initializer
               yield_expression
               lambda_expression
               static_cast_expression
               _unary_expression
               multiplicative_expression
               arithmetic_expression
               in_expression
               bitshift_expression
               dynamic_cast_expression
               type_relational_expression
               relational_expression
               equality_expression
               bitwise_and_expression
               bitwise_xor_expression
               bitwise_or_expression
               logical_and_expression
               logical_or_expression
               null_coalescing_expression
               ternary_expression
               assignment_expression)
  _contained_expression (:choice
                         (:seq "(" _expression ")")
                         literal
                         array_creation_expression
                         object_creation_expression
                         this_access
                         base_access
                         value_access
                         member_access_expression
                         element_access_expression
                         method_call_expression
                         sizeof_expression
                         typeof_expression
                         postfix_expression)
  _left_contained_expression (:choice
                              _contained_expression
                              yield_expression
                              static_cast_expression
                              _unary_expression)
  member_access_expression (:seq
                            (:choice (:seq _contained_expression (:choice "." "?." "->")) :blank)
                            identifier)
  element_access_expression (:seq _contained_expression (:repeat1 element_access))
  element_access (:seq
                  "["
                  (:choice _expression slice_expression)
                  (:repeat (:seq "," (:choice _expression slice_expression)))
                  "]")
  slice_expression (:choice
                    (:seq _expression ":" (:choice _expression :blank))
                    (:seq ":" _expression))
  argument (:choice
            (:seq "ref" _expression)
            (:seq "out" _expression)
            _expression
            (:seq identifier ":" _expression))
  method_call_expression (:seq
                          (:choice
                           (:seq "(" _expression ")")
                           (:seq member_access_expression (:choice type_arguments :blank))
                           _contained_expression)
                          "("
                          (:choice (:seq argument (:repeat (:seq "," argument))) :blank)
                          ")")
  yield_expression (:seq "yield" _expression)
  lambda_expression (:seq
                     (:choice
                      identifier
                      (:seq
                       "("
                       (:choice (:seq identifier (:repeat (:seq "," identifier))) :blank)
                       ")"))
                     "=>"
                     (:choice _expression block))
  postfix_expression (:prec-left 15 (:seq _expression (:choice "++" "--")))
  static_cast_expression (:prec-right 14 (:seq "(" (:choice type "!" "owned") ")" _expression))
  typeof_expression (:prec-right 14 (:seq "typeof" "(" type ")"))
  sizeof_expression (:prec-right 14 (:seq "sizeof" "(" type ")"))
  dereferencing_expression (:prec-right 14 (:seq "*" _left_contained_expression))
  addressof_expression (:prec-right 14 (:seq "&" _left_contained_expression))
  arithmetic_negation_expression (:prec-right 14 (:seq "-" _left_contained_expression))
  prefix_expression (:prec-right 14 (:seq (:choice "++" "--") _left_contained_expression))
  bitwise_negation_expression (:prec-right 14 (:seq "~" _left_contained_expression))
  logical_negation_expression (:prec-right 14 (:seq (:choice "!" "not") _left_contained_expression))
  _unary_expression (:prec-right 14
                     (:choice
                      dereferencing_expression
                      addressof_expression
                      arithmetic_negation_expression
                      prefix_expression
                      bitwise_negation_expression
                      logical_negation_expression))
  multiplicative_expression (:prec-left 13 (:seq _expression (:choice "*" "/" "%") _expression))
  arithmetic_expression (:prec-left 12 (:seq _expression (:choice "+" "-") _expression))
  bitshift_expression (:prec-left 11 (:seq _expression (:choice "<<" ">>") _expression))
  in_expression (:prec-left 10 (:seq _expression (:choice "not" :blank) "in" _expression))
  dynamic_cast_expression (:prec-left 10 (:seq _expression "as" type))
  type_relational_expression (:prec-left 10 (:seq _expression "is" (:choice "not" :blank) type))
  relational_expression (:prec-left 10 (:seq _expression (:choice "<" "<=" ">=" ">") _expression))
  equality_expression (:prec-left 9 (:seq _expression (:choice "==" "!=") _expression))
  bitwise_and_expression (:prec-left 8 (:seq _expression "&" _expression))
  bitwise_xor_expression (:prec-left 7 (:seq _expression "^" _expression))
  bitwise_or_expression (:prec-left 6 (:seq _expression "|" _expression))
  logical_and_expression (:prec-left 5 (:seq _expression (:choice "&&" "and") _expression))
  logical_or_expression (:prec-left 4 (:seq _expression (:choice "||" "or") _expression))
  null_coalescing_expression (:prec-left 3 (:seq _expression "??" _expression))
  ternary_expression (:prec-right 2 (:seq _expression "?" _expression ":" _expression))
  _assignment_operator (:choice "=" "+=" "-=" "|=" "&=" "^=" "/=" "*=" "%=" "<<=" ">>=")
  assignment_expression (:prec-right 1 (:seq _expression _assignment_operator _expression))
  this_access "this"
  base_access "base"
  value_access "value"
  array_creation_expression (:seq "new" type inline_array_type (:choice initializer :blank))
  object_creation_expression (:seq
                              "new"
                              type
                              (:choice (:seq "." identifier) :blank)
                              "("
                              (:choice (:seq argument (:repeat (:seq "," argument))) :blank)
                              ")"
                              (:choice object_initializers :blank))
  object_initializers (:seq
                       "{"
                       (:choice
                        (:seq
                         member_initializer
                         (:repeat (:seq "," member_initializer))
                         (:choice "," :blank))
                        :blank)
                       "}")
  member_initializer (:seq identifier "=" _expression)
  initializer (:seq
               "{"
               (:choice (:seq argument (:repeat (:seq "," argument)) (:choice "," :blank)) :blank)
               "}")
  boolean (:choice "true" "false")
  character (:pattern "'(\\S+|\\s)'")
  integer (:choice (:pattern "([1-9]\\d*|0[0-7]*)[UuLl]?") (:pattern "0[xX][A-Fa-f0-9]+"))
  null "null"
  real (:pattern "\\d+(\\.\\d+)?([eE][+-]?\\d+)?[Ff]?")
  regex (:pattern "\\/([^\\\\\\/\\n]|\\\\[\\\\\\/A-z0|\\[\\]^$?.(){}+\\-*])+\\/[gmxsu]*")
  string (:seq
          "\""
          (:repeat
           (:choice
            (:token (:prec 2 (:pattern "[^\"%\\\\]+")))
            escape_sequence
            (:pattern "\\\\[^abefnrtv\\\\'\"? xXuU]")
            string_formatter
            (:pattern "%[^$#0\\- +'I\\d\\\\.hlqLjzZtdiouxXeEfFgGaAcsCSpnm%\"]")))
          (:token (:prec 2 "\"")))
  escape_sequence (:pattern "\\\\([abefnrtv\\\\'\"? ]|[0-7]{3}|[xX][A-Fa-f0-9]{2}|[uU][A-Fa-f0-9]{4,8})")
  string_formatter (:pattern "%\\$?[#0\\- +'I]?\\d*(\\.\\d+)?(hh?|ll?|q|L|j|z|Z|t)?[diouxXeEfFgGaAcsCSpnm%]?")
  template_string (:seq
                   "@\""
                   (:repeat
                    (:choice
                     (:token (:prec 2 (:pattern "([^$\"]+|\\\\\")+")))
                     template_string_expression
                     "$$"))
                   (:token (:prec 2 "\"")))
  template_string_expression (:choice (:seq "$(" _expression ")") (:seq "$" identifier))
  verbatim_string (:pattern "\"\"\"(.|\\n)*\"\"\"")
  literal (:choice boolean null character integer real regex string template_string verbatim_string)
  type (:choice
        "var"
        (:seq "void" (:repeat "*"))
        (:seq
         (:choice "dynamic" :blank)
         (:choice (:choice "unowned" "owned" "weak") :blank)
         "("
         type
         ")"
         (:repeat1 array_type))
        (:seq
         (:choice "dynamic" :blank)
         (:choice (:choice "unowned" "owned" "weak") :blank)
         symbol
         (:choice type_arguments :blank)
         (:repeat "*")
         (:choice "?" :blank)
         (:repeat array_type)))
  unqualified_type (:seq symbol (:choice type_arguments :blank))
  type_arguments (:seq "<" type (:repeat (:seq "," type)) ">")
  array_type (:seq "[" (:choice array_size :blank) "]" (:choice "?" :blank))
  array_size (:seq _expression (:repeat (:seq "," _expression)))
  member_declaration_modifier (:choice
                               "async"
                               "class"
                               "extern"
                               "inline"
                               "static"
                               "abstract"
                               "virtual"
                               "override"
                               "new")
  access_modifier (:choice "private" "protected" "internal" "public")
  namespace_declaration (:seq
                         "namespace"
                         symbol
                         "{"
                         (:repeat using_directive)
                         (:repeat namespace_member)
                         "}")
  type_declaration_modifier (:choice "partial" "abstract" "extern" "static")
  class_declaration (:seq
                     (:choice access_modifier :blank)
                     (:repeat type_declaration_modifier)
                     "class"
                     unqualified_type
                     (:choice (:seq ":" type (:repeat (:seq "," type))) :blank)
                     "{"
                     (:repeat class_member)
                     "}")
  class_member (:seq
                (:repeat _attribute_list)
                (:choice
                 class_declaration
                 interface_declaration
                 struct_declaration
                 enum_declaration
                 delegate_declaration
                 method_declaration
                 creation_method_declaration
                 signal_declaration
                 field_declaration
                 constant_declaration
                 property_declaration
                 constructor_declaration
                 destructor_declaration))
  interface_declaration (:seq
                         (:choice access_modifier :blank)
                         (:repeat type_declaration_modifier)
                         "interface"
                         unqualified_type
                         (:choice (:seq ":" type (:repeat (:seq "," type))) :blank)
                         "{"
                         (:repeat interface_member)
                         "}")
  interface_member (:seq
                    (:repeat _attribute_list)
                    (:choice
                     class_declaration
                     interface_declaration
                     struct_declaration
                     enum_declaration
                     delegate_declaration
                     method_declaration
                     signal_declaration
                     field_declaration
                     constant_declaration
                     property_declaration))
  struct_declaration (:seq
                      (:choice access_modifier :blank)
                      (:repeat type_declaration_modifier)
                      "struct"
                      unqualified_type
                      (:choice (:seq ":" type (:repeat (:seq "," type))) :blank)
                      "{"
                      (:repeat struct_member)
                      "}")
  struct_member (:seq
                 (:repeat _attribute_list)
                 (:choice
                  method_declaration
                  creation_method_declaration
                  field_declaration
                  constant_declaration
                  property_declaration))
  enum_declaration (:seq
                    (:choice access_modifier :blank)
                    (:repeat type_declaration_modifier)
                    "enum"
                    symbol
                    "{"
                    enum_value
                    (:repeat (:seq "," enum_value))
                    (:choice
                     (:choice
                      ","
                      (:seq
                       ";"
                       (:repeat
                        (:seq
                         (:repeat _attribute_list)
                         (:choice method_declaration constant_declaration)))))
                     :blank)
                    "}")
  enum_value (:seq (:repeat _attribute_list) identifier (:choice (:seq "=" _expression) :blank))
  errordomain_declaration (:seq
                           (:choice access_modifier :blank)
                           (:repeat type_declaration_modifier)
                           "errordomain"
                           symbol
                           "{"
                           errorcode
                           (:repeat (:seq "," errorcode))
                           (:choice
                            (:choice
                             ","
                             (:seq
                              ";"
                              (:repeat (:seq (:repeat _attribute_list) method_declaration))))
                            :blank)
                           "}")
  errorcode (:seq (:repeat _attribute_list) identifier (:choice (:seq "=" _expression) :blank))
  parameter (:seq
             (:repeat _attribute_list)
             (:choice
              (:seq
               (:choice "params" :blank)
               (:choice (:choice "out" "ref") :blank)
               type
               identifier
               (:choice inline_array_type :blank)
               (:choice (:seq "=" _expression) :blank))
              "..."))
  creation_method_declaration (:seq
                               (:choice access_modifier :blank)
                               (:repeat member_declaration_modifier)
                               symbol
                               "("
                               (:choice (:seq parameter (:repeat (:seq "," parameter))) :blank)
                               ")"
                               (:choice (:seq "throws" type) :blank)
                               (:choice
                                (:seq (:choice "requires" "ensures") "(" _expression ")")
                                :blank)
                               (:choice block ";"))
  delegate_declaration_modifier (:seq
                                 "async"
                                 "class"
                                 "extern"
                                 "inline"
                                 "abstract"
                                 "virtual"
                                 "override")
  delegate_declaration (:seq
                        (:choice access_modifier :blank)
                        (:repeat delegate_declaration_modifier)
                        "delegate"
                        type
                        symbol
                        (:choice type_arguments :blank)
                        "("
                        (:choice (:seq parameter (:repeat (:seq "," parameter))) :blank)
                        ")"
                        (:choice (:seq "throws" type) :blank)
                        ";")
  method_declaration (:seq
                      (:choice access_modifier :blank)
                      (:repeat member_declaration_modifier)
                      type
                      symbol
                      (:choice type_arguments :blank)
                      "("
                      (:choice (:seq parameter (:repeat (:seq "," parameter))) :blank)
                      ")"
                      (:choice (:seq "throws" type) :blank)
                      (:choice (:seq (:choice "requires" "ensures") "(" _expression ")") :blank)
                      (:choice block ";"))
  signal_declaration_modifier (:choice
                               "async"
                               "extern"
                               "inline"
                               "abstract"
                               "virtual"
                               "override"
                               "new")
  signal_declaration (:seq
                      (:choice access_modifier :blank)
                      (:repeat signal_declaration_modifier)
                      "signal"
                      type
                      symbol
                      "("
                      (:choice (:seq parameter (:repeat (:seq "," parameter))) :blank)
                      ")"
                      (:choice block ";"))
  field_declaration (:seq
                     (:choice access_modifier :blank)
                     (:repeat member_declaration_modifier)
                     type
                     identifier
                     (:choice inline_array_type :blank)
                     (:choice (:seq "=" _expression) :blank)
                     ";")
  constant_declaration (:seq
                        (:choice access_modifier :blank)
                        (:repeat member_declaration_modifier)
                        "const"
                        type
                        identifier
                        (:choice inline_array_type :blank)
                        (:choice (:seq "=" _expression) :blank)
                        ";")
  inline_array_type (:seq "[" (:choice _expression :blank) "]")
  property_declaration (:seq
                        (:choice access_modifier :blank)
                        (:repeat member_declaration_modifier)
                        type
                        symbol
                        "{"
                        (:choice property_default property_accessor)
                        (:repeat (:choice property_default property_accessor))
                        "}")
  property_default (:seq "default" "=" _expression ";")
  property_accessor (:seq
                     (:repeat _attribute_list)
                     (:choice access_modifier :blank)
                     (:choice
                      (:seq (:choice "owned" :blank) "get")
                      (:seq (:choice "owned" :blank) "set" (:choice "construct" :blank))
                      (:seq "construct" (:choice "set" :blank)))
                     (:choice ";" block))
  constructor_declaration_modifier (:choice
                                    "async"
                                    "class"
                                    "extern"
                                    "inline"
                                    "static"
                                    "abstract"
                                    "virtual"
                                    "override")
  constructor_declaration (:seq (:repeat constructor_declaration_modifier) "construct" block)
  destructor_declaration (:seq
                          (:repeat constructor_declaration_modifier)
                          "~"
                          identifier
                          "("
                          ")"
                          block)
  local_declaration (:seq type assignment (:repeat (:seq "," assignment)) ";")
  local_function_declaration (:seq
                              type
                              identifier
                              "("
                              (:choice (:seq parameter (:repeat (:seq "," parameter))) :blank)
                              ")"
                              block)
  assignment (:prec-dynamic 20
              (:seq
               identifier
               (:choice inline_array_type :blank)
               (:choice (:seq "=" _expression) :blank)))
  block (:seq "{" (:repeat (:choice _statement local_declaration local_function_declaration)) "}")
  _statement (:choice if_statement _statement_without_if)
  _statement_without_if (:choice
                         block
                         empty_statement
                         expression_statement
                         return_statement
                         try_statement
                         while_statement
                         do_statement
                         for_statement
                         foreach_statement
                         break_statement
                         continue_statement
                         lock_statement
                         delete_statement
                         throw_statement
                         yield_statement
                         switch_statement
                         with_statement)
  empty_statement ";"
  expression_statement (:seq _expression ";")
  return_statement (:seq "return" (:choice _expression :blank) ";")
  if_statement (:seq
                "if"
                "("
                _expression
                ")"
                _statement
                (:repeat elseif_statement)
                (:choice else_statement :blank))
  elseif_statement (:seq "else" "if" "(" _expression ")" _statement)
  else_statement (:seq "else" _statement_without_if)
  try_statement (:seq "try" block (:repeat catch_clause) (:choice finally_clause :blank))
  catch_clause (:seq "catch" (:choice (:seq "(" type identifier ")") :blank) block)
  finally_clause (:seq "finally" block)
  while_statement (:seq "while" "(" _expression ")" _statement)
  do_statement (:seq "do" _statement "while" "(" _expression ")" ";")
  for_statement (:seq
                 "for"
                 "("
                 (:choice (:choice local_declaration (:seq _expression ";") ";") :blank)
                 (:choice _expression :blank)
                 ";"
                 (:choice (:seq _expression (:repeat (:seq "," _expression))) :blank)
                 ")"
                 _statement)
  foreach_statement (:seq "foreach" "(" type identifier "in" _expression ")" _statement)
  break_statement (:seq "break" ";")
  continue_statement (:seq "continue" ";")
  lock_statement (:seq "lock" "(" _expression ")" _statement)
  delete_statement (:seq "delete" _expression ";")
  throw_statement (:seq "throw" _expression ";")
  yield_statement (:seq "yield" (:choice (:seq "return" _expression) :blank) ";")
  switch_statement (:seq "switch" "(" _expression ")" "{" (:repeat switch_section) "}")
  switch_section (:seq
                  (:choice (:seq "case" _expression) "default")
                  ":"
                  (:repeat (:choice _statement local_declaration)))
  with_statement (:seq "with" "(" _expression ")" _statement)
  _preprocessor_directive_start (:pattern "#[\\t ]*")
  _preprocessor_statement (:seq
                           _preprocessor_directive_start
                           (:choice if_directive elif_directive else_directive endif_directive))
  if_directive (:seq "if" (:field :expression _preprocessor_expression))
  elif_directive (:seq "elif" (:field :expression _preprocessor_expression))
  else_directive "else"
  endif_directive "endif"
  _preprocessor_expression (:choice
                            (:seq "(" _preprocessor_contained_expression ")")
                            identifier
                            literal)
  _preprocessor_contained_expression (:choice
                                      (:seq "(" _preprocessor_contained_expression ")")
                                      identifier
                                      literal
                                      (:prec-right 4 (:seq "!" _preprocessor_expression))
                                      (:prec-left 3
                                       (:seq
                                        _preprocessor_expression
                                        (:choice "==" "!=")
                                        _preprocessor_expression))
                                      (:prec-left 2
                                       (:seq _preprocessor_expression "&&" _preprocessor_expression))
                                      (:prec-left 1
                                       (:seq _preprocessor_expression "||" _preprocessor_expression)))}}
