# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "java"
 :word identifier
 :extras [line_comment block_comment (:pattern "\\s")]
 :conflicts [[modifiers annotated_type receiver_parameter]
             [modifiers annotated_type module_declaration package_declaration]
             [_unannotated_type primary_expression inferred_parameters]
             [_unannotated_type primary_expression]
             [_unannotated_type primary_expression scoped_type_identifier]
             [_unannotated_type scoped_type_identifier]
             [_unannotated_type generic_type]
             [generic_type primary_expression]
             [expression statement]
             [lambda_expression primary_expression]
             [inferred_parameters primary_expression]
             [argument_list record_pattern_body]
             [yield_statement _reserved_identifier]]
 :precedences []
 :externals []
 :inline [_name _simple_type _class_body_declaration _variable_initializer]
 :supertypes [expression
              declaration
              statement
              primary_expression
              _literal
              _type
              _simple_type
              _unannotated_type
              module_directive]
 :rules
 {program (:repeat _toplevel_statement)
  _toplevel_statement (:choice statement method_declaration)
  _literal (:choice
            decimal_integer_literal
            hex_integer_literal
            octal_integer_literal
            binary_integer_literal
            decimal_floating_point_literal
            hex_floating_point_literal
            (:ref "true")
            (:ref "false")
            character_literal
            string_literal
            null_literal)
  decimal_integer_literal (:token
                           (:seq
                            (:token
                             (:choice
                              "0"
                              (:seq
                               (:pattern "[1-9]")
                               (:choice
                                (:seq
                                 (:choice "_" :blank)
                                 (:seq
                                  (:pattern "[0-9]+")
                                  (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                :blank))))
                            (:choice (:choice "l" "L") :blank)))
  hex_integer_literal (:token
                       (:seq
                        (:choice "0x" "0X")
                        (:token
                         (:seq
                          (:pattern "[A-Fa-f0-9]+")
                          (:repeat (:seq "_" (:pattern "[A-Fa-f0-9]+")))))
                        (:choice (:choice "l" "L") :blank)))
  octal_integer_literal (:token
                         (:seq
                          (:choice "0o" "0O" "0")
                          (:seq (:pattern "[0-7]+") (:repeat (:seq "_" (:pattern "[0-7]+"))))
                          (:choice (:choice "l" "L") :blank)))
  binary_integer_literal (:token
                          (:seq
                           (:choice "0b" "0B")
                           (:seq (:pattern "[01]+") (:repeat (:seq "_" (:pattern "[01]+"))))
                           (:choice (:choice "l" "L") :blank)))
  decimal_floating_point_literal (:token
                                  (:choice
                                   (:seq
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq "_" (:pattern "[0-9]+")))))
                                    "."
                                    (:choice
                                     (:token
                                      (:seq
                                       (:pattern "[0-9]+")
                                       (:repeat (:seq "_" (:pattern "[0-9]+")))))
                                     :blank)
                                    (:choice
                                     (:seq
                                      (:pattern "[eE]")
                                      (:choice (:choice "-" "+") :blank)
                                      (:token
                                       (:seq
                                        (:pattern "[0-9]+")
                                        (:repeat (:seq "_" (:pattern "[0-9]+"))))))
                                     :blank)
                                    (:choice (:pattern "[fFdD]") :blank))
                                   (:seq
                                    "."
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq "_" (:pattern "[0-9]+")))))
                                    (:choice
                                     (:seq
                                      (:pattern "[eE]")
                                      (:choice (:choice "-" "+") :blank)
                                      (:token
                                       (:seq
                                        (:pattern "[0-9]+")
                                        (:repeat (:seq "_" (:pattern "[0-9]+"))))))
                                     :blank)
                                    (:choice (:pattern "[fFdD]") :blank))
                                   (:seq
                                    (:token
                                     (:choice
                                      "0"
                                      (:seq
                                       (:pattern "[1-9]")
                                       (:choice
                                        (:seq
                                         (:choice "_" :blank)
                                         (:seq
                                          (:pattern "[0-9]+")
                                          (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                        :blank))))
                                    (:pattern "[eE]")
                                    (:choice (:choice "-" "+") :blank)
                                    (:token
                                     (:seq
                                      (:pattern "[0-9]+")
                                      (:repeat (:seq "_" (:pattern "[0-9]+")))))
                                    (:choice (:pattern "[fFdD]") :blank))
                                   (:seq
                                    (:token
                                     (:choice
                                      "0"
                                      (:seq
                                       (:pattern "[1-9]")
                                       (:choice
                                        (:seq
                                         (:choice "_" :blank)
                                         (:seq
                                          (:pattern "[0-9]+")
                                          (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                        :blank))))
                                    (:choice
                                     (:seq
                                      (:pattern "[eE]")
                                      (:choice (:choice "-" "+") :blank)
                                      (:token
                                       (:seq
                                        (:pattern "[0-9]+")
                                        (:repeat (:seq "_" (:pattern "[0-9]+"))))))
                                     :blank)
                                    (:pattern "[fFdD]"))))
  hex_floating_point_literal (:token
                              (:seq
                               (:choice "0x" "0X")
                               (:choice
                                (:seq
                                 (:token
                                  (:seq
                                   (:pattern "[A-Fa-f0-9]+")
                                   (:repeat (:seq "_" (:pattern "[A-Fa-f0-9]+")))))
                                 (:choice "." :blank))
                                (:seq
                                 (:choice
                                  (:token
                                   (:seq
                                    (:pattern "[A-Fa-f0-9]+")
                                    (:repeat (:seq "_" (:pattern "[A-Fa-f0-9]+")))))
                                  :blank)
                                 "."
                                 (:token
                                  (:seq
                                   (:pattern "[A-Fa-f0-9]+")
                                   (:repeat (:seq "_" (:pattern "[A-Fa-f0-9]+")))))))
                               (:choice
                                (:seq
                                 (:pattern "[pP]")
                                 (:choice (:choice "-" "+") :blank)
                                 (:token
                                  (:choice
                                   "0"
                                   (:seq
                                    (:pattern "[1-9]")
                                    (:choice
                                     (:seq
                                      (:choice "_" :blank)
                                      (:seq
                                       (:pattern "[0-9]+")
                                       (:repeat (:seq (:pattern "_+") (:pattern "[0-9]+")))))
                                     :blank))))
                                 (:choice (:pattern "[fFdD]") :blank))
                                :blank)))
  (:ref "true") "true"
  (:ref "false") "false"
  character_literal (:token
                     (:seq
                      "'"
                      (:repeat1
                       (:choice (:pattern "[^\\\\'\\n]") (:pattern "\\\\.") (:pattern "\\\\\\n")))
                      "'"))
  string_literal (:choice _string_literal _multiline_string_literal)
  _string_literal (:seq
                   "\""
                   (:repeat (:choice string_fragment escape_sequence string_interpolation))
                   "\"")
  _multiline_string_literal (:seq
                             "\"\"\""
                             (:repeat
                              (:choice
                               (:alias _multiline_string_fragment multiline_string_fragment)
                               _escape_sequence
                               string_interpolation))
                             "\"\"\"")
  string_fragment (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+")))
  _multiline_string_fragment (:choice (:pattern "[^\"\\\\]+") (:pattern "\"([^\"\\\\]|\\\\\")*"))
  string_interpolation (:seq "\\{" expression "}")
  _escape_sequence (:choice
                    (:prec 2 (:token-immediate (:seq "\\" (:pattern "[^bfnrts'\\\"\\\\]"))))
                    (:prec 1 escape_sequence))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu0-7]")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9a-fA-F]{2}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}"))))
  null_literal "null"
  expression (:choice
              assignment_expression
              binary_expression
              instanceof_expression
              lambda_expression
              ternary_expression
              update_expression
              primary_expression
              unary_expression
              cast_expression
              switch_expression)
  cast_expression (:prec 14
                   (:choice
                    (:seq "(" (:field :type _type) ")" (:field :value expression))
                    (:seq
                     "("
                     (:seq (:field :type _type) (:repeat (:seq "&" (:field :type _type))))
                     ")"
                     (:field :value (:choice primary_expression lambda_expression)))))
  assignment_expression (:prec-right 1
                         (:seq
                          (:field :left
                           (:choice identifier _reserved_identifier field_access array_access))
                          (:field :operator
                           (:choice "=" "+=" "-=" "*=" "/=" "&=" "|=" "^=" "%=" "<<=" ">>=" ">>>="))
                          (:field :right expression)))
  binary_expression (:choice
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator ">")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "<")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator ">=")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "==")
                       (:field :right expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left expression)
                       (:field :operator "!=")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator "&&")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator "||")
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
                     (:prec-left 8
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "^")
                       (:field :right expression)))
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
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
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>>")
                       (:field :right expression))))
  instanceof_expression (:prec 10
                         (:seq
                          (:field :left expression)
                          "instanceof"
                          (:choice "final" :blank)
                          (:choice
                           (:seq
                            (:field :right _type)
                            (:choice
                             (:field :name (:choice identifier _reserved_identifier))
                             :blank))
                           (:field :pattern record_pattern))))
  lambda_expression (:seq
                     (:field :parameters
                      (:choice
                       identifier
                       formal_parameters
                       inferred_parameters
                       _reserved_identifier))
                     "->"
                     (:field :body (:choice expression block)))
  inferred_parameters (:seq
                       "("
                       (:seq
                        (:choice identifier _reserved_identifier)
                        (:repeat (:seq "," (:choice identifier _reserved_identifier))))
                       ")")
  ternary_expression (:prec-right 3
                      (:seq
                       (:field :condition expression)
                       "?"
                       (:field :consequence expression)
                       ":"
                       (:field :alternative expression)))
  unary_expression (:choice
                    (:prec-left 15 (:seq (:field :operator "+") (:field :operand expression)))
                    (:prec-left 15 (:seq (:field :operator "-") (:field :operand expression)))
                    (:prec-left 15 (:seq (:field :operator "!") (:field :operand expression)))
                    (:prec-left 15 (:seq (:field :operator "~") (:field :operand expression))))
  update_expression (:prec-left 15
                     (:choice
                      (:seq expression "++")
                      (:seq expression "--")
                      (:seq "++" expression)
                      (:seq "--" expression)))
  primary_expression (:choice
                      _literal
                      class_literal
                      this
                      identifier
                      _reserved_identifier
                      parenthesized_expression
                      object_creation_expression
                      field_access
                      array_access
                      method_invocation
                      method_reference
                      array_creation_expression
                      template_expression)
  array_creation_expression (:prec-right 0
                             (:seq
                              "new"
                              (:repeat _annotation)
                              (:field :type _simple_type)
                              (:choice
                               (:seq
                                (:field :dimensions (:repeat1 dimensions_expr))
                                (:field :dimensions (:choice dimensions :blank)))
                               (:seq
                                (:field :dimensions dimensions)
                                (:field :value array_initializer)))))
  dimensions_expr (:seq (:repeat _annotation) "[" expression "]")
  parenthesized_expression (:seq "(" expression ")")
  class_literal (:prec-dynamic 17 (:seq _unannotated_type "." "class"))
  object_creation_expression (:choice
                              _unqualified_object_creation_expression
                              (:seq primary_expression "." _unqualified_object_creation_expression))
  _unqualified_object_creation_expression (:prec-right 0
                                           (:seq
                                            "new"
                                            (:choice
                                             (:seq
                                              (:repeat _annotation)
                                              (:field :type_arguments type_arguments)
                                              (:repeat _annotation))
                                             (:repeat _annotation))
                                            (:field :type _simple_type)
                                            (:field :arguments argument_list)
                                            (:choice class_body :blank)))
  field_access (:seq
                (:field :object (:choice primary_expression super))
                (:choice (:seq "." super) :blank)
                "."
                (:field :field (:choice identifier _reserved_identifier this)))
  template_expression (:seq
                       (:field :template_processor primary_expression)
                       "."
                       (:field :template_argument string_literal))
  array_access (:seq (:field :array primary_expression) "[" (:field :index expression) "]")
  method_invocation (:seq
                     (:choice
                      (:field :name (:choice identifier _reserved_identifier))
                      (:seq
                       (:field :object (:choice primary_expression super))
                       "."
                       (:choice (:seq super ".") :blank)
                       (:field :type_arguments (:choice type_arguments :blank))
                       (:field :name (:choice identifier _reserved_identifier))))
                     (:field :arguments argument_list))
  argument_list (:seq "(" (:choice (:seq expression (:repeat (:seq "," expression))) :blank) ")")
  method_reference (:seq
                    (:choice _type primary_expression super)
                    "::"
                    (:choice type_arguments :blank)
                    (:choice "new" identifier))
  type_arguments (:seq
                  "<"
                  (:choice
                   (:seq (:choice _type wildcard) (:repeat (:seq "," (:choice _type wildcard))))
                   :blank)
                  ">")
  wildcard (:seq (:repeat _annotation) "?" (:choice _wildcard_bounds :blank))
  _wildcard_bounds (:choice (:seq "extends" _type) (:seq super _type))
  dimensions (:prec-right 0 (:repeat1 (:seq (:repeat _annotation) "[" "]")))
  switch_expression (:seq
                     "switch"
                     (:field :condition parenthesized_expression)
                     (:field :body switch_block))
  switch_block (:seq "{" (:choice (:repeat switch_block_statement_group) (:repeat switch_rule)) "}")
  switch_block_statement_group (:prec-left 0
                                (:seq (:repeat1 (:seq switch_label ":")) (:repeat statement)))
  switch_rule (:seq switch_label "->" (:choice expression_statement throw_statement block))
  switch_label (:choice
                (:seq
                 "case"
                 (:choice pattern (:seq expression (:repeat (:seq "," expression))))
                 (:choice guard :blank))
                "default")
  pattern (:choice type_pattern record_pattern)
  type_pattern (:seq _unannotated_type (:choice identifier _reserved_identifier))
  record_pattern (:seq (:choice identifier _reserved_identifier generic_type) record_pattern_body)
  record_pattern_body (:seq
                       "("
                       (:choice
                        (:seq
                         (:choice record_pattern_component record_pattern)
                         (:repeat (:seq "," (:choice record_pattern_component record_pattern))))
                        :blank)
                       ")")
  record_pattern_component (:choice
                            underscore_pattern
                            (:seq _unannotated_type (:choice identifier _reserved_identifier)))
  underscore_pattern "_"
  guard (:seq "when" expression)
  statement (:choice
             declaration
             expression_statement
             labeled_statement
             if_statement
             while_statement
             for_statement
             enhanced_for_statement
             block
             ";"
             assert_statement
             do_statement
             break_statement
             continue_statement
             return_statement
             yield_statement
             switch_expression
             synchronized_statement
             local_variable_declaration
             throw_statement
             try_statement
             try_with_resources_statement)
  block (:seq "{" (:repeat statement) "}")
  expression_statement (:seq expression ";")
  labeled_statement (:seq identifier ":" statement)
  assert_statement (:choice
                    (:seq "assert" expression ";")
                    (:seq "assert" expression ":" expression ";"))
  do_statement (:seq
                "do"
                (:field :body statement)
                "while"
                (:field :condition parenthesized_expression)
                ";")
  break_statement (:seq "break" (:choice identifier :blank) ";")
  continue_statement (:seq "continue" (:choice identifier :blank) ";")
  return_statement (:seq "return" (:choice expression :blank) ";")
  yield_statement (:seq "yield" expression ";")
  synchronized_statement (:seq "synchronized" parenthesized_expression (:field :body block))
  throw_statement (:seq "throw" expression ";")
  try_statement (:seq
                 "try"
                 (:field :body block)
                 (:choice (:repeat1 catch_clause) (:seq (:repeat catch_clause) finally_clause)))
  catch_clause (:seq "catch" "(" catch_formal_parameter ")" (:field :body block))
  catch_formal_parameter (:seq (:choice modifiers :blank) catch_type _variable_declarator_id)
  catch_type (:seq _unannotated_type (:repeat (:seq "|" _unannotated_type)))
  finally_clause (:seq "finally" block)
  try_with_resources_statement (:seq
                                "try"
                                (:field :resources resource_specification)
                                (:field :body block)
                                (:repeat catch_clause)
                                (:choice finally_clause :blank))
  resource_specification (:seq
                          "("
                          (:seq resource (:repeat (:seq ";" resource)))
                          (:choice ";" :blank)
                          ")")
  resource (:choice
            (:seq
             (:choice modifiers :blank)
             (:field :type _unannotated_type)
             _variable_declarator_id
             "="
             (:field :value expression))
            identifier
            field_access)
  if_statement (:prec-right 0
                (:seq
                 "if"
                 (:field :condition parenthesized_expression)
                 (:field :consequence statement)
                 (:choice (:seq "else" (:field :alternative statement)) :blank)))
  while_statement (:seq
                   "while"
                   (:field :condition parenthesized_expression)
                   (:field :body statement))
  for_statement (:seq
                 "for"
                 "("
                 (:choice
                  (:field :init local_variable_declaration)
                  (:seq
                   (:choice
                    (:seq (:field :init expression) (:repeat (:seq "," (:field :init expression))))
                    :blank)
                   ";"))
                 (:field :condition (:choice expression :blank))
                 ";"
                 (:choice
                  (:seq
                   (:field :update expression)
                   (:repeat (:seq "," (:field :update expression))))
                  :blank)
                 ")"
                 (:field :body statement))
  enhanced_for_statement (:seq
                          "for"
                          "("
                          (:choice modifiers :blank)
                          (:field :type _unannotated_type)
                          _variable_declarator_id
                          ":"
                          (:field :value expression)
                          ")"
                          (:field :body statement))
  _annotation (:choice marker_annotation annotation)
  marker_annotation (:seq "@" (:field :name _name))
  annotation (:seq "@" (:field :name _name) (:field :arguments annotation_argument_list))
  annotation_argument_list (:seq
                            "("
                            (:choice
                             _element_value
                             (:choice
                              (:seq element_value_pair (:repeat (:seq "," element_value_pair)))
                              :blank))
                            ")")
  element_value_pair (:seq
                      (:field :key (:choice identifier _reserved_identifier))
                      "="
                      (:field :value _element_value))
  _element_value (:prec 2 (:choice expression element_value_array_initializer _annotation))
  element_value_array_initializer (:seq
                                   "{"
                                   (:choice
                                    (:seq _element_value (:repeat (:seq "," _element_value)))
                                    :blank)
                                   (:choice "," :blank)
                                   "}")
  declaration (:prec 2
               (:choice
                module_declaration
                package_declaration
                import_declaration
                class_declaration
                record_declaration
                interface_declaration
                annotation_type_declaration
                enum_declaration))
  module_declaration (:seq
                      (:repeat _annotation)
                      (:choice "open" :blank)
                      "module"
                      (:field :name _name)
                      (:field :body module_body))
  module_body (:seq "{" (:repeat module_directive) "}")
  module_directive (:choice
                    requires_module_directive
                    exports_module_directive
                    opens_module_directive
                    uses_module_directive
                    provides_module_directive)
  requires_module_directive (:seq
                             "requires"
                             (:repeat (:field :modifiers requires_modifier))
                             (:field :module _name)
                             ";")
  requires_modifier (:choice "transitive" "static")
  exports_module_directive (:seq
                            "exports"
                            (:field :package _name)
                            (:choice
                             (:seq
                              "to"
                              (:field :modules _name)
                              (:repeat (:seq "," (:field :modules _name))))
                             :blank)
                            ";")
  opens_module_directive (:seq
                          "opens"
                          (:field :package _name)
                          (:choice
                           (:seq
                            "to"
                            (:field :modules _name)
                            (:repeat (:seq "," (:field :modules _name))))
                           :blank)
                          ";")
  uses_module_directive (:seq "uses" (:field :type _name) ";")
  provides_module_directive (:seq
                             "provides"
                             (:field :provided _name)
                             "with"
                             _name
                             (:repeat (:seq "," (:field :provider _name)))
                             ";")
  package_declaration (:seq (:repeat _annotation) "package" _name ";")
  import_declaration (:seq
                      "import"
                      (:choice "static" :blank)
                      _name
                      (:choice (:seq "." asterisk) :blank)
                      ";")
  asterisk "*"
  enum_declaration (:seq
                    (:choice modifiers :blank)
                    "enum"
                    (:field :name identifier)
                    (:field :interfaces (:choice super_interfaces :blank))
                    (:field :body enum_body))
  enum_body (:seq
             "{"
             (:choice (:seq enum_constant (:repeat (:seq "," enum_constant))) :blank)
             (:choice "," :blank)
             (:choice enum_body_declarations :blank)
             "}")
  enum_body_declarations (:seq ";" (:repeat _class_body_declaration))
  enum_constant (:seq
                 (:choice modifiers :blank)
                 (:field :name identifier)
                 (:field :arguments (:choice argument_list :blank))
                 (:field :body (:choice class_body :blank)))
  class_declaration (:seq
                     (:choice modifiers :blank)
                     "class"
                     (:field :name identifier)
                     (:choice (:field :type_parameters type_parameters) :blank)
                     (:choice (:field :superclass superclass) :blank)
                     (:choice (:field :interfaces super_interfaces) :blank)
                     (:choice (:field :permits permits) :blank)
                     (:field :body class_body))
  modifiers (:repeat1
             (:choice
              _annotation
              "public"
              "protected"
              "private"
              "abstract"
              "static"
              "final"
              "strictfp"
              "default"
              "synchronized"
              "native"
              "transient"
              "volatile"
              "sealed"
              "non-sealed"))
  type_parameters (:seq "<" (:seq type_parameter (:repeat (:seq "," type_parameter))) ">")
  type_parameter (:seq
                  (:repeat _annotation)
                  (:alias identifier type_identifier)
                  (:choice type_bound :blank))
  type_bound (:seq "extends" _type (:repeat (:seq "&" _type)))
  superclass (:seq "extends" _type)
  super_interfaces (:seq "implements" type_list)
  type_list (:seq _type (:repeat (:seq "," _type)))
  permits (:seq "permits" type_list)
  class_body (:seq "{" (:repeat _class_body_declaration) "}")
  _class_body_declaration (:choice
                           field_declaration
                           record_declaration
                           method_declaration
                           compact_constructor_declaration
                           class_declaration
                           interface_declaration
                           annotation_type_declaration
                           enum_declaration
                           block
                           static_initializer
                           constructor_declaration
                           ";")
  static_initializer (:seq "static" block)
  constructor_declaration (:seq
                           (:choice modifiers :blank)
                           _constructor_declarator
                           (:choice throws :blank)
                           (:field :body constructor_body))
  _constructor_declarator (:seq
                           (:field :type_parameters (:choice type_parameters :blank))
                           (:field :name identifier)
                           (:field :parameters formal_parameters))
  constructor_body (:seq
                    "{"
                    (:choice explicit_constructor_invocation :blank)
                    (:repeat statement)
                    "}")
  explicit_constructor_invocation (:seq
                                   (:choice
                                    (:seq
                                     (:field :type_arguments (:choice type_arguments :blank))
                                     (:field :constructor (:choice this super)))
                                    (:seq
                                     (:field :object (:choice primary_expression))
                                     "."
                                     (:field :type_arguments (:choice type_arguments :blank))
                                     (:field :constructor super)))
                                   (:field :arguments argument_list)
                                   ";")
  _name (:choice identifier _reserved_identifier scoped_identifier)
  scoped_identifier (:seq (:field :scope _name) "." (:field :name identifier))
  field_declaration (:seq
                     (:choice modifiers :blank)
                     (:field :type _unannotated_type)
                     _variable_declarator_list
                     ";")
  record_declaration (:seq
                      (:choice modifiers :blank)
                      "record"
                      (:field :name identifier)
                      (:choice (:field :type_parameters type_parameters) :blank)
                      (:field :parameters formal_parameters)
                      (:choice (:field :interfaces super_interfaces) :blank)
                      (:field :body class_body))
  annotation_type_declaration (:seq
                               (:choice modifiers :blank)
                               "@interface"
                               (:field :name identifier)
                               (:field :body annotation_type_body))
  annotation_type_body (:seq
                        "{"
                        (:repeat
                         (:choice
                          annotation_type_element_declaration
                          constant_declaration
                          class_declaration
                          interface_declaration
                          enum_declaration
                          annotation_type_declaration
                          ";"))
                        "}")
  annotation_type_element_declaration (:seq
                                       (:choice modifiers :blank)
                                       (:field :type _unannotated_type)
                                       (:field :name (:choice identifier _reserved_identifier))
                                       "("
                                       ")"
                                       (:field :dimensions (:choice dimensions :blank))
                                       (:choice _default_value :blank)
                                       ";")
  _default_value (:seq "default" (:field :value _element_value))
  interface_declaration (:seq
                         (:choice modifiers :blank)
                         "interface"
                         (:field :name identifier)
                         (:field :type_parameters (:choice type_parameters :blank))
                         (:choice extends_interfaces :blank)
                         (:choice (:field :permits permits) :blank)
                         (:field :body interface_body))
  extends_interfaces (:seq "extends" type_list)
  interface_body (:seq
                  "{"
                  (:repeat
                   (:choice
                    constant_declaration
                    enum_declaration
                    method_declaration
                    class_declaration
                    interface_declaration
                    record_declaration
                    annotation_type_declaration
                    ";"))
                  "}")
  constant_declaration (:seq
                        (:choice modifiers :blank)
                        (:field :type _unannotated_type)
                        _variable_declarator_list
                        ";")
  _variable_declarator_list (:seq
                             (:field :declarator variable_declarator)
                             (:repeat (:seq "," (:field :declarator variable_declarator))))
  variable_declarator (:seq
                       _variable_declarator_id
                       (:choice (:seq "=" (:field :value _variable_initializer)) :blank))
  _variable_declarator_id (:seq
                           (:field :name
                            (:choice identifier _reserved_identifier underscore_pattern))
                           (:field :dimensions (:choice dimensions :blank)))
  _variable_initializer (:choice expression array_initializer)
  array_initializer (:seq
                     "{"
                     (:choice
                      (:seq _variable_initializer (:repeat (:seq "," _variable_initializer)))
                      :blank)
                     (:choice "," :blank)
                     "}")
  _type (:choice _unannotated_type annotated_type)
  _unannotated_type (:choice _simple_type array_type)
  _simple_type (:choice
                void_type
                integral_type
                floating_point_type
                boolean_type
                (:alias identifier type_identifier)
                scoped_type_identifier
                generic_type)
  annotated_type (:seq (:repeat1 _annotation) _unannotated_type)
  scoped_type_identifier (:seq
                          (:choice
                           (:alias identifier type_identifier)
                           scoped_type_identifier
                           generic_type)
                          "."
                          (:repeat _annotation)
                          (:alias identifier type_identifier))
  generic_type (:prec-dynamic 10
                (:seq
                 (:choice (:alias identifier type_identifier) scoped_type_identifier)
                 type_arguments))
  array_type (:seq (:field :element _unannotated_type) (:field :dimensions dimensions))
  integral_type (:choice "byte" "short" "int" "long" "char")
  floating_point_type (:choice "float" "double")
  boolean_type "boolean"
  void_type "void"
  _method_header (:seq
                  (:choice
                   (:seq (:field :type_parameters type_parameters) (:repeat _annotation))
                   :blank)
                  (:field :type _unannotated_type)
                  _method_declarator
                  (:choice throws :blank))
  _method_declarator (:seq
                      (:field :name (:choice identifier _reserved_identifier))
                      (:field :parameters formal_parameters)
                      (:field :dimensions (:choice dimensions :blank)))
  formal_parameters (:seq
                     "("
                     (:choice
                      receiver_parameter
                      (:seq
                       (:choice (:seq receiver_parameter ",") :blank)
                       (:choice
                        (:seq
                         (:choice formal_parameter spread_parameter)
                         (:repeat (:seq "," (:choice formal_parameter spread_parameter))))
                        :blank)))
                     ")")
  formal_parameter (:seq
                    (:choice modifiers :blank)
                    (:field :type _unannotated_type)
                    _variable_declarator_id)
  receiver_parameter (:seq
                      (:repeat _annotation)
                      _unannotated_type
                      (:repeat (:seq identifier "."))
                      this)
  spread_parameter (:seq
                    (:choice modifiers :blank)
                    _unannotated_type
                    "..."
                    (:repeat _annotation)
                    variable_declarator)
  throws (:seq "throws" (:seq _type (:repeat (:seq "," _type))))
  local_variable_declaration (:seq
                              (:choice modifiers :blank)
                              (:field :type _unannotated_type)
                              _variable_declarator_list
                              ";")
  method_declaration (:seq
                      (:choice modifiers :blank)
                      _method_header
                      (:choice (:field :body block) ";"))
  compact_constructor_declaration (:seq
                                   (:choice modifiers :blank)
                                   (:field :name identifier)
                                   (:field :body block))
  _reserved_identifier (:choice
                        (:prec -3
                         (:alias (:choice "open" "module" "record" "with" "sealed") identifier))
                        (:alias "yield" identifier))
  this "this"
  super "super"
  identifier (:pattern "[\\p{XID_Start}_$][\\p{XID_Continue}\\u00A2_$]*")
  line_comment (:token (:prec 0 (:seq "//" (:pattern "[^\\n]*"))))
  block_comment (:token (:prec 0 (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))}}
