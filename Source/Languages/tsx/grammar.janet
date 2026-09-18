# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "tsx"
 :word identifier
 :inherits "javascript"
 :extras [comment html_comment (:pattern "[\\s\\p{Zs}\\uFEFF\\u2028\\u2029\\u2060\\u200B]")]
 :conflicts [[primary_expression _property_name]
             [primary_expression _property_name arrow_function]
             [primary_expression arrow_function]
             [primary_expression method_definition]
             [primary_expression rest_pattern]
             [primary_expression pattern]
             [primary_expression _for_header]
             [variable_declarator _for_header]
             [array array_pattern]
             [object object_pattern]
             [assignment_expression pattern]
             [assignment_expression object_assignment_pattern]
             [labeled_statement _property_name]
             [computed_property_name array]
             [binary_expression _initializer]
             [class_static_block _property_name]
             [call_expression instantiation_expression binary_expression]
             [call_expression instantiation_expression binary_expression unary_expression]
             [call_expression instantiation_expression binary_expression update_expression]
             [call_expression instantiation_expression binary_expression await_expression]
             [class]
             [nested_identifier nested_type_identifier primary_expression]
             [nested_identifier nested_type_identifier]
             [_call_signature function_type]
             [_call_signature constructor_type]
             [primary_expression _parameter_name]
             [primary_expression _parameter_name primary_type]
             [primary_expression literal_type]
             [primary_expression literal_type rest_pattern]
             [primary_expression predefined_type rest_pattern]
             [primary_expression primary_type]
             [primary_expression generic_type]
             [primary_expression predefined_type]
             [primary_expression pattern primary_type]
             [_parameter_name primary_type]
             [pattern primary_type]
             [optional_tuple_parameter primary_type]
             [rest_pattern primary_type primary_expression]
             [object object_type]
             [object object_pattern object_type]
             [object object_pattern _property_name]
             [object_pattern object_type]
             [object_pattern object_type]
             [array tuple_type]
             [array array_pattern tuple_type]
             [array_pattern tuple_type]
             [template_literal_type template_string]
             [jsx_opening_element type_parameter]
             [jsx_namespace_name primary_type]]
 :precedences [["member"
                "template_call"
                "call"
                update_expression
                "unary_void"
                "binary_exp"
                "binary_times"
                "binary_plus"
                "binary_shift"
                "binary_compare"
                "binary_relation"
                "binary_equality"
                "bitwise_and"
                "bitwise_xor"
                "bitwise_or"
                "logical_and"
                "logical_or"
                "ternary"
                sequence_expression
                arrow_function]
               ["assign" primary_expression]
               ["member" "template_call" "new" "call" expression]
               ["declaration" "literal"]
               [primary_expression statement_block "object"]
               [meta_property import]
               [import_statement import]
               [export_statement primary_expression]
               [lexical_declaration primary_expression]
               ["call" "instantiation" "unary" "binary" await_expression arrow_function]
               ["extends" "instantiation"]
               [intersection_type
                union_type
                conditional_type
                function_type
                "binary"
                type_predicate
                readonly_type]
               [mapped_type_clause primary_expression]
               [accessibility_modifier primary_expression]
               ["unary_void" expression]
               [extends_clause primary_expression]
               ["unary" "assign"]
               ["declaration" expression]
               [predefined_type unary_expression]
               [type flow_maybe_type]
               [tuple_type array_type pattern type]
               [readonly_type pattern]
               [readonly_type primary_expression]
               [type_query subscript_expression expression]
               [type_query _type_query_subscript_expression]
               [nested_type_identifier generic_type primary_type lookup_type index_type_query type]
               [as_expression satisfies_expression primary_type]
               [_type_query_member_expression member_expression]
               [member_expression _type_query_member_expression_in_type_annotation]
               [_type_query_member_expression primary_expression]
               [_type_query_subscript_expression subscript_expression]
               [_type_query_subscript_expression primary_expression]
               [_type_query_call_expression primary_expression]
               [_type_query_instantiation_expression primary_expression]
               [type_query primary_expression]
               [override_modifier primary_expression]
               [decorator_call_expression decorator]
               [literal_type pattern]
               [predefined_type pattern]
               [call_expression _type_query_call_expression]
               [call_expression _type_query_call_expression_in_type_annotation]
               [new_expression primary_expression]
               [meta_property primary_expression]
               [construct_signature _property_name]]
 :externals [_automatic_semicolon
             _template_chars
             _ternary_qmark
             html_comment
             "||"
             escape_sequence
             regex_pattern
             jsx_text
             _function_signature_automatic_semicolon
             __error_recovery]
 :inline [_expressions
          _semicolon
          _identifier
          _reserved_identifier
          _jsx_attribute
          _jsx_element_name
          _jsx_child
          _jsx_element
          _jsx_attribute_name
          _jsx_attribute_value
          _jsx_identifier
          _lhs_expression
          _type_identifier
          _jsx_start_opening_element]
 :supertypes [statement declaration expression primary_expression pattern type primary_type]
 :rules
 {program (:seq (:choice hash_bang_line :blank) (:repeat statement))
  hash_bang_line (:pattern "#!.*")
  export_statement (:choice
                    (:choice
                     (:seq
                      "export"
                      (:choice
                       (:seq "*" _from_clause)
                       (:seq namespace_export _from_clause)
                       (:seq export_clause _from_clause)
                       export_clause)
                      _semicolon)
                     (:seq
                      (:repeat (:field :decorator decorator))
                      "export"
                      (:choice
                       (:field :declaration declaration)
                       (:seq
                        "default"
                        (:choice
                         (:field :declaration declaration)
                         (:seq (:field :value expression) _semicolon))))))
                    (:seq "export" "type" export_clause (:choice _from_clause :blank) _semicolon)
                    (:seq "export" "=" expression _semicolon)
                    (:seq "export" "as" "namespace" identifier _semicolon))
  namespace_export (:seq "*" "as" _module_export_name)
  export_clause (:seq
                 "{"
                 (:choice (:seq export_specifier (:repeat (:seq "," export_specifier))) :blank)
                 (:choice "," :blank)
                 "}")
  export_specifier (:seq
                    (:choice (:choice "type" "typeof") :blank)
                    (:seq
                     (:field :name _module_export_name)
                     (:choice (:seq "as" (:field :alias _module_export_name)) :blank)))
  _module_export_name (:choice identifier string)
  declaration (:choice
               (:choice
                function_declaration
                generator_function_declaration
                class_declaration
                lexical_declaration
                variable_declaration)
               function_signature
               abstract_class_declaration
               module
               (:prec "declaration" internal_module)
               type_alias_declaration
               enum_declaration
               interface_declaration
               import_alias
               ambient_declaration)
  import (:token "import")
  import_statement (:seq
                    "import"
                    (:choice (:choice "type" "typeof") :blank)
                    (:choice
                     (:seq import_clause _from_clause)
                     import_require_clause
                     (:field :source string))
                    (:choice import_attribute :blank)
                    _semicolon)
  import_clause (:choice
                 namespace_import
                 named_imports
                 (:seq
                  _import_identifier
                  (:choice (:seq "," (:choice namespace_import named_imports)) :blank)))
  _from_clause (:seq "from" (:field :source string))
  namespace_import (:seq "*" "as" identifier)
  named_imports (:seq
                 "{"
                 (:choice (:seq import_specifier (:repeat (:seq "," import_specifier))) :blank)
                 (:choice "," :blank)
                 "}")
  import_specifier (:seq
                    (:choice (:choice "type" "typeof") :blank)
                    (:choice
                     (:field :name _import_identifier)
                     (:seq
                      (:field :name (:choice _module_export_name (:alias "type" identifier)))
                      "as"
                      (:field :alias _import_identifier))))
  import_attribute (:seq (:choice "with" "assert") object)
  statement (:choice
             export_statement
             import_statement
             debugger_statement
             expression_statement
             declaration
             statement_block
             if_statement
             switch_statement
             for_statement
             for_in_statement
             while_statement
             do_statement
             try_statement
             with_statement
             break_statement
             continue_statement
             return_statement
             throw_statement
             empty_statement
             labeled_statement)
  expression_statement (:seq _expressions _semicolon)
  variable_declaration (:seq
                        "var"
                        (:seq variable_declarator (:repeat (:seq "," variable_declarator)))
                        _semicolon)
  lexical_declaration (:seq
                       (:field :kind (:choice "let" "const"))
                       (:seq variable_declarator (:repeat (:seq "," variable_declarator)))
                       _semicolon)
  variable_declarator (:choice
                       (:seq
                        (:field :name (:choice identifier _destructuring_pattern))
                        (:field :type (:choice type_annotation :blank))
                        (:choice _initializer :blank))
                       (:prec "declaration"
                        (:seq (:field :name identifier) "!" (:field :type type_annotation))))
  statement_block (:prec-right 0
                   (:seq "{" (:repeat statement) "}" (:choice _automatic_semicolon :blank)))
  else_clause (:seq "else" statement)
  if_statement (:prec-right 0
                (:seq
                 "if"
                 (:field :condition parenthesized_expression)
                 (:field :consequence statement)
                 (:choice (:field :alternative else_clause) :blank)))
  switch_statement (:seq
                    "switch"
                    (:field :value parenthesized_expression)
                    (:field :body switch_body))
  for_statement (:seq
                 "for"
                 "("
                 (:choice
                  (:field :initializer (:choice lexical_declaration variable_declaration))
                  (:seq (:field :initializer _expressions) ";")
                  (:field :initializer empty_statement))
                 (:field :condition (:choice (:seq _expressions ";") empty_statement))
                 (:field :increment (:choice _expressions :blank))
                 ")"
                 (:field :body statement))
  for_in_statement (:seq "for" (:choice "await" :blank) _for_header (:field :body statement))
  _for_header (:seq
               "("
               (:choice
                (:field :left (:choice _lhs_expression parenthesized_expression))
                (:seq
                 (:field :kind "var")
                 (:field :left (:choice identifier _destructuring_pattern))
                 (:choice _initializer :blank))
                (:seq
                 (:field :kind (:choice "let" "const"))
                 (:field :left (:choice identifier _destructuring_pattern))
                 (:choice _automatic_semicolon :blank)))
               (:field :operator (:choice "in" "of"))
               (:field :right _expressions)
               ")")
  while_statement (:seq
                   "while"
                   (:field :condition parenthesized_expression)
                   (:field :body statement))
  do_statement (:prec-right 0
                (:seq
                 "do"
                 (:field :body statement)
                 "while"
                 (:field :condition parenthesized_expression)
                 (:choice _semicolon :blank)))
  try_statement (:seq
                 "try"
                 (:field :body statement_block)
                 (:choice (:field :handler catch_clause) :blank)
                 (:choice (:field :finalizer finally_clause) :blank))
  with_statement (:seq "with" (:field :object parenthesized_expression) (:field :body statement))
  break_statement (:seq
                   "break"
                   (:field :label (:choice (:alias identifier statement_identifier) :blank))
                   _semicolon)
  continue_statement (:seq
                      "continue"
                      (:field :label (:choice (:alias identifier statement_identifier) :blank))
                      _semicolon)
  debugger_statement (:seq "debugger" _semicolon)
  return_statement (:seq "return" (:choice _expressions :blank) _semicolon)
  throw_statement (:seq "throw" _expressions _semicolon)
  empty_statement ";"
  labeled_statement (:prec-dynamic -1
                     (:seq
                      (:field :label
                       (:alias (:choice identifier _reserved_identifier) statement_identifier))
                      ":"
                      (:field :body statement)))
  switch_body (:seq "{" (:repeat (:choice switch_case switch_default)) "}")
  switch_case (:seq "case" (:field :value _expressions) ":" (:field :body (:repeat statement)))
  switch_default (:seq "default" ":" (:field :body (:repeat statement)))
  catch_clause (:seq
                "catch"
                (:choice
                 (:seq
                  "("
                  (:field :parameter (:choice identifier _destructuring_pattern))
                  (:choice (:field :type type_annotation) :blank)
                  ")")
                 :blank)
                (:field :body statement_block))
  finally_clause (:seq "finally" (:field :body statement_block))
  parenthesized_expression (:seq
                            "("
                            (:choice
                             (:seq expression (:field :type (:choice type_annotation :blank)))
                             sequence_expression)
                            ")")
  _expressions (:choice expression sequence_expression)
  expression (:choice
              as_expression
              satisfies_expression
              instantiation_expression
              internal_module
              primary_expression
              _jsx_element
              assignment_expression
              augmented_assignment_expression
              await_expression
              unary_expression
              binary_expression
              ternary_expression
              update_expression
              new_expression
              yield_expression)
  primary_expression (:choice
                      (:choice
                       subscript_expression
                       member_expression
                       parenthesized_expression
                       _identifier
                       (:alias _reserved_identifier identifier)
                       this
                       super
                       number
                       string
                       template_string
                       regex
                       (:ref "true")
                       (:ref "false")
                       null
                       object
                       array
                       function_expression
                       arrow_function
                       generator_function
                       class
                       meta_property
                       call_expression)
                      non_null_expression)
  yield_expression (:prec-right 0
                    (:seq "yield" (:choice (:seq "*" expression) (:choice expression :blank))))
  object (:prec "object"
          (:seq
           "{"
           (:choice
            (:seq
             (:choice
              (:choice
               pair
               spread_element
               method_definition
               (:alias (:choice identifier _reserved_identifier) shorthand_property_identifier))
              :blank)
             (:repeat
              (:seq
               ","
               (:choice
                (:choice
                 pair
                 spread_element
                 method_definition
                 (:alias (:choice identifier _reserved_identifier) shorthand_property_identifier))
                :blank))))
            :blank)
           "}"))
  object_pattern (:prec "object"
                  (:seq
                   "{"
                   (:choice
                    (:seq
                     (:choice
                      (:choice
                       pair_pattern
                       rest_pattern
                       object_assignment_pattern
                       (:alias
                        (:choice identifier _reserved_identifier)
                        shorthand_property_identifier_pattern))
                      :blank)
                     (:repeat
                      (:seq
                       ","
                       (:choice
                        (:choice
                         pair_pattern
                         rest_pattern
                         object_assignment_pattern
                         (:alias
                          (:choice identifier _reserved_identifier)
                          shorthand_property_identifier_pattern))
                        :blank))))
                    :blank)
                   "}"))
  assignment_pattern (:seq (:field :left pattern) "=" (:field :right expression))
  object_assignment_pattern (:seq
                             (:field :left
                              (:choice
                               (:alias
                                (:choice _reserved_identifier identifier)
                                shorthand_property_identifier_pattern)
                               _destructuring_pattern))
                             "="
                             (:field :right expression))
  array (:seq
         "["
         (:choice
          (:seq
           (:choice (:choice expression spread_element) :blank)
           (:repeat (:seq "," (:choice (:choice expression spread_element) :blank))))
          :blank)
         "]")
  array_pattern (:seq
                 "["
                 (:choice
                  (:seq
                   (:choice (:choice pattern assignment_pattern) :blank)
                   (:repeat (:seq "," (:choice (:choice pattern assignment_pattern) :blank))))
                  :blank)
                 "]")
  _jsx_element (:choice jsx_element jsx_self_closing_element)
  jsx_element (:seq
               (:field :open_tag jsx_opening_element)
               (:repeat _jsx_child)
               (:field :close_tag jsx_closing_element))
  html_character_reference (:pattern "&(#([xX][0-9a-fA-F]{1,6}|[0-9]{1,5})|[A-Za-z]{1,30});")
  jsx_expression (:seq
                  "{"
                  (:choice (:choice expression sequence_expression spread_element) :blank)
                  "}")
  _jsx_child (:choice jsx_text html_character_reference _jsx_element jsx_expression)
  jsx_opening_element (:prec-dynamic -1 (:seq _jsx_start_opening_element ">"))
  jsx_identifier (:pattern "[a-zA-Z_$][a-zA-Z\\d_$]*-[a-zA-Z\\d_$\\-]*")
  _jsx_identifier (:choice (:alias jsx_identifier identifier) identifier)
  nested_identifier (:prec "member"
                     (:seq
                      (:field :object
                       (:choice identifier (:alias nested_identifier member_expression)))
                      "."
                      (:field :property (:alias identifier property_identifier))))
  jsx_namespace_name (:seq _jsx_identifier ":" _jsx_identifier)
  _jsx_element_name (:choice
                     _jsx_identifier
                     (:alias nested_identifier member_expression)
                     jsx_namespace_name)
  jsx_closing_element (:seq "</" (:choice (:field :name _jsx_element_name) :blank) ">")
  jsx_self_closing_element (:prec-dynamic -1 (:seq _jsx_start_opening_element "/>"))
  _jsx_attribute (:choice jsx_attribute jsx_expression)
  _jsx_attribute_name (:choice (:alias _jsx_identifier property_identifier) jsx_namespace_name)
  jsx_attribute (:seq _jsx_attribute_name (:choice (:seq "=" _jsx_attribute_value) :blank))
  _jsx_string (:choice
               (:seq
                "\""
                (:repeat
                 (:choice
                  (:alias unescaped_double_jsx_string_fragment string_fragment)
                  html_character_reference))
                "\"")
               (:seq
                "'"
                (:repeat
                 (:choice
                  (:alias unescaped_single_jsx_string_fragment string_fragment)
                  html_character_reference))
                "'"))
  unescaped_double_jsx_string_fragment (:token-immediate
                                        (:prec 1 (:pattern "([^\"&]|&[^#A-Za-z])+")))
  unescaped_single_jsx_string_fragment (:token-immediate
                                        (:prec 1 (:pattern "([^'&]|&[^#A-Za-z])+")))
  _jsx_attribute_value (:choice (:alias _jsx_string string) jsx_expression _jsx_element)
  class (:prec "literal"
         (:seq
          (:repeat (:field :decorator decorator))
          "class"
          (:field :name (:choice _type_identifier :blank))
          (:field :type_parameters (:choice type_parameters :blank))
          (:choice class_heritage :blank)
          (:field :body class_body)))
  class_declaration (:prec-left "declaration"
                     (:seq
                      (:repeat (:field :decorator decorator))
                      "class"
                      (:field :name _type_identifier)
                      (:field :type_parameters (:choice type_parameters :blank))
                      (:choice class_heritage :blank)
                      (:field :body class_body)
                      (:choice _automatic_semicolon :blank)))
  class_heritage (:choice
                  (:seq extends_clause (:choice implements_clause :blank))
                  implements_clause)
  function_expression (:prec "literal"
                       (:seq
                        (:choice "async" :blank)
                        "function"
                        (:field :name (:choice identifier :blank))
                        _call_signature
                        (:field :body statement_block)))
  function_declaration (:prec-right "declaration"
                        (:seq
                         (:choice "async" :blank)
                         "function"
                         (:field :name identifier)
                         _call_signature
                         (:field :body statement_block)
                         (:choice _automatic_semicolon :blank)))
  generator_function (:prec "literal"
                      (:seq
                       (:choice "async" :blank)
                       "function"
                       "*"
                       (:field :name (:choice identifier :blank))
                       _call_signature
                       (:field :body statement_block)))
  generator_function_declaration (:prec-right "declaration"
                                  (:seq
                                   (:choice "async" :blank)
                                   "function"
                                   "*"
                                   (:field :name identifier)
                                   _call_signature
                                   (:field :body statement_block)
                                   (:choice _automatic_semicolon :blank)))
  arrow_function (:seq
                  (:choice "async" :blank)
                  (:choice
                   (:field :parameter (:choice (:alias _reserved_identifier identifier) identifier))
                   _call_signature)
                  "=>"
                  (:field :body (:choice expression statement_block)))
  _call_signature (:seq
                   (:field :type_parameters (:choice type_parameters :blank))
                   (:field :parameters formal_parameters)
                   (:field :return_type
                    (:choice
                     (:choice type_annotation asserts_annotation type_predicate_annotation)
                     :blank)))
  _formal_parameter (:choice required_parameter optional_parameter)
  optional_chain "?."
  call_expression (:choice
                   (:prec "call"
                    (:seq
                     (:field :function (:choice expression import))
                     (:field :type_arguments (:choice type_arguments :blank))
                     (:field :arguments arguments)))
                   (:prec "template_call"
                    (:seq
                     (:field :function (:choice primary_expression new_expression))
                     (:field :arguments template_string)))
                   (:prec "member"
                    (:seq
                     (:field :function primary_expression)
                     "?."
                     (:field :type_arguments (:choice type_arguments :blank))
                     (:field :arguments arguments))))
  new_expression (:prec-right "new"
                  (:seq
                   "new"
                   (:field :constructor primary_expression)
                   (:field :type_arguments (:choice type_arguments :blank))
                   (:field :arguments (:choice arguments :blank))))
  await_expression (:prec "unary_void" (:seq "await" expression))
  member_expression (:prec "member"
                     (:seq
                      (:field :object (:choice expression primary_expression import))
                      (:choice "." (:field :optional_chain optional_chain))
                      (:field :property
                       (:choice private_property_identifier (:alias identifier property_identifier)))))
  subscript_expression (:prec-right "member"
                        (:seq
                         (:field :object (:choice expression primary_expression))
                         (:choice (:field :optional_chain optional_chain) :blank)
                         "["
                         (:field :index _expressions)
                         "]"))
  _lhs_expression (:choice
                   (:choice
                    member_expression
                    subscript_expression
                    _identifier
                    (:alias _reserved_identifier identifier)
                    _destructuring_pattern)
                   non_null_expression)
  assignment_expression (:prec-right "assign"
                         (:seq
                          (:choice "using" :blank)
                          (:field :left (:choice parenthesized_expression _lhs_expression))
                          "="
                          (:field :right expression)))
  _augmented_assignment_lhs (:choice
                             (:choice
                              member_expression
                              subscript_expression
                              (:alias _reserved_identifier identifier)
                              identifier
                              parenthesized_expression)
                             non_null_expression)
  augmented_assignment_expression (:prec-right "assign"
                                   (:seq
                                    (:field :left _augmented_assignment_lhs)
                                    (:field :operator
                                     (:choice
                                      "+="
                                      "-="
                                      "*="
                                      "/="
                                      "%="
                                      "^="
                                      "&="
                                      "|="
                                      ">>="
                                      ">>>="
                                      "<<="
                                      "**="
                                      "&&="
                                      "||="
                                      "??="))
                                    (:field :right expression)))
  _initializer (:seq "=" (:field :value expression))
  _destructuring_pattern (:choice object_pattern array_pattern)
  spread_element (:seq "..." expression)
  ternary_expression (:prec-right "ternary"
                      (:seq
                       (:field :condition expression)
                       (:alias _ternary_qmark "?")
                       (:field :consequence expression)
                       ":"
                       (:field :alternative expression)))
  binary_expression (:choice
                     (:prec-left "logical_and"
                      (:seq
                       (:field :left expression)
                       (:field :operator "&&")
                       (:field :right expression)))
                     (:prec-left "logical_or"
                      (:seq
                       (:field :left expression)
                       (:field :operator "||")
                       (:field :right expression)))
                     (:prec-left "binary_shift"
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>")
                       (:field :right expression)))
                     (:prec-left "binary_shift"
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>>")
                       (:field :right expression)))
                     (:prec-left "binary_shift"
                      (:seq
                       (:field :left expression)
                       (:field :operator "<<")
                       (:field :right expression)))
                     (:prec-left "bitwise_and"
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left "bitwise_xor"
                      (:seq
                       (:field :left expression)
                       (:field :operator "^")
                       (:field :right expression)))
                     (:prec-left "bitwise_or"
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
                       (:field :right expression)))
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left expression)
                       (:field :operator "+")
                       (:field :right expression)))
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left expression)
                       (:field :operator "-")
                       (:field :right expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left expression)
                       (:field :operator "*")
                       (:field :right expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left expression)
                       (:field :operator "/")
                       (:field :right expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
                       (:field :right expression)))
                     (:prec-right "binary_exp"
                      (:seq
                       (:field :left expression)
                       (:field :operator "**")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left expression)
                       (:field :operator "<")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left expression)
                       (:field :operator "<=")
                       (:field :right expression)))
                     (:prec-left "binary_equality"
                      (:seq
                       (:field :left expression)
                       (:field :operator "==")
                       (:field :right expression)))
                     (:prec-left "binary_equality"
                      (:seq
                       (:field :left expression)
                       (:field :operator "===")
                       (:field :right expression)))
                     (:prec-left "binary_equality"
                      (:seq
                       (:field :left expression)
                       (:field :operator "!=")
                       (:field :right expression)))
                     (:prec-left "binary_equality"
                      (:seq
                       (:field :left expression)
                       (:field :operator "!==")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left expression)
                       (:field :operator ">=")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left expression)
                       (:field :operator ">")
                       (:field :right expression)))
                     (:prec-left "ternary"
                      (:seq
                       (:field :left expression)
                       (:field :operator "??")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left expression)
                       (:field :operator "instanceof")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left (:choice expression private_property_identifier))
                       (:field :operator "in")
                       (:field :right expression))))
  unary_expression (:prec-left "unary_void"
                    (:seq
                     (:field :operator (:choice "!" "~" "-" "+" "typeof" "void" "delete"))
                     (:field :argument expression)))
  update_expression (:prec-left 0
                     (:choice
                      (:seq (:field :argument expression) (:field :operator (:choice "++" "--")))
                      (:seq (:field :operator (:choice "++" "--")) (:field :argument expression))))
  sequence_expression (:prec-right 0 (:seq expression (:repeat (:seq "," expression))))
  string (:choice
          (:seq
           "\""
           (:repeat
            (:choice (:alias unescaped_double_string_fragment string_fragment) escape_sequence))
           "\"")
          (:seq
           "'"
           (:repeat
            (:choice (:alias unescaped_single_string_fragment string_fragment) escape_sequence))
           "'"))
  unescaped_double_string_fragment (:token-immediate (:prec 1 (:pattern "[^\"\\\\\\r\\n]+")))
  unescaped_single_string_fragment (:token-immediate (:prec 1 (:pattern "[^'\\\\\\r\\n]+")))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu0-7]")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9a-fA-F]{2}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}")
                     (:pattern "[\\r?][\\n\\u2028\\u2029]"))))
  comment (:token
           (:choice
            (:seq "//" (:pattern "[^\\r\\n\\u2028\\u2029]*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))
  template_string (:seq
                   "`"
                   (:repeat
                    (:choice
                     (:alias _template_chars string_fragment)
                     escape_sequence
                     template_substitution))
                   "`")
  template_substitution (:seq "${" _expressions "}")
  regex (:seq
         "/"
         (:field :pattern regex_pattern)
         (:token-immediate (:prec 1 "/"))
         (:choice (:field :flags regex_flags) :blank))
  regex_pattern (:token-immediate
                 (:prec -1
                  (:repeat1
                   (:choice
                    (:seq
                     "["
                     (:repeat (:choice (:seq "\\" (:pattern ".")) (:pattern "[^\\]\\n\\\\]")))
                     "]")
                    (:seq "\\" (:pattern "."))
                    (:pattern "[^/\\\\\\[\\n]")))))
  regex_flags (:token-immediate (:pattern "[a-z]+"))
  number (:token
          (:choice
           (:seq (:choice "0x" "0X") (:pattern "[\\da-fA-F](_?[\\da-fA-F])*"))
           (:choice
            (:seq
             (:choice
              "0"
              (:seq
               (:choice "0" :blank)
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank)))
             "."
             (:choice (:pattern "\\d(_?\\d)*") :blank)
             (:choice
              (:seq
               (:choice "e" "E")
               (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*")))
              :blank))
            (:seq
             "."
             (:pattern "\\d(_?\\d)*")
             (:choice
              (:seq
               (:choice "e" "E")
               (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*")))
              :blank))
            (:seq
             (:choice
              "0"
              (:seq
               (:choice "0" :blank)
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank)))
             (:seq
              (:choice "e" "E")
              (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*"))))
            (:pattern "\\d(_?\\d)*"))
           (:seq (:choice "0b" "0B") (:pattern "[0-1](_?[0-1])*"))
           (:seq (:choice "0o" "0O") (:pattern "[0-7](_?[0-7])*"))
           (:seq
            (:choice
             (:seq (:choice "0x" "0X") (:pattern "[\\da-fA-F](_?[\\da-fA-F])*"))
             (:seq (:choice "0b" "0B") (:pattern "[0-1](_?[0-1])*"))
             (:seq (:choice "0o" "0O") (:pattern "[0-7](_?[0-7])*"))
             (:pattern "\\d(_?\\d)*"))
            "n")))
  _identifier (:choice undefined identifier)
  identifier (:token
              (:seq
               (:pattern "[^\\x00-\\x1F\\s\\p{Zs}0-9:;`\"'@#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}\\uFEFF\\u2060\\u200B\\u2028\\u2029]|\\\\u[0-9a-fA-F]{4}|\\\\u\\{[0-9a-fA-F]+\\}")
               (:repeat
                (:pattern "[^\\x00-\\x1F\\s\\p{Zs}:;`\"'@#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}\\uFEFF\\u2060\\u200B\\u2028\\u2029]|\\\\u[0-9a-fA-F]{4}|\\\\u\\{[0-9a-fA-F]+\\}"))))
  private_property_identifier (:token
                               (:seq
                                "#"
                                (:pattern "[^\\x00-\\x1F\\s\\p{Zs}0-9:;`\"'@#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}\\uFEFF\\u2060\\u200B\\u2028\\u2029]|\\\\u[0-9a-fA-F]{4}|\\\\u\\{[0-9a-fA-F]+\\}")
                                (:repeat
                                 (:pattern "[^\\x00-\\x1F\\s\\p{Zs}:;`\"'@#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}\\uFEFF\\u2060\\u200B\\u2028\\u2029]|\\\\u[0-9a-fA-F]{4}|\\\\u\\{[0-9a-fA-F]+\\}"))))
  meta_property (:choice (:seq "new" "." "target") (:seq "import" "." "meta"))
  this "this"
  super "super"
  (:ref "true") "true"
  (:ref "false") "false"
  null "null"
  undefined "undefined"
  arguments (:seq
             "("
             (:choice
              (:seq
               (:choice (:choice expression spread_element) :blank)
               (:repeat (:seq "," (:choice (:choice expression spread_element) :blank))))
              :blank)
             ")")
  decorator (:seq
             "@"
             (:choice
              identifier
              (:alias decorator_member_expression member_expression)
              (:alias decorator_call_expression call_expression)
              (:alias decorator_parenthesized_expression parenthesized_expression)))
  decorator_member_expression (:prec "member"
                               (:seq
                                (:field :object
                                 (:choice
                                  identifier
                                  (:alias decorator_member_expression member_expression)))
                                "."
                                (:field :property (:alias identifier property_identifier))))
  decorator_call_expression (:prec "call"
                             (:seq
                              (:field :function
                               (:choice
                                identifier
                                (:alias decorator_member_expression member_expression)))
                              (:choice (:field :type_arguments type_arguments) :blank)
                              (:field :arguments arguments)))
  class_body (:seq
              "{"
              (:repeat
               (:choice
                (:seq
                 (:repeat (:field :decorator decorator))
                 method_definition
                 (:choice _semicolon :blank))
                (:seq method_signature (:choice _function_signature_automatic_semicolon ","))
                class_static_block
                (:seq
                 (:choice
                  abstract_method_signature
                  index_signature
                  method_signature
                  public_field_definition)
                 (:choice _semicolon ","))
                ";"))
              "}")
  field_definition (:seq
                    (:repeat (:field :decorator decorator))
                    (:choice "static" :blank)
                    (:field :property _property_name)
                    (:choice _initializer :blank))
  formal_parameters (:seq
                     "("
                     (:choice
                      (:seq
                       (:seq _formal_parameter (:repeat (:seq "," _formal_parameter)))
                       (:choice "," :blank))
                      :blank)
                     ")")
  class_static_block (:seq
                      "static"
                      (:choice _automatic_semicolon :blank)
                      (:field :body statement_block))
  pattern (:prec-dynamic -1 (:choice _lhs_expression rest_pattern))
  rest_pattern (:prec-right 0 (:seq "..." _lhs_expression))
  method_definition (:prec-left 0
                     (:seq
                      (:choice accessibility_modifier :blank)
                      (:choice "static" :blank)
                      (:choice override_modifier :blank)
                      (:choice "readonly" :blank)
                      (:choice "async" :blank)
                      (:choice (:choice "get" "set" "*") :blank)
                      (:field :name _property_name)
                      (:choice "?" :blank)
                      _call_signature
                      (:field :body statement_block)))
  pair (:seq (:field :key _property_name) ":" (:field :value expression))
  pair_pattern (:seq
                (:field :key _property_name)
                ":"
                (:field :value (:choice pattern assignment_pattern)))
  _property_name (:choice
                  (:alias (:choice identifier _reserved_identifier) property_identifier)
                  private_property_identifier
                  string
                  number
                  computed_property_name)
  computed_property_name (:seq "[" expression "]")
  _reserved_identifier (:choice
                        "declare"
                        "namespace"
                        "type"
                        "public"
                        "private"
                        "protected"
                        "override"
                        "readonly"
                        "module"
                        "any"
                        "number"
                        "boolean"
                        "string"
                        "symbol"
                        "export"
                        "object"
                        "new"
                        "readonly"
                        (:choice "get" "set" "async" "static" "export" "let"))
  _semicolon (:choice _automatic_semicolon ";")
  public_field_definition (:seq
                           (:repeat (:field :decorator decorator))
                           (:choice
                            (:choice
                             (:seq "declare" (:choice accessibility_modifier :blank))
                             (:seq accessibility_modifier (:choice "declare" :blank)))
                            :blank)
                           (:choice
                            (:seq
                             (:choice "static" :blank)
                             (:choice override_modifier :blank)
                             (:choice "readonly" :blank))
                            (:seq (:choice "abstract" :blank) (:choice "readonly" :blank))
                            (:seq (:choice "readonly" :blank) (:choice "abstract" :blank))
                            (:choice "accessor" :blank))
                           (:field :name _property_name)
                           (:choice (:choice "?" "!") :blank)
                           (:field :type (:choice type_annotation :blank))
                           (:choice _initializer :blank))
  _jsx_start_opening_element (:seq
                              "<"
                              (:choice
                               (:seq
                                (:choice
                                 (:field :name (:choice _jsx_identifier jsx_namespace_name))
                                 (:seq
                                  (:field :name
                                   (:choice identifier (:alias nested_identifier member_expression)))
                                  (:field :type_arguments (:choice type_arguments :blank))))
                                (:repeat (:field :attribute _jsx_attribute)))
                               :blank))
  _import_identifier (:choice identifier (:alias "type" identifier))
  non_null_expression (:prec-left "unary" (:seq expression "!"))
  method_signature (:seq
                    (:choice accessibility_modifier :blank)
                    (:choice "static" :blank)
                    (:choice override_modifier :blank)
                    (:choice "readonly" :blank)
                    (:choice "async" :blank)
                    (:choice (:choice "get" "set" "*") :blank)
                    (:field :name _property_name)
                    (:choice "?" :blank)
                    _call_signature)
  abstract_method_signature (:seq
                             (:choice accessibility_modifier :blank)
                             "abstract"
                             (:choice override_modifier :blank)
                             (:choice (:choice "get" "set" "*") :blank)
                             (:field :name _property_name)
                             (:choice "?" :blank)
                             _call_signature)
  function_signature (:seq
                      (:choice "async" :blank)
                      "function"
                      (:field :name identifier)
                      _call_signature
                      (:choice _semicolon _function_signature_automatic_semicolon))
  decorator_parenthesized_expression (:seq
                                      "("
                                      (:choice
                                       identifier
                                       (:alias decorator_member_expression member_expression)
                                       (:alias decorator_call_expression call_expression))
                                      ")")
  type_assertion (:prec-left "unary" (:seq type_arguments expression))
  as_expression (:prec-left "binary" (:seq expression "as" (:choice "const" type)))
  satisfies_expression (:prec-left "binary" (:seq expression "satisfies" type))
  instantiation_expression (:prec "instantiation"
                            (:seq expression (:field :type_arguments type_arguments)))
  import_require_clause (:seq identifier "=" "require" "(" (:field :source string) ")")
  extends_clause (:seq
                  "extends"
                  (:seq _extends_clause_single (:repeat (:seq "," _extends_clause_single))))
  _extends_clause_single (:prec "extends"
                          (:seq
                           (:field :value expression)
                           (:field :type_arguments (:choice type_arguments :blank))))
  implements_clause (:seq "implements" (:seq type (:repeat (:seq "," type))))
  ambient_declaration (:seq
                       "declare"
                       (:choice
                        declaration
                        (:seq "global" statement_block)
                        (:seq
                         "module"
                         "."
                         (:alias identifier property_identifier)
                         ":"
                         type
                         _semicolon)))
  abstract_class_declaration (:prec "declaration"
                              (:seq
                               (:repeat (:field :decorator decorator))
                               "abstract"
                               "class"
                               (:field :name _type_identifier)
                               (:field :type_parameters (:choice type_parameters :blank))
                               (:choice class_heritage :blank)
                               (:field :body class_body)))
  module (:seq "module" _module)
  internal_module (:seq "namespace" _module)
  _module (:prec-right 0
           (:seq
            (:field :name (:choice string identifier nested_identifier))
            (:field :body (:choice statement_block :blank))))
  import_alias (:seq "import" identifier "=" (:choice identifier nested_identifier) _semicolon)
  nested_type_identifier (:prec "member"
                          (:seq
                           (:field :module (:choice identifier nested_identifier))
                           "."
                           (:field :name _type_identifier)))
  interface_declaration (:seq
                         "interface"
                         (:field :name _type_identifier)
                         (:field :type_parameters (:choice type_parameters :blank))
                         (:choice extends_type_clause :blank)
                         (:field :body (:alias object_type interface_body)))
  extends_type_clause (:seq
                       "extends"
                       (:seq
                        (:field :type
                         (:choice _type_identifier nested_type_identifier generic_type))
                        (:repeat
                         (:seq
                          ","
                          (:field :type
                           (:choice _type_identifier nested_type_identifier generic_type))))))
  enum_declaration (:seq
                    (:choice "const" :blank)
                    "enum"
                    (:field :name identifier)
                    (:field :body enum_body))
  enum_body (:seq
             "{"
             (:choice
              (:seq
               (:seq
                (:choice (:field :name _property_name) enum_assignment)
                (:repeat (:seq "," (:choice (:field :name _property_name) enum_assignment))))
               (:choice "," :blank))
              :blank)
             "}")
  enum_assignment (:seq (:field :name _property_name) _initializer)
  type_alias_declaration (:seq
                          "type"
                          (:field :name _type_identifier)
                          (:field :type_parameters (:choice type_parameters :blank))
                          "="
                          (:field :value type)
                          _semicolon)
  accessibility_modifier (:choice "public" "private" "protected")
  override_modifier "override"
  required_parameter (:seq
                      _parameter_name
                      (:field :type (:choice type_annotation :blank))
                      (:choice _initializer :blank))
  optional_parameter (:seq
                      _parameter_name
                      "?"
                      (:field :type (:choice type_annotation :blank))
                      (:choice _initializer :blank))
  _parameter_name (:seq
                   (:repeat (:field :decorator decorator))
                   (:choice accessibility_modifier :blank)
                   (:choice override_modifier :blank)
                   (:choice "readonly" :blank)
                   (:field :pattern (:choice pattern this)))
  omitting_type_annotation (:seq "-?:" type)
  adding_type_annotation (:seq "+?:" type)
  opting_type_annotation (:seq "?:" type)
  type_annotation (:seq ":" type)
  _type_query_member_expression_in_type_annotation (:seq
                                                    (:field :object
                                                     (:choice
                                                      import
                                                      (:alias
                                                       _type_query_member_expression_in_type_annotation
                                                       member_expression)
                                                      (:alias
                                                       _type_query_call_expression_in_type_annotation
                                                       call_expression)))
                                                    "."
                                                    (:field :property
                                                     (:choice
                                                      private_property_identifier
                                                      (:alias identifier property_identifier))))
  _type_query_call_expression_in_type_annotation (:seq
                                                  (:field :function
                                                   (:choice
                                                    import
                                                    (:alias
                                                     _type_query_member_expression_in_type_annotation
                                                     member_expression)))
                                                  (:field :arguments arguments))
  asserts (:seq "asserts" (:choice type_predicate identifier this))
  asserts_annotation (:seq (:seq ":" asserts))
  type (:choice
        primary_type
        function_type
        readonly_type
        constructor_type
        infer_type
        (:prec -1 (:alias _type_query_member_expression_in_type_annotation member_expression))
        (:prec -1 (:alias _type_query_call_expression_in_type_annotation call_expression)))
  tuple_parameter (:seq
                   (:field :name (:choice identifier rest_pattern))
                   (:field :type type_annotation))
  optional_tuple_parameter (:seq (:field :name identifier) "?" (:field :type type_annotation))
  optional_type (:seq type "?")
  rest_type (:seq "..." type)
  _tuple_type_member (:choice
                      (:alias tuple_parameter required_parameter)
                      (:alias optional_tuple_parameter optional_parameter)
                      optional_type
                      rest_type
                      type)
  constructor_type (:prec-left 0
                    (:seq
                     (:choice "abstract" :blank)
                     "new"
                     (:field :type_parameters (:choice type_parameters :blank))
                     (:field :parameters formal_parameters)
                     "=>"
                     (:field :type type)))
  primary_type (:choice
                parenthesized_type
                predefined_type
                _type_identifier
                nested_type_identifier
                generic_type
                object_type
                array_type
                tuple_type
                flow_maybe_type
                type_query
                index_type_query
                (:alias this this_type)
                existential_type
                literal_type
                lookup_type
                conditional_type
                template_literal_type
                intersection_type
                union_type
                "const")
  template_type (:seq "${" (:choice primary_type infer_type) "}")
  template_literal_type (:seq
                         "`"
                         (:repeat (:choice (:alias _template_chars string_fragment) template_type))
                         "`")
  infer_type (:prec-right 0 (:seq "infer" _type_identifier (:choice (:seq "extends" type) :blank)))
  conditional_type (:prec-right 0
                    (:seq
                     (:field :left type)
                     "extends"
                     (:field :right type)
                     "?"
                     (:field :consequence type)
                     ":"
                     (:field :alternative type)))
  generic_type (:prec "call"
                (:seq
                 (:field :name (:choice _type_identifier nested_type_identifier))
                 (:field :type_arguments type_arguments)))
  type_predicate (:seq
                  (:field :name (:choice identifier this (:alias predefined_type identifier)))
                  "is"
                  (:field :type type))
  type_predicate_annotation (:seq (:seq ":" type_predicate))
  _type_query_member_expression (:seq
                                 (:field :object
                                  (:choice
                                   identifier
                                   this
                                   (:alias _type_query_subscript_expression subscript_expression)
                                   (:alias _type_query_member_expression member_expression)
                                   (:alias _type_query_call_expression call_expression)))
                                 (:choice "." "?.")
                                 (:field :property
                                  (:choice
                                   private_property_identifier
                                   (:alias identifier property_identifier))))
  _type_query_subscript_expression (:seq
                                    (:field :object
                                     (:choice
                                      identifier
                                      this
                                      (:alias _type_query_subscript_expression subscript_expression)
                                      (:alias _type_query_member_expression member_expression)
                                      (:alias _type_query_call_expression call_expression)))
                                    (:choice "?." :blank)
                                    "["
                                    (:field :index (:choice predefined_type string number))
                                    "]")
  _type_query_call_expression (:seq
                               (:field :function
                                (:choice
                                 import
                                 identifier
                                 (:alias _type_query_member_expression member_expression)
                                 (:alias _type_query_subscript_expression subscript_expression)))
                               (:field :arguments arguments))
  _type_query_instantiation_expression (:seq
                                        (:field :function
                                         (:choice
                                          import
                                          identifier
                                          (:alias _type_query_member_expression member_expression)
                                          (:alias
                                           _type_query_subscript_expression
                                           subscript_expression)))
                                        (:field :type_arguments type_arguments))
  type_query (:prec-right 0
              (:seq
               "typeof"
               (:choice
                (:alias _type_query_subscript_expression subscript_expression)
                (:alias _type_query_member_expression member_expression)
                (:alias _type_query_call_expression call_expression)
                (:alias _type_query_instantiation_expression instantiation_expression)
                identifier
                this)))
  index_type_query (:seq "keyof" primary_type)
  lookup_type (:seq primary_type "[" type "]")
  mapped_type_clause (:seq
                      (:field :name _type_identifier)
                      "in"
                      (:field :type type)
                      (:choice (:seq "as" (:field :alias type)) :blank))
  literal_type (:choice
                (:alias _number unary_expression)
                number
                string
                (:ref "true")
                (:ref "false")
                null
                undefined)
  _number (:prec-left 1 (:seq (:field :operator (:choice "-" "+")) (:field :argument number)))
  existential_type "*"
  flow_maybe_type (:prec-right 0 (:seq "?" primary_type))
  parenthesized_type (:seq "(" type ")")
  predefined_type (:choice
                   "any"
                   "number"
                   "boolean"
                   "string"
                   "symbol"
                   (:alias (:seq "unique" "symbol") "unique symbol")
                   "void"
                   "unknown"
                   "string"
                   "never"
                   "object")
  type_arguments (:seq "<" (:seq type (:repeat (:seq "," type))) (:choice "," :blank) ">")
  object_type (:seq
               (:choice "{" "{|")
               (:choice
                (:seq
                 (:choice (:choice "," ";") :blank)
                 (:seq
                  (:choice
                   export_statement
                   property_signature
                   call_signature
                   construct_signature
                   index_signature
                   method_signature)
                  (:repeat
                   (:seq
                    (:choice "," _semicolon)
                    (:choice
                     export_statement
                     property_signature
                     call_signature
                     construct_signature
                     index_signature
                     method_signature))))
                 (:choice (:choice "," _semicolon) :blank))
                :blank)
               (:choice "}" "|}"))
  call_signature _call_signature
  property_signature (:seq
                      (:choice accessibility_modifier :blank)
                      (:choice "static" :blank)
                      (:choice override_modifier :blank)
                      (:choice "readonly" :blank)
                      (:field :name _property_name)
                      (:choice "?" :blank)
                      (:field :type (:choice type_annotation :blank)))
  type_parameters (:seq
                   "<"
                   (:seq type_parameter (:repeat (:seq "," type_parameter)))
                   (:choice "," :blank)
                   ">")
  type_parameter (:seq
                  (:choice "const" :blank)
                  (:field :name _type_identifier)
                  (:field :constraint (:choice constraint :blank))
                  (:field :value (:choice default_type :blank)))
  default_type (:seq "=" type)
  constraint (:seq (:choice "extends" ":") type)
  construct_signature (:seq
                       (:choice "abstract" :blank)
                       "new"
                       (:field :type_parameters (:choice type_parameters :blank))
                       (:field :parameters formal_parameters)
                       (:field :type (:choice type_annotation :blank)))
  index_signature (:seq
                   (:choice
                    (:seq (:field :sign (:choice (:choice "-" "+") :blank)) "readonly")
                    :blank)
                   "["
                   (:choice
                    (:seq
                     (:field :name (:choice identifier (:alias _reserved_identifier identifier)))
                     ":"
                     (:field :index_type type))
                    mapped_type_clause)
                   "]"
                   (:field :type
                    (:choice
                     type_annotation
                     omitting_type_annotation
                     adding_type_annotation
                     opting_type_annotation)))
  array_type (:seq primary_type "[" "]")
  tuple_type (:seq
              "["
              (:choice (:seq _tuple_type_member (:repeat (:seq "," _tuple_type_member))) :blank)
              (:choice "," :blank)
              "]")
  readonly_type (:seq "readonly" type)
  union_type (:prec-left 0 (:seq (:choice type :blank) "|" type))
  intersection_type (:prec-left 0 (:seq (:choice type :blank) "&" type))
  function_type (:prec-left 0
                 (:seq
                  (:field :type_parameters (:choice type_parameters :blank))
                  (:field :parameters formal_parameters)
                  "=>"
                  (:field :return_type (:choice type asserts type_predicate))))
  _type_identifier (:alias identifier type_identifier)}}
