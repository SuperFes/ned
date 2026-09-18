# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "javascript"
 :word identifier
 :extras [comment html_comment (:pattern "[\\s\\p{Zs}\\uFEFF\\u2028\\u2029\\u2060\\u200B]")]
 :conflicts [[primary_expression _property_name]
             [primary_expression await_expression]
             [primary_expression await_expression _property_name]
             [primary_expression arrow_function]
             [primary_expression arrow_function _property_name]
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
             [class_static_block _property_name]]
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
               [lexical_declaration primary_expression]]
 :externals [_automatic_semicolon
             _template_chars
             _ternary_qmark
             html_comment
             "||"
             escape_sequence
             regex_pattern
             jsx_text]
 :inline [_call_signature
          _formal_parameter
          _expressions
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
          _lhs_expression]
 :supertypes [statement declaration expression primary_expression pattern]
 :reserved
 {:global ["break"
            "case"
            "catch"
            "class"
            "const"
            "continue"
            "debugger"
            "default"
            "delete"
            "do"
            "else"
            "export"
            "extends"
            "false"
            "finally"
            "for"
            "function"
            "if"
            "import"
            "in"
            "instanceof"
            "new"
            "null"
            "return"
            "super"
            "switch"
            "this"
            "throw"
            "true"
            "try"
            "typeof"
            "var"
            "void"
            "while"
            "with"]
  :properties []}
 :rules
 {program (:seq (:choice hash_bang_line :blank) (:repeat statement))
  hash_bang_line (:pattern "#!.*")
  export_statement (:choice
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
  namespace_export (:seq "*" "as" _module_export_name)
  export_clause (:seq
                 "{"
                 (:choice (:seq export_specifier (:repeat (:seq "," export_specifier))) :blank)
                 (:choice "," :blank)
                 "}")
  export_specifier (:seq
                    (:field :name _module_export_name)
                    (:choice (:seq "as" (:field :alias _module_export_name)) :blank))
  _module_export_name (:choice identifier string "default")
  declaration (:choice
               function_declaration
               generator_function_declaration
               class_declaration
               lexical_declaration
               variable_declaration
               using_declaration)
  import (:token "import")
  import_statement (:seq
                    "import"
                    (:choice (:seq import_clause _from_clause) (:field :source string))
                    (:choice import_attribute :blank)
                    _semicolon)
  import_clause (:choice
                 namespace_import
                 named_imports
                 (:seq
                  identifier
                  (:choice (:seq "," (:choice namespace_import named_imports)) :blank)))
  _from_clause (:seq "from" (:field :source string))
  namespace_import (:seq "*" "as" identifier)
  named_imports (:seq
                 "{"
                 (:choice (:seq import_specifier (:repeat (:seq "," import_specifier))) :blank)
                 (:choice "," :blank)
                 "}")
  import_specifier (:choice
                    (:field :name identifier)
                    (:seq (:field :name _module_export_name) "as" (:field :alias identifier)))
  import_attribute (:seq "with" object)
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
  using_declaration (:seq
                     (:field :kind (:choice "using" (:seq "await" "using")))
                     (:seq variable_declarator (:repeat (:seq "," variable_declarator)))
                     _semicolon)
  variable_declarator (:seq
                       (:field :name
                        (:choice identifier (:alias "of" identifier) _destructuring_pattern))
                       (:choice _initializer :blank))
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
                 (:field :left (:choice identifier (:alias "of" identifier) _destructuring_pattern))
                 (:choice _initializer :blank))
                (:seq
                 (:field :kind (:choice "let" "const"))
                 (:field :left (:choice identifier (:alias "of" identifier) _destructuring_pattern))
                 (:choice _automatic_semicolon :blank))
                (:seq
                 (:field :kind (:choice "using" (:seq "await" "using")))
                 (:field :left (:choice identifier (:alias "of" identifier) _destructuring_pattern))
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
                 (:seq "(" (:field :parameter (:choice identifier _destructuring_pattern)) ")")
                 :blank)
                (:field :body statement_block))
  finally_clause (:seq "finally" (:field :body statement_block))
  parenthesized_expression (:seq "(" _expressions ")")
  _expressions (:choice expression sequence_expression)
  expression (:choice
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
  jsx_opening_element (:prec-dynamic -1
                       (:seq
                        "<"
                        (:choice
                         (:seq
                          (:field :name _jsx_element_name)
                          (:repeat (:field :attribute _jsx_attribute)))
                         :blank)
                        ">"))
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
  jsx_self_closing_element (:seq
                            "<"
                            (:field :name _jsx_element_name)
                            (:repeat (:field :attribute _jsx_attribute))
                            "/>")
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
          (:field :name (:choice identifier :blank))
          (:choice class_heritage :blank)
          (:field :body class_body)))
  class_declaration (:prec "declaration"
                     (:seq
                      (:repeat (:field :decorator decorator))
                      "class"
                      (:field :name identifier)
                      (:choice class_heritage :blank)
                      (:field :body class_body)
                      (:choice _automatic_semicolon :blank)))
  class_heritage (:seq "extends" expression)
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
  _call_signature (:field :parameters formal_parameters)
  _formal_parameter (:choice pattern assignment_pattern)
  optional_chain "?."
  call_expression (:choice
                   (:prec "call"
                    (:seq
                     (:field :function (:choice expression import))
                     (:field :arguments arguments)))
                   (:prec "template_call"
                    (:seq
                     (:field :function (:choice primary_expression new_expression))
                     (:field :arguments template_string)))
                   (:prec "member"
                    (:seq
                     (:field :function primary_expression)
                     (:field :optional_chain optional_chain)
                     (:field :arguments arguments))))
  new_expression (:prec-right "new"
                  (:seq
                   "new"
                   (:field :constructor (:choice primary_expression new_expression))
                   (:field :arguments (:choice (:prec-dynamic 1 arguments) :blank))))
  await_expression (:prec "unary_void" (:seq "await" expression))
  member_expression (:prec "member"
                     (:seq
                      (:field :object (:choice expression primary_expression import))
                      (:choice "." (:field :optional_chain optional_chain))
                      (:field :property
                       (:choice
                        private_property_identifier
                        (:reserved :properties (:alias identifier property_identifier))))))
  subscript_expression (:prec-right "member"
                        (:seq
                         (:field :object (:choice expression primary_expression))
                         (:choice (:field :optional_chain optional_chain) :blank)
                         "["
                         (:field :index _expressions)
                         "]"))
  _lhs_expression (:choice
                   member_expression
                   subscript_expression
                   _identifier
                   (:alias _reserved_identifier identifier)
                   _destructuring_pattern)
  assignment_expression (:prec-right "assign"
                         (:seq
                          (:field :left (:choice parenthesized_expression _lhs_expression))
                          "="
                          (:field :right expression)))
  _augmented_assignment_lhs (:choice
                             member_expression
                             subscript_expression
                             (:alias _reserved_identifier identifier)
                             identifier
                             parenthesized_expression)
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
              (:alias decorator_call_expression call_expression)))
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
                              (:field :arguments arguments)))
  class_body (:seq
              "{"
              (:repeat
               (:choice
                (:seq (:field :member method_definition) (:choice ";" :blank))
                (:seq (:field :member field_definition) _semicolon)
                (:field :member class_static_block)
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
  method_definition (:seq
                     (:repeat (:field :decorator decorator))
                     (:choice
                      (:choice
                       "static"
                       (:alias
                        (:token (:seq "static" (:pattern "\\s+") "get" (:pattern "\\s*\\n")))
                        "static get"))
                      :blank)
                     (:choice "async" :blank)
                     (:choice (:choice "get" "set" "*") :blank)
                     (:field :name _property_name)
                     (:field :parameters formal_parameters)
                     (:field :body statement_block))
  pair (:seq (:field :key _property_name) ":" (:field :value expression))
  pair_pattern (:seq
                (:field :key _property_name)
                ":"
                (:field :value (:choice pattern assignment_pattern)))
  _property_name (:reserved :properties
                  (:choice
                   (:alias (:choice identifier _reserved_identifier) property_identifier)
                   private_property_identifier
                   string
                   number
                   computed_property_name))
  computed_property_name (:seq "[" expression "]")
  _reserved_identifier (:choice "get" "set" "async" "await" "static" "export" "let")
  _semicolon (:choice _automatic_semicolon ";")}}
