# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "php"
 :word name
 :extras [comment (:pattern "[\\s\\u00A0\\u200B\\u2060\\uFEFF]") text_interpolation]
 :conflicts [[_array_destructing array_creation_expression]
             [_array_destructing_element array_element_initializer]
             [primary_expression _array_destructing_element]
             [type union_type intersection_type disjunctive_normal_form_type]
             [union_type disjunctive_normal_form_type]
             [intersection_type]
             [if_statement]
             [namespace_name]
             [heredoc_body]]
 :precedences []
 :externals [_automatic_semicolon
             encapsed_string_chars
             encapsed_string_chars_after_variable
             execution_string_chars
             execution_string_chars_after_variable
             encapsed_string_chars_heredoc
             encapsed_string_chars_after_variable_heredoc
             _eof
             heredoc_start
             heredoc_end
             nowdoc_string
             sentinel_error]
 :inline [_variable _namespace_use_type]
 :supertypes [statement expression primary_expression type literal]
 :reserved
 {:global [(:pattern "abstract" "i")
            (:pattern "and" "i")
            (:pattern "as" "i")
            (:pattern "break" "i")
            (:pattern "callable" "i")
            (:pattern "case" "i")
            (:pattern "catch" "i")
            (:pattern "class" "i")
            (:pattern "clone" "i")
            (:pattern "const" "i")
            (:pattern "continue" "i")
            (:pattern "declare" "i")
            (:pattern "default" "i")
            (:pattern "do" "i")
            (:pattern "echo" "i")
            (:pattern "else" "i")
            (:pattern "elseif" "i")
            (:pattern "enddeclare" "i")
            (:pattern "endfor" "i")
            (:pattern "endforeach" "i")
            (:pattern "endif" "i")
            (:pattern "endswitch" "i")
            (:pattern "endwhile" "i")
            (:pattern "extends" "i")
            (:pattern "final" "i")
            (:pattern "finally" "i")
            (:pattern "fn" "i")
            (:pattern "for" "i")
            (:pattern "foreach" "i")
            (:pattern "function" "i")
            (:pattern "global" "i")
            (:pattern "goto" "i")
            (:pattern "if" "i")
            (:pattern "implements" "i")
            (:pattern "include" "i")
            (:pattern "include_once" "i")
            (:pattern "instanceof" "i")
            (:pattern "insteadof" "i")
            (:pattern "interface" "i")
            (:pattern "match" "i")
            (:pattern "namespace" "i")
            (:pattern "new" "i")
            (:pattern "or" "i")
            (:pattern "print" "i")
            (:pattern "private" "i")
            (:pattern "protected" "i")
            (:pattern "public" "i")
            (:pattern "readonly" "i")
            (:pattern "require" "i")
            (:pattern "require_once" "i")
            (:pattern "return" "i")
            (:pattern "static" "i")
            (:pattern "switch" "i")
            (:pattern "throw" "i")
            (:pattern "trait" "i")
            (:pattern "try" "i")
            (:pattern "use" "i")
            (:pattern "var" "i")
            (:pattern "while" "i")
            (:pattern "xor" "i")
            (:pattern "yield from" "i")
            (:pattern "yield" "i")]
  :classes [(:pattern "abstract" "i")
             (:pattern "and" "i")
             (:pattern "as" "i")
             (:pattern "break" "i")
             (:pattern "callable" "i")
             (:pattern "case" "i")
             (:pattern "catch" "i")
             (:pattern "class" "i")
             (:pattern "clone" "i")
             (:pattern "const" "i")
             (:pattern "continue" "i")
             (:pattern "declare" "i")
             (:pattern "default" "i")
             (:pattern "do" "i")
             (:pattern "echo" "i")
             (:pattern "else" "i")
             (:pattern "elseif" "i")
             (:pattern "enddeclare" "i")
             (:pattern "endfor" "i")
             (:pattern "endforeach" "i")
             (:pattern "endif" "i")
             (:pattern "endswitch" "i")
             (:pattern "endwhile" "i")
             (:pattern "extends" "i")
             (:pattern "final" "i")
             (:pattern "finally" "i")
             (:pattern "fn" "i")
             (:pattern "for" "i")
             (:pattern "foreach" "i")
             (:pattern "function" "i")
             (:pattern "global" "i")
             (:pattern "goto" "i")
             (:pattern "if" "i")
             (:pattern "implements" "i")
             (:pattern "include" "i")
             (:pattern "include_once" "i")
             (:pattern "instanceof" "i")
             (:pattern "insteadof" "i")
             (:pattern "interface" "i")
             (:pattern "match" "i")
             (:pattern "namespace" "i")
             (:pattern "new" "i")
             (:pattern "or" "i")
             (:pattern "print" "i")
             (:pattern "private" "i")
             (:pattern "protected" "i")
             (:pattern "public" "i")
             (:pattern "readonly" "i")
             (:pattern "require" "i")
             (:pattern "require_once" "i")
             (:pattern "return" "i")
             (:pattern "static" "i")
             (:pattern "switch" "i")
             (:pattern "throw" "i")
             (:pattern "trait" "i")
             (:pattern "try" "i")
             (:pattern "use" "i")
             (:pattern "var" "i")
             (:pattern "while" "i")
             (:pattern "xor" "i")
             (:pattern "yield from" "i")
             (:pattern "yield" "i")
             (:pattern "bool" "i")
             (:pattern "false" "i")
             (:pattern "float" "i")
             (:pattern "int" "i")
             (:pattern "iterable" "i")
             (:pattern "mixed" "i")
             (:pattern "never" "i")
             (:pattern "null" "i")
             (:pattern "object" "i")
             (:pattern "string" "i")
             (:pattern "true" "i")
             (:pattern "void" "i")]
  :nothing []}
 :rules
 {program (:seq (:choice text :blank) (:choice (:seq php_tag (:repeat statement)) :blank))
  php_tag (:pattern "<\\?([pP][hH][pP]|=)?")
  php_end_tag "?>"
  text_interpolation (:seq php_end_tag (:choice text :blank) (:choice php_tag _eof))
  text (:repeat1
        (:choice (:token (:prec -1 (:pattern "<"))) (:token (:prec 1 (:pattern "[^\\s<][^<]*")))))
  statement (:choice
             empty_statement
             compound_statement
             named_label_statement
             expression_statement
             if_statement
             switch_statement
             while_statement
             do_statement
             for_statement
             foreach_statement
             goto_statement
             continue_statement
             break_statement
             return_statement
             try_statement
             declare_statement
             echo_statement
             exit_statement
             unset_statement
             const_declaration
             function_definition
             class_declaration
             interface_declaration
             trait_declaration
             enum_declaration
             namespace_definition
             namespace_use_declaration
             global_declaration
             function_static_declaration)
  empty_statement (:prec -1 ";")
  reference_modifier "&"
  function_static_declaration (:seq
                               (:alias (:pattern "static" "i") "static")
                               (:seq
                                static_variable_declaration
                                (:repeat (:seq "," static_variable_declaration)))
                               _semicolon)
  static_variable_declaration (:seq
                               (:field :name variable_name)
                               (:choice (:seq "=" (:field :value expression)) :blank))
  global_declaration (:seq
                      (:alias (:pattern "global" "i") "global")
                      (:seq _simple_variable (:repeat (:seq "," _simple_variable)))
                      _semicolon)
  namespace_definition (:seq
                        (:alias (:pattern "namespace" "i") "namespace")
                        (:choice
                         (:seq (:field :name namespace_name) _semicolon)
                         (:seq
                          (:field :name (:choice namespace_name :blank))
                          (:field :body compound_statement))))
  namespace_use_declaration (:seq
                             (:alias (:pattern "use" "i") "use")
                             (:choice
                              (:seq namespace_use_clause (:repeat (:seq "," namespace_use_clause)))
                              _namespace_use_group)
                             _semicolon)
  namespace_use_clause (:seq
                        (:field :type (:choice _namespace_use_type :blank))
                        (:choice name qualified_name)
                        (:choice
                         (:seq (:alias (:pattern "as" "i") "as") (:field :alias name))
                         :blank))
  _namespace_use_type (:choice
                       (:alias (:pattern "function" "i") "function")
                       (:alias (:pattern "const" "i") "const"))
  qualified_name (:seq
                  (:field :prefix (:seq (:choice "\\" :blank) (:choice namespace_name :blank) "\\"))
                  (:reserved :classes name))
  relative_name (:seq
                 (:field :prefix
                  (:seq
                   (:alias (:pattern "namespace" "i") "namespace")
                   (:choice (:seq "\\" namespace_name) :blank)
                   "\\"))
                 (:reserved :classes name))
  _name (:choice
         (:alias (:pattern "static" "i") name)
         (:reserved :classes name)
         qualified_name
         relative_name)
  namespace_name (:seq (:reserved :nothing name) (:repeat (:seq "\\" (:reserved :nothing name))))
  _namespace_use_group (:seq
                        (:field :type (:choice _namespace_use_type :blank))
                        namespace_name
                        "\\"
                        (:field :body namespace_use_group))
  namespace_use_group (:seq
                       "{"
                       (:seq namespace_use_clause (:repeat (:seq "," namespace_use_clause)))
                       "}")
  trait_declaration (:seq
                     (:choice (:field :attributes attribute_list) :blank)
                     (:alias (:pattern "trait" "i") "trait")
                     (:field :name (:reserved :classes name))
                     (:field :body declaration_list))
  interface_declaration (:seq
                         (:choice (:field :attributes attribute_list) :blank)
                         (:alias (:pattern "interface" "i") "interface")
                         (:field :name (:reserved :classes name))
                         (:choice base_clause :blank)
                         (:field :body declaration_list))
  base_clause (:seq
               (:alias (:pattern "extends" "i") "extends")
               (:seq _name (:repeat (:seq "," _name))))
  enum_declaration (:prec-right 0
                    (:seq
                     (:choice (:field :attributes attribute_list) :blank)
                     (:alias (:pattern "enum" "i") "enum")
                     (:field :name (:reserved :classes name))
                     (:choice (:seq ":" (:alias (:choice "string" "int") primitive_type)) :blank)
                     (:choice class_interface_clause :blank)
                     (:field :body enum_declaration_list)))
  enum_declaration_list (:seq "{" (:repeat _enum_member_declaration) "}")
  _enum_member_declaration (:choice
                            (:alias _class_const_declaration const_declaration)
                            enum_case
                            method_declaration
                            use_declaration)
  enum_case (:seq
             (:choice (:field :attributes attribute_list) :blank)
             (:alias (:pattern "case" "i") "case")
             (:field :name (:reserved :nothing name))
             (:choice (:seq "=" (:field :value expression)) :blank)
             _semicolon)
  class_declaration (:prec-right 0
                     (:seq
                      (:choice (:field :attributes attribute_list) :blank)
                      (:repeat _modifier)
                      (:alias (:pattern "class" "i") "class")
                      (:field :name (:reserved :classes name))
                      (:choice base_clause :blank)
                      (:choice class_interface_clause :blank)
                      (:field :body declaration_list)))
  declaration_list (:seq "{" (:repeat _member_declaration) "}")
  final_modifier (:alias (:pattern "final" "i") "final")
  abstract_modifier (:alias (:pattern "abstract" "i") "abstract")
  readonly_modifier (:alias (:pattern "readonly" "i") "readonly")
  class_interface_clause (:seq
                          (:alias (:pattern "implements" "i") "implements")
                          (:seq _name (:repeat (:seq "," _name))))
  _member_declaration (:choice
                       (:alias _class_const_declaration const_declaration)
                       property_declaration
                       method_declaration
                       use_declaration)
  const_declaration (:seq
                     (:choice (:field :attributes attribute_list) :blank)
                     (:repeat _modifier)
                     (:alias (:pattern "const" "i") "const")
                     (:choice (:field :type type) :blank)
                     (:seq
                      (:alias _const_element const_element)
                      (:repeat (:seq "," (:alias _const_element const_element))))
                     _semicolon)
  _class_const_declaration (:seq
                            (:choice (:field :attributes attribute_list) :blank)
                            (:choice final_modifier :blank)
                            (:repeat _modifier)
                            (:alias (:pattern "const" "i") "const")
                            (:choice (:field :type type) :blank)
                            (:seq
                             (:alias _class_const_element const_element)
                             (:repeat (:seq "," (:alias _class_const_element const_element))))
                            _semicolon)
  property_declaration (:seq
                        (:choice (:field :attributes attribute_list) :blank)
                        (:repeat1 _modifier)
                        (:choice (:field :type type) :blank)
                        (:seq property_element (:repeat (:seq "," property_element)))
                        (:choice _semicolon property_hook_list))
  _modifier (:prec-left 0
             (:choice
              var_modifier
              visibility_modifier
              static_modifier
              final_modifier
              abstract_modifier
              readonly_modifier))
  property_element (:seq
                    (:field :name variable_name)
                    (:choice (:seq "=" (:field :default_value expression)) :blank))
  property_hook_list (:seq "{" (:repeat property_hook) "}")
  property_hook (:seq
                 (:choice (:field :attributes attribute_list) :blank)
                 (:choice (:field :final final_modifier) :blank)
                 (:choice (:field :reference_modifier reference_modifier) :blank)
                 name
                 (:choice (:field :parameters formal_parameters) :blank)
                 _property_hook_body)
  _property_hook_body (:choice
                       (:seq "=>" (:field :body expression) _semicolon)
                       (:field :body compound_statement)
                       _semicolon)
  method_declaration (:seq
                      (:choice (:field :attributes attribute_list) :blank)
                      (:repeat _modifier)
                      (:alias (:pattern "function" "i") "function")
                      (:choice reference_modifier :blank)
                      (:field :name (:reserved :nothing name))
                      (:field :parameters formal_parameters)
                      (:choice _return_type :blank)
                      (:choice (:field :body compound_statement) _semicolon))
  var_modifier (:pattern "var" "i")
  static_modifier (:alias (:pattern "static" "i") "static")
  use_declaration (:seq
                   (:alias (:pattern "use" "i") "use")
                   (:seq _name (:repeat (:seq "," _name)))
                   (:choice use_list _semicolon))
  use_list (:seq "{" (:repeat (:seq (:choice use_instead_of_clause use_as_clause) _semicolon)) "}")
  use_instead_of_clause (:prec-left 0
                         (:seq
                          class_constant_access_expression
                          (:alias (:pattern "insteadof" "i") "insteadof")
                          name))
  use_as_clause (:seq
                 (:choice class_constant_access_expression name)
                 (:alias (:pattern "as" "i") "as")
                 (:choice
                  (:seq (:choice visibility_modifier :blank) name)
                  (:seq visibility_modifier (:choice name :blank))))
  visibility_modifier (:seq
                       (:choice
                        (:alias (:pattern "public" "i") "public")
                        (:alias (:pattern "protected" "i") "protected")
                        (:alias (:pattern "private" "i") "private"))
                       (:choice
                        (:seq (:token-immediate "(") (:alias name operation) (:token-immediate ")"))
                        :blank))
  function_definition (:seq
                       (:choice (:field :attributes attribute_list) :blank)
                       (:alias (:pattern "function" "i") "function")
                       (:choice reference_modifier :blank)
                       (:field :name name)
                       (:field :parameters formal_parameters)
                       (:choice _return_type :blank)
                       (:field :body compound_statement))
  anonymous_function (:seq _anonymous_function_header (:field :body compound_statement))
  anonymous_function_use_clause (:seq
                                 (:alias (:pattern "use" "i") "use")
                                 "("
                                 (:seq
                                  (:choice by_ref variable_name)
                                  (:repeat (:seq "," (:choice by_ref variable_name))))
                                 (:choice "," :blank)
                                 ")")
  _anonymous_function_header (:seq
                              (:choice (:field :attributes attribute_list) :blank)
                              (:choice (:field :static_modifier static_modifier) :blank)
                              (:alias (:pattern "function" "i") "function")
                              (:choice (:field :reference_modifier reference_modifier) :blank)
                              (:field :parameters formal_parameters)
                              (:choice anonymous_function_use_clause :blank)
                              (:choice _return_type :blank))
  _arrow_function_header (:seq
                          (:choice (:field :attributes attribute_list) :blank)
                          (:choice (:field :static_modifier static_modifier) :blank)
                          (:alias (:pattern "fn" "i") "fn")
                          (:choice (:field :reference_modifier reference_modifier) :blank)
                          (:field :parameters formal_parameters)
                          (:choice _return_type :blank))
  arrow_function (:seq _arrow_function_header "=>" (:field :body expression))
  formal_parameters (:seq
                     "("
                     (:choice
                      (:seq
                       (:choice simple_parameter variadic_parameter property_promotion_parameter)
                       (:repeat
                        (:seq
                         ","
                         (:choice simple_parameter variadic_parameter property_promotion_parameter))))
                      :blank)
                     (:choice "," :blank)
                     ")")
  property_promotion_parameter (:seq
                                (:choice (:field :attributes attribute_list) :blank)
                                (:field :visibility visibility_modifier)
                                (:field :readonly (:choice readonly_modifier :blank))
                                (:field :type (:choice type :blank))
                                (:field :name (:choice by_ref variable_name))
                                (:choice (:seq "=" (:field :default_value expression)) :blank)
                                (:choice property_hook_list :blank))
  simple_parameter (:seq
                    (:choice (:field :attributes attribute_list) :blank)
                    (:field :type (:choice type :blank))
                    (:choice (:field :reference_modifier reference_modifier) :blank)
                    (:field :name variable_name)
                    (:choice (:seq "=" (:field :default_value expression)) :blank))
  variadic_parameter (:seq
                      (:choice (:field :attributes attribute_list) :blank)
                      (:field :type (:choice type :blank))
                      (:choice (:field :reference_modifier reference_modifier) :blank)
                      "..."
                      (:field :name variable_name))
  type (:choice _types union_type intersection_type disjunctive_normal_form_type)
  _types (:choice optional_type named_type primitive_type)
  named_type (:choice (:reserved :classes name) qualified_name relative_name)
  optional_type (:seq "?" (:choice named_type primitive_type))
  bottom_type (:pattern "never" "i")
  union_type (:seq _types (:repeat (:seq "|" _types)))
  intersection_type (:seq _types (:repeat (:seq (:token "&") _types)))
  disjunctive_normal_form_type (:prec-dynamic -1
                                (:seq
                                 (:choice (:seq "(" intersection_type ")") _types)
                                 (:repeat
                                  (:seq "|" (:choice (:seq "(" intersection_type ")") _types)))))
  primitive_type (:choice
                  "array"
                  "bool"
                  (:pattern "callable" "i")
                  (:pattern "false" "i")
                  "float"
                  "int"
                  (:pattern "iterable" "i")
                  (:pattern "mixed" "i")
                  "null"
                  "object"
                  "string"
                  (:pattern "true" "i")
                  (:pattern "void" "i"))
  cast_type (:choice
             (:pattern "array" "i")
             (:pattern "binary" "i")
             (:pattern "bool" "i")
             (:pattern "boolean" "i")
             (:pattern "double" "i")
             (:pattern "float" "i")
             (:pattern "int" "i")
             (:pattern "integer" "i")
             (:pattern "object" "i")
             (:pattern "real" "i")
             (:pattern "string" "i")
             (:pattern "unset" "i"))
  _return_type (:seq ":" (:field :return_type (:choice type bottom_type)))
  _const_element (:seq name "=" expression)
  _class_const_element (:seq (:reserved :nothing name) "=" expression)
  echo_statement (:seq (:alias (:pattern "echo" "i") "echo") _expressions _semicolon)
  exit_statement (:seq
                  (:alias (:pattern "exit" "i") "exit")
                  (:choice (:seq "(" (:choice expression :blank) ")") :blank)
                  _semicolon)
  unset_statement (:seq
                   "unset"
                   "("
                   (:seq _variable (:repeat (:seq "," _variable)))
                   (:choice "," :blank)
                   ")"
                   _semicolon)
  declare_statement (:seq
                     (:alias (:pattern "declare" "i") "declare")
                     "("
                     declare_directive
                     ")"
                     (:choice
                      statement
                      _semicolon
                      (:seq
                       ":"
                       (:repeat statement)
                       (:alias (:pattern "enddeclare" "i") "enddeclare")
                       _semicolon)))
  declare_directive (:seq (:choice "ticks" "encoding" "strict_types") "=" literal)
  literal (:choice integer float _string boolean null)
  float (:pattern "\\d*(_\\d+)*((\\.\\d*(_\\d+)*)?([eE][\\+-]?\\d+(_\\d+)*)|(\\.\\d*(_\\d+)*)([eE][\\+-]?\\d+(_\\d+)*)?)")
  try_statement (:seq
                 (:alias (:pattern "try" "i") "try")
                 (:field :body compound_statement)
                 (:repeat1 (:choice catch_clause finally_clause)))
  catch_clause (:seq
                (:alias (:pattern "catch" "i") "catch")
                "("
                (:field :type type_list)
                (:choice (:field :name variable_name) :blank)
                ")"
                (:field :body compound_statement))
  type_list (:seq named_type (:repeat (:seq "|" named_type)))
  finally_clause (:seq
                  (:alias (:pattern "finally" "i") "finally")
                  (:field :body compound_statement))
  goto_statement (:seq (:alias (:pattern "goto" "i") "goto") name _semicolon)
  continue_statement (:seq
                      (:alias (:pattern "continue" "i") "continue")
                      (:choice expression :blank)
                      _semicolon)
  break_statement (:seq
                   (:alias (:pattern "break" "i") "break")
                   (:choice expression :blank)
                   _semicolon)
  integer (:token
           (:choice
            (:pattern "[1-9]\\d*(_\\d+)*")
            (:pattern "0[oO]?[0-7]*(_[0-7]+)*")
            (:pattern "0[xX][0-9a-fA-F]+(_[0-9a-fA-F]+)*")
            (:pattern "0[bB][01]+(_[01]+)*")))
  return_statement (:seq
                    (:alias (:pattern "return" "i") "return")
                    (:choice expression :blank)
                    _semicolon)
  throw_expression (:seq (:alias (:pattern "throw" "i") "throw") expression)
  while_statement (:seq
                   (:alias (:pattern "while" "i") "while")
                   (:field :condition parenthesized_expression)
                   (:choice
                    (:field :body statement)
                    (:seq
                     (:field :body colon_block)
                     (:alias (:pattern "endwhile" "i") "endwhile")
                     _semicolon)))
  do_statement (:seq
                (:alias (:pattern "do" "i") "do")
                (:field :body statement)
                (:alias (:pattern "while" "i") "while")
                (:field :condition parenthesized_expression)
                _semicolon)
  for_statement (:seq
                 (:alias (:pattern "for" "i") "for")
                 "("
                 (:field :initialize (:choice _expressions :blank))
                 ";"
                 (:field :condition (:choice _expressions :blank))
                 ";"
                 (:field :update (:choice _expressions :blank))
                 ")"
                 (:choice
                  _semicolon
                  (:field :body statement)
                  (:seq
                   ":"
                   (:field :body (:repeat statement))
                   (:alias (:pattern "endfor" "i") "endfor")
                   _semicolon)))
  _expressions (:choice expression sequence_expression)
  sequence_expression (:prec -1 (:seq expression "," (:choice sequence_expression expression)))
  foreach_statement (:seq
                     (:alias (:pattern "foreach" "i") "foreach")
                     "("
                     expression
                     (:alias (:pattern "as" "i") "as")
                     (:choice (:alias foreach_pair pair) _foreach_value)
                     ")"
                     (:choice
                      _semicolon
                      (:field :body statement)
                      (:seq
                       (:field :body colon_block)
                       (:alias (:pattern "endforeach" "i") "endforeach")
                       _semicolon)))
  foreach_pair (:seq expression "=>" _foreach_value)
  _foreach_value (:choice by_ref expression list_literal)
  if_statement (:seq
                (:alias (:pattern "if" "i") "if")
                (:field :condition parenthesized_expression)
                (:choice
                 (:seq
                  (:field :body statement)
                  (:repeat (:field :alternative else_if_clause))
                  (:choice (:field :alternative else_clause) :blank))
                 (:seq
                  (:field :body colon_block)
                  (:repeat (:field :alternative (:alias else_if_clause_2 else_if_clause)))
                  (:choice (:field :alternative (:alias else_clause_2 else_clause)) :blank)
                  (:alias (:pattern "endif" "i") "endif")
                  _semicolon)))
  colon_block (:seq ":" (:repeat statement))
  else_if_clause (:seq
                  (:alias (:pattern "elseif" "i") "elseif")
                  (:field :condition parenthesized_expression)
                  (:field :body statement))
  else_clause (:seq (:alias (:pattern "else" "i") "else") (:field :body statement))
  else_if_clause_2 (:seq
                    (:alias (:pattern "elseif" "i") "elseif")
                    (:field :condition parenthesized_expression)
                    (:field :body colon_block))
  else_clause_2 (:seq (:alias (:pattern "else" "i") "else") (:field :body colon_block))
  match_expression (:seq
                    (:alias (:pattern "match" "i") "match")
                    (:field :condition parenthesized_expression)
                    (:field :body match_block))
  match_block (:prec-left 0
               (:seq
                "{"
                (:choice
                 (:seq
                  (:choice match_conditional_expression match_default_expression)
                  (:repeat
                   (:seq "," (:choice match_conditional_expression match_default_expression))))
                 :blank)
                (:choice "," :blank)
                "}"))
  match_condition_list (:seq (:seq expression (:repeat (:seq "," expression))) (:choice "," :blank))
  match_conditional_expression (:seq
                                (:field :conditional_expressions match_condition_list)
                                "=>"
                                (:field :return_expression expression))
  match_default_expression (:seq
                            (:alias (:pattern "default" "i") "default")
                            "=>"
                            (:field :return_expression expression))
  switch_statement (:seq
                    (:alias (:pattern "switch" "i") "switch")
                    (:field :condition parenthesized_expression)
                    (:field :body switch_block))
  switch_block (:choice
                (:seq "{" (:repeat (:choice case_statement default_statement)) "}")
                (:seq
                 ":"
                 (:repeat (:choice case_statement default_statement))
                 (:alias (:pattern "endswitch" "i") "endswitch")
                 _semicolon))
  case_statement (:seq
                  (:alias (:pattern "case" "i") "case")
                  (:field :value expression)
                  (:choice ":" ";")
                  (:repeat statement))
  default_statement (:seq
                     (:alias (:pattern "default" "i") "default")
                     (:choice ":" ";")
                     (:repeat statement))
  compound_statement (:seq "{" (:repeat statement) "}")
  named_label_statement (:seq name ":")
  expression_statement (:seq expression _semicolon)
  expression (:choice
              conditional_expression
              match_expression
              augmented_assignment_expression
              assignment_expression
              reference_assignment_expression
              yield_expression
              _unary_expression
              error_suppression_expression
              binary_expression
              include_expression
              include_once_expression
              require_expression
              require_once_expression)
  _unary_expression (:choice
                     clone_expression
                     primary_expression
                     unary_op_expression
                     cast_expression)
  unary_op_expression (:prec-left 20
                       (:seq
                        (:field :operator (:choice "+" "-" "~" "!"))
                        (:field :argument expression)))
  error_suppression_expression (:prec 22 (:seq "@" expression))
  clone_expression (:seq (:alias (:pattern "clone" "i") "clone") primary_expression)
  primary_expression (:choice
                      _variable
                      literal
                      class_constant_access_expression
                      qualified_name
                      relative_name
                      name
                      array_creation_expression
                      print_intrinsic
                      anonymous_function
                      arrow_function
                      object_creation_expression
                      update_expression
                      shell_command_expression
                      parenthesized_expression
                      throw_expression)
  parenthesized_expression (:seq "(" expression ")")
  class_constant_access_expression (:seq
                                    _scope_resolution_qualifier
                                    "::"
                                    (:choice
                                     (:reserved :nothing name)
                                     (:seq "{" (:alias expression name) "}")))
  print_intrinsic (:seq (:alias (:pattern "print" "i") "print") expression)
  object_creation_expression (:choice
                              _new_dereferencable_expression
                              _new_non_dereferencable_expression)
  _new_non_dereferencable_expression (:prec-right 24
                                      (:seq
                                       (:alias (:pattern "new" "i") "new")
                                       _class_name_reference))
  _new_dereferencable_expression (:prec-right 24
                                  (:seq
                                   (:alias (:pattern "new" "i") "new")
                                   (:choice (:seq _class_name_reference arguments) anonymous_class)))
  _class_name_reference (:choice _name _new_variable parenthesized_expression)
  anonymous_class (:prec-right 0
                   (:seq
                    (:choice (:field :attributes attribute_list) :blank)
                    (:repeat _modifier)
                    (:alias (:pattern "class" "i") "class")
                    (:choice arguments :blank)
                    (:choice base_clause :blank)
                    (:choice class_interface_clause :blank)
                    (:field :body declaration_list)))
  update_expression (:prec-left 22
                     (:choice
                      (:seq (:field :operator (:choice "--" "++")) (:field :argument _variable))
                      (:seq (:field :argument _variable) (:field :operator (:choice "--" "++")))))
  cast_expression (:prec -1
                   (:seq
                    "("
                    (:field :type cast_type)
                    ")"
                    (:field :value
                     (:choice
                      _unary_expression
                      include_expression
                      include_once_expression
                      error_suppression_expression))))
  cast_variable (:prec -1 (:seq "(" (:field :type cast_type) ")" (:field :value _variable)))
  assignment_expression (:prec-right 4
                         (:seq
                          (:field :left (:choice _variable list_literal))
                          "="
                          (:field :right expression)))
  reference_assignment_expression (:prec-right 4
                                   (:seq
                                    (:field :left (:choice _variable list_literal))
                                    "="
                                    "&"
                                    (:field :right expression)))
  conditional_expression (:prec-left 5
                          (:seq
                           (:field :condition expression)
                           "?"
                           (:field :body (:choice expression :blank))
                           ":"
                           (:field :alternative expression)))
  augmented_assignment_expression (:prec-right 4
                                   (:seq
                                    (:field :left _variable)
                                    (:field :operator
                                     (:choice
                                      "**="
                                      "*="
                                      "/="
                                      "%="
                                      "+="
                                      "-="
                                      ".="
                                      "<<="
                                      ">>="
                                      "&="
                                      "^="
                                      "|="
                                      "??="))
                                    (:field :right expression)))
  _variable (:choice
             (:alias cast_variable cast_expression)
             _new_variable
             _callable_variable
             scoped_property_access_expression
             member_access_expression
             nullsafe_member_access_expression)
  _variable_member_access_expression (:prec 26
                                      (:seq (:field :object _new_variable) "->" _member_name))
  member_access_expression (:prec 26
                            (:seq (:field :object _dereferencable_expression) "->" _member_name))
  _variable_nullsafe_member_access_expression (:prec 26
                                               (:seq
                                                (:field :object _new_variable)
                                                "?->"
                                                _member_name))
  nullsafe_member_access_expression (:prec 26
                                     (:seq
                                      (:field :object _dereferencable_expression)
                                      "?->"
                                      _member_name))
  _variable_scoped_property_access_expression (:prec 26
                                               (:seq
                                                (:field :scope (:choice _name _new_variable))
                                                "::"
                                                (:field :name _simple_variable)))
  scoped_property_access_expression (:prec 26
                                     (:seq
                                      (:field :scope _scope_resolution_qualifier)
                                      "::"
                                      (:field :name _simple_variable)))
  list_literal (:choice _list_destructing _array_destructing)
  _list_destructing (:seq
                     (:alias (:pattern "list" "i") "list")
                     "("
                     (:seq
                      (:choice
                       (:choice
                        (:alias _list_destructing list_literal)
                        _variable
                        by_ref
                        (:seq
                         expression
                         "=>"
                         (:choice (:alias _list_destructing list_literal) _variable by_ref)))
                       :blank)
                      (:repeat
                       (:seq
                        ","
                        (:choice
                         (:choice
                          (:alias _list_destructing list_literal)
                          _variable
                          by_ref
                          (:seq
                           expression
                           "=>"
                           (:choice (:alias _list_destructing list_literal) _variable by_ref)))
                         :blank))))
                     ")")
  _array_destructing (:seq
                      "["
                      (:seq
                       (:choice _array_destructing_element :blank)
                       (:repeat (:seq "," (:choice _array_destructing_element :blank))))
                      "]")
  _array_destructing_element (:choice
                              (:choice (:alias _array_destructing list_literal) _variable by_ref)
                              (:seq
                               expression
                               "=>"
                               (:choice (:alias _array_destructing list_literal) _variable by_ref)))
  function_call_expression (:prec 25
                            (:seq
                             (:field :function (:choice _name _callable_expression))
                             (:field :arguments arguments)))
  _callable_expression (:choice
                        _callable_variable
                        parenthesized_expression
                        _dereferencable_scalar
                        (:alias _new_dereferencable_expression object_creation_expression))
  scoped_call_expression (:prec 25
                          (:seq
                           (:field :scope _scope_resolution_qualifier)
                           "::"
                           _member_name
                           (:field :arguments arguments)))
  _scope_resolution_qualifier (:choice relative_scope _name _dereferencable_expression)
  relative_scope (:prec 23
                  (:choice
                   (:alias (:pattern "self" "i") "self")
                   (:alias (:pattern "parent" "i") "parent")
                   (:alias (:pattern "static" "i") "static")))
  variadic_placeholder "..."
  arguments (:seq
             "("
             (:choice
              (:choice
               (:seq (:seq argument (:repeat (:seq "," argument))) (:choice "," :blank))
               variadic_placeholder)
              :blank)
             ")")
  argument (:seq
            (:choice _argument_name :blank)
            (:choice (:field :reference_modifier reference_modifier) :blank)
            (:choice (:alias relative_scope name) variadic_unpacking expression))
  _argument_name (:seq
                  (:field :name
                   (:alias
                    (:choice
                     (:reserved :nothing name)
                     (:pattern "array" "i")
                     (:pattern "fn" "i")
                     (:pattern "function" "i")
                     (:pattern "match" "i")
                     (:pattern "namespace" "i")
                     (:pattern "null" "i")
                     (:pattern "static" "i")
                     (:pattern "throw" "i")
                     (:pattern "parent" "i")
                     (:pattern "self" "i")
                     (:pattern "true|false" "i"))
                    name))
                  ":")
  member_call_expression (:prec 25
                          (:seq
                           (:field :object _dereferencable_expression)
                           "->"
                           _member_name
                           (:field :arguments arguments)))
  nullsafe_member_call_expression (:prec 25
                                   (:seq
                                    (:field :object _dereferencable_expression)
                                    "?->"
                                    _member_name
                                    (:field :arguments arguments)))
  variadic_unpacking (:seq "..." expression)
  _member_name (:choice
                (:field :name (:choice (:reserved :nothing name) _simple_variable))
                (:seq "{" (:field :name expression) "}"))
  _variable_subscript_expression (:seq _new_variable (:seq "[" (:choice expression :blank) "]"))
  _dereferencable_subscript_expression (:seq
                                        _dereferencable_expression
                                        (:seq "[" (:choice expression :blank) "]"))
  _dereferencable_expression (:prec 27
                              (:choice
                               _variable
                               (:alias _new_dereferencable_expression object_creation_expression)
                               class_constant_access_expression
                               parenthesized_expression
                               _dereferencable_scalar
                               _name))
  _dereferencable_scalar (:prec 27 (:choice array_creation_expression _string))
  array_creation_expression (:choice
                             (:seq
                              (:alias (:pattern "array" "i") "array")
                              "("
                              (:choice
                               (:seq
                                array_element_initializer
                                (:repeat (:seq "," array_element_initializer)))
                               :blank)
                              (:choice "," :blank)
                              ")")
                             (:seq
                              "["
                              (:choice
                               (:seq
                                array_element_initializer
                                (:repeat (:seq "," array_element_initializer)))
                               :blank)
                              (:choice "," :blank)
                              "]"))
  attribute_group (:seq
                   "#["
                   (:seq attribute (:repeat (:seq "," attribute)))
                   (:choice "," :blank)
                   "]")
  attribute_list (:repeat1 attribute_group)
  attribute (:seq _name (:choice (:field :parameters arguments) :blank))
  _complex_string_part (:seq "{" expression "}")
  _simple_string_member_access_expression (:prec 26
                                           (:seq
                                            (:field :object variable_name)
                                            "->"
                                            (:field :name name)))
  _simple_string_subscript_unary_expression (:prec-left 0 (:seq "-" integer))
  _simple_string_array_access_argument (:choice
                                        integer
                                        (:alias
                                         _simple_string_subscript_unary_expression
                                         unary_op_expression)
                                        name
                                        variable_name)
  _simple_string_subscript_expression (:prec 27
                                       (:seq
                                        variable_name
                                        (:seq "[" _simple_string_array_access_argument "]")))
  _simple_string_part (:choice
                       (:alias _simple_string_member_access_expression member_access_expression)
                       _simple_variable
                       (:alias _simple_string_subscript_expression subscript_expression))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     "n"
                     "r"
                     "t"
                     "v"
                     "e"
                     "f"
                     "\\"
                     (:pattern "\\$")
                     "\""
                     "`"
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9A-Fa-f]{1,2}")
                     (:pattern "u\\{[0-9A-Fa-f]+\\}"))))
  _interpolated_string_body (:repeat1
                             (:choice
                              escape_sequence
                              (:seq
                               variable_name
                               (:alias encapsed_string_chars_after_variable string_content))
                              (:alias encapsed_string_chars string_content)
                              _simple_string_part
                              _complex_string_part
                              (:alias "\\u" string_content)))
  _interpolated_string_body_heredoc (:repeat1
                                     (:choice
                                      escape_sequence
                                      (:seq
                                       variable_name
                                       (:alias
                                        encapsed_string_chars_after_variable_heredoc
                                        string_content))
                                      (:alias encapsed_string_chars_heredoc string_content)
                                      _simple_string_part
                                      _complex_string_part
                                      (:alias "\\u" string_content)))
  encapsed_string (:prec-right 0
                   (:seq
                    (:choice (:pattern "[bB]\"") "\"")
                    (:choice _interpolated_string_body :blank)
                    "\""))
  string (:seq
          (:choice (:pattern "[bB]'") "'")
          (:repeat
           (:choice (:alias (:token (:choice "\\\\" "\\'")) escape_sequence) string_content))
          "'")
  string_content (:prec-right 0 (:repeat1 (:token-immediate (:prec 1 (:pattern "\\\\?[^'\\\\]+")))))
  heredoc_body (:seq
                _new_line
                (:repeat1
                 (:prec-right 0 (:seq (:choice _new_line :blank) _interpolated_string_body_heredoc))))
  heredoc (:seq
           (:token "<<<")
           (:choice "\"" :blank)
           (:field :identifier heredoc_start)
           (:choice (:token-immediate "\"") :blank)
           (:choice
            (:seq (:field :value heredoc_body) _new_line)
            (:field :value (:choice heredoc_body :blank)))
           (:field :end_tag heredoc_end))
  _new_line (:pattern "\\r?\\n|\\r")
  nowdoc_body (:seq _new_line (:repeat1 nowdoc_string))
  nowdoc (:seq
          (:token "<<<")
          "'"
          (:field :identifier heredoc_start)
          (:token-immediate "'")
          (:choice
           (:seq (:field :value nowdoc_body) _new_line)
           (:field :value (:choice nowdoc_body :blank)))
          (:field :end_tag heredoc_end))
  _interpolated_execution_operator_body (:repeat1
                                         (:choice
                                          escape_sequence
                                          (:seq
                                           variable_name
                                           (:alias
                                            execution_string_chars_after_variable
                                            string_content))
                                          (:alias execution_string_chars string_content)
                                          _simple_string_part
                                          _complex_string_part
                                          (:alias "\\u" string_content)))
  shell_command_expression (:seq "`" (:choice _interpolated_execution_operator_body :blank) "`")
  boolean (:pattern "true|false" "i")
  null (:pattern "null" "i")
  _string (:choice encapsed_string string heredoc nowdoc)
  dynamic_variable_name (:choice (:seq "$" _simple_variable) (:seq "$" "{" expression "}"))
  _simple_variable (:choice variable_name dynamic_variable_name)
  _new_variable (:prec 1
                 (:choice
                  _simple_variable
                  (:alias _variable_subscript_expression subscript_expression)
                  (:alias _variable_member_access_expression member_access_expression)
                  (:alias
                   _variable_nullsafe_member_access_expression
                   nullsafe_member_access_expression)
                  (:alias
                   _variable_scoped_property_access_expression
                   scoped_property_access_expression)))
  _callable_variable (:choice
                      _simple_variable
                      (:alias _dereferencable_subscript_expression subscript_expression)
                      member_call_expression
                      nullsafe_member_call_expression
                      function_call_expression
                      scoped_call_expression)
  variable_name (:seq "$" (:reserved :nothing name))
  by_ref (:seq "&" _variable)
  yield_expression (:prec-right 0
                    (:choice
                     (:seq
                      (:alias (:pattern "yield" "i") "yield")
                      (:choice array_element_initializer :blank))
                     (:seq (:alias (:pattern "yield from" "i") "yield from") expression)))
  array_element_initializer (:prec-right 0
                             (:choice
                              (:choice by_ref expression)
                              (:seq expression "=>" (:choice by_ref expression))
                              variadic_unpacking))
  binary_expression (:choice
                     (:prec 21
                      (:seq
                       (:field :left _unary_expression)
                       (:field :operator (:alias (:pattern "instanceof" "i") "instanceof"))
                       (:field :right _class_name_reference)))
                     (:prec-right 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "??")
                       (:field :right expression)))
                     (:prec-right 19
                      (:seq
                       (:field :left expression)
                       (:field :operator "**")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator (:alias (:pattern "and" "i") "and"))
                       (:field :right expression)))
                     (:prec-left 1
                      (:seq
                       (:field :left expression)
                       (:field :operator (:alias (:pattern "or" "i") "or"))
                       (:field :right expression)))
                     (:prec-left 2
                      (:seq
                       (:field :left expression)
                       (:field :operator (:alias (:pattern "xor" "i") "xor"))
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "||")
                       (:field :right expression)))
                     (:prec-left 8
                      (:seq
                       (:field :left expression)
                       (:field :operator "&&")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "^")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "==")
                       (:field :right expression)))
                     (:prec-left 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "!=")
                       (:field :right expression)))
                     (:prec-left 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "<>")
                       (:field :right expression)))
                     (:prec-left 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "===")
                       (:field :right expression)))
                     (:prec-left 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "!==")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator "<")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator ">")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator ">=")
                       (:field :right expression)))
                     (:prec-left 12
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=>")
                       (:field :right expression)))
                     (:prec-left 14
                      (:seq
                       (:field :left expression)
                       (:field :operator "|>")
                       (:field :right expression)))
                     (:prec-left 15
                      (:seq
                       (:field :left expression)
                       (:field :operator ".")
                       (:field :right expression)))
                     (:prec-left 16
                      (:seq
                       (:field :left expression)
                       (:field :operator "<<")
                       (:field :right expression)))
                     (:prec-left 16
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>")
                       (:field :right expression)))
                     (:prec-left 17
                      (:seq
                       (:field :left expression)
                       (:field :operator "+")
                       (:field :right expression)))
                     (:prec-left 17
                      (:seq
                       (:field :left expression)
                       (:field :operator "-")
                       (:field :right expression)))
                     (:prec-left 18
                      (:seq
                       (:field :left expression)
                       (:field :operator "*")
                       (:field :right expression)))
                     (:prec-left 18
                      (:seq
                       (:field :left expression)
                       (:field :operator "/")
                       (:field :right expression)))
                     (:prec-left 18
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
                       (:field :right expression))))
  include_expression (:seq (:alias (:pattern "include" "i") "include") expression)
  include_once_expression (:seq (:alias (:pattern "include_once" "i") "include_once") expression)
  require_expression (:seq (:alias (:pattern "require" "i") "require") expression)
  require_once_expression (:seq (:alias (:pattern "require_once" "i") "require_once") expression)
  name (:pattern "[_a-zA-Z\\u0080-\\u009f\\u00a1-\\u200a\\u200c-\\u205f\\u2061-\\ufefe\\uff00-\\uffff][_a-zA-Z\\u0080-\\u009f\\u00a1-\\u200a\\u200c-\\u205f\\u2061-\\ufefe\\uff00-\\uffff\\d]*")
  comment (:token
           (:choice
            (:seq
             (:choice "//" (:pattern "#[^?\\[?\\r?\\n]"))
             (:repeat (:pattern "[^?\\r?\\n]|\\?[^>\\r\\n]"))
             (:choice (:pattern "\\?\\r?\\n") :blank))
            "#"
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))
  _semicolon (:choice _automatic_semicolon ";")}}
