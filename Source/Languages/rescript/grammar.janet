# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "rescript"
 :extras [block_comment line_comment decorator (:pattern "[\\s\\uFEFF\\u2060\\u200B\\u00A0]")]
 :conflicts [[unit formal_parameters]
             [primary_expression _pattern]
             [primary_expression record_pattern]
             [primary_expression spread_pattern]
             [primary_expression _literal_pattern]
             [primary_expression _jsx_child]
             [tuple_type function_type_parameter]
             [list list_pattern]
             [array array_pattern]
             [dict dict_pattern]
             [variant_identifier module_identifier]
             [variant variant_pattern]
             [variant_arguments _variant_pattern_parameters]
             [polyvar polyvar_pattern]
             [_pattern]
             [_record_element _record_single_field]
             [_record_pun_field _record_single_pun_field]
             [_record_field_name record_pattern]
             [_statement _one_or_more_statements]
             [_statement _switch_body]
             [_inline_type function_type_parameters]
             [primary_expression parameter _pattern]
             [parameter _pattern]
             [parameter parenthesized_pattern]
             [parameter tuple_item_pattern]
             [unit _function_type_parameter_list]
             [functor_parameter module_primary_expression module_identifier_path]
             [_reserved_identifier function]
             [exception_pattern or_pattern]
             [type_binding _inline_type]
             [_type _non_function_inline_type]
             [_module_structure parenthesized_module_expression]
             [_record_type_member _object_type_member]
             [_non_function_inline_type generic_type]
             [_type_identifier polymorphic_type]]
 :precedences [["unary_not"
                "member"
                "call"
                spread_element
                await_expression
                pipe_expression
                lazy_expression
                "binary_times"
                "binary_pow"
                "binary_plus"
                "binary_shift"
                "binary_compare"
                "binary_relation"
                "binary_bitand"
                "binary_bitxor"
                "binary_bitor"
                "binary_and"
                "binary_or"
                "coercion_relation"
                expression
                primary_expression
                ternary_expression
                mutation_expression
                function
                let_declaration]
               [module_primary_expression
                value_identifier_path
                nested_variant_identifier
                module_identifier_path]
               [_jsx_attribute_value pipe_expression]
               [function_type_parameters function_type]
               [_reserved_identifier module_unpack]
               [lazy_pattern or_pattern]]
 :externals [_newline
             block_comment
             "\""
             "`"
             _template_chars
             _lparen
             _rparen
             _list_constructor
             _dict_constructor
             _decorator
             _decorator_inline]
 :inline [_module_definition]
 :supertypes [statement
              declaration
              expression
              primary_expression
              _type
              module_expression
              module_primary_expression]
 :rules
 {source_file (:seq (:repeat _statement_delimeter) (:repeat _statement))
  _statement (:seq statement (:repeat1 _statement_delimeter))
  _statement_delimeter (:choice ";" _newline)
  line_comment (:token (:seq "//" (:pattern "[^\\n]*")))
  _one_or_more_statements (:seq
                           (:repeat _statement)
                           statement
                           (:choice _statement_delimeter :blank))
  _switch_body (:seq (:repeat _statement) statement)
  statement (:choice expression_statement declaration open_statement include_statement)
  block (:prec-right 0 (:seq "{" (:choice _one_or_more_statements :blank) "}"))
  open_statement (:seq "open" (:choice "!" :blank) module_expression)
  include_statement (:seq "include" (:choice _module_definition (:seq "(" _module_structure ")")))
  declaration (:choice
               type_declaration
               let_declaration
               module_declaration
               external_declaration
               exception_declaration)
  module_binding (:prec-left 0
                  (:seq
                   (:field :name (:choice module_identifier type_identifier))
                   (:choice
                    (:seq ":" (:field :signature (:choice block module_expression functor)))
                    :blank)
                   (:choice
                    (:seq "=" (:choice "await" :blank) (:field :definition _module_definition))
                    :blank)))
  module_declaration (:seq
                      "module"
                      (:choice "rec" :blank)
                      (:choice "type" :blank)
                      (:seq module_binding (:repeat (:seq "and" module_binding))))
  _module_structure (:seq _module_definition (:choice module_type_annotation :blank))
  _module_definition (:choice block module_expression functor extension_expression)
  module_unpack (:seq
                 "unpack"
                 "("
                 (:choice
                  (:seq
                   (:choice value_identifier value_identifier_path member_expression)
                   (:choice module_type_annotation :blank))
                  (:seq call_expression (:choice module_type_annotation :blank))
                  extension_expression)
                 ")")
  functor (:seq
           (:field :parameters functor_parameters)
           (:choice (:field :return_module_type module_type_annotation) :blank)
           "=>"
           (:field :body _module_definition))
  functor_parameters (:seq
                      "("
                      (:choice
                       (:seq
                        (:seq functor_parameter (:repeat (:seq "," functor_parameter)))
                        (:choice "," :blank))
                       :blank)
                      ")")
  functor_parameter (:seq module_identifier (:choice module_type_annotation :blank))
  module_type_annotation (:seq ":" (:choice module_expression block))
  external_declaration (:seq "external" value_identifier type_annotation "=" string)
  exception_declaration (:seq
                         "exception"
                         variant_identifier
                         (:choice variant_parameters :blank)
                         (:choice
                          (:seq "=" (:choice variant_identifier nested_variant_identifier))
                          :blank))
  type_declaration (:seq
                    (:choice "export" :blank)
                    "type"
                    (:choice "rec" :blank)
                    (:seq type_binding (:repeat (:seq "and" type_binding))))
  type_binding (:seq
                (:field :name (:choice type_identifier type_identifier_path))
                (:choice type_parameters :blank)
                (:choice
                 (:choice
                  (:seq "=" extensible_type)
                  (:seq
                   (:choice (:seq "=" _non_function_inline_type) :blank)
                   (:choice
                    (:seq (:choice "=" "+=") (:choice "private" :blank) (:field :body _type))
                    :blank)
                   (:repeat type_constraint)))
                 :blank))
  extensible_type ".."
  type_parameters (:seq
                   "<"
                   (:seq
                    (:seq
                     (:seq (:choice (:choice "+" "-") :blank) type_identifier)
                     (:repeat (:seq "," (:seq (:choice (:choice "+" "-") :blank) type_identifier))))
                    (:choice "," :blank))
                   ">")
  type_annotation (:seq ":" _inline_type)
  _type (:choice _inline_type variant_type record_type as_aliasing_type)
  _inline_type (:choice _non_function_inline_type function_type)
  _non_function_inline_type (:choice
                             _type_identifier
                             tuple_type
                             polyvar_type
                             object_type
                             record_type
                             generic_type
                             unit_type
                             module_pack
                             unit
                             polymorphic_type
                             (:alias _as_aliasing_non_function_inline_type as_aliasing_type))
  polymorphic_type (:seq (:choice (:repeat1 type_identifier) abstract_type) "." _inline_type)
  type_constraint (:seq "constraint" _type "=" _type)
  tuple_type (:prec-dynamic -1
              (:seq "(" (:seq (:seq _type (:repeat (:seq "," _type))) (:choice "," :blank)) ")"))
  variant_type (:prec-left 0
                (:seq
                 (:choice "|" :blank)
                 (:seq
                  (:choice variant_declaration variant_type_spread)
                  (:repeat (:seq "|" (:choice variant_declaration variant_type_spread))))))
  variant_declaration (:prec-right 0
                       (:seq
                        variant_identifier
                        (:choice variant_parameters :blank)
                        (:choice type_annotation :blank)))
  variant_type_spread (:seq "..." _type_identifier)
  variant_parameters (:seq
                      "("
                      (:seq (:seq _type (:repeat (:seq "," _type))) (:choice "," :blank))
                      ")")
  polyvar_type (:prec-left 0
                (:seq
                 (:choice "[" "[>" "[<")
                 (:choice "|" :blank)
                 (:seq polyvar_declaration (:repeat (:seq "|" polyvar_declaration)))
                 "]"))
  polyvar_declaration (:prec-right 0
                       (:choice
                        (:seq polyvar_identifier (:choice polyvar_parameters :blank))
                        _inline_type))
  polyvar_parameters (:seq
                      "("
                      (:seq (:seq _type (:repeat (:seq "," _type))) (:choice "," :blank))
                      ")")
  record_type (:seq
               "{"
               (:choice
                (:seq
                 (:seq _record_type_member (:repeat (:seq "," _record_type_member)))
                 (:choice "," :blank))
                :blank)
               "}")
  record_type_field (:seq
                     (:choice "mutable" :blank)
                     (:alias value_identifier property_identifier)
                     (:choice "?" :blank)
                     type_annotation)
  type_spread (:seq "..." (:choice type_identifier generic_type type_identifier_path))
  _record_type_member (:choice record_type_field type_spread)
  object_type (:prec-left 0
               (:seq
                "{"
                (:choice
                 (:seq
                  (:seq _object_type_member (:repeat (:seq "," _object_type_member)))
                  (:choice "," :blank))
                 (:seq
                  "."
                  (:choice
                   (:seq
                    (:seq _object_type_member (:repeat (:seq "," _object_type_member)))
                    (:choice "," :blank))
                   :blank))
                 (:seq
                  ".."
                  (:choice
                   (:seq
                    (:seq _object_type_member (:repeat (:seq "," _object_type_member)))
                    (:choice "," :blank))
                   :blank)))
                "}"))
  _object_type_member (:choice (:alias object_type_field field) type_spread)
  object_type_field (:choice (:seq (:alias string property_identifier) ":" _type))
  generic_type (:prec-left 0 (:seq _type_identifier type_arguments))
  type_arguments (:seq "<" (:seq (:seq _type (:repeat (:seq "," _type))) (:choice "," :blank)) ">")
  function_type (:prec-left 0 (:seq function_type_parameters "=>" _type))
  function_type_parameters (:choice _non_function_inline_type _function_type_parameter_list)
  _function_type_parameter_list (:seq
                                 "("
                                 (:choice
                                  (:seq
                                   (:seq
                                    (:alias function_type_parameter parameter)
                                    (:repeat (:seq "," (:alias function_type_parameter parameter))))
                                   (:choice "," :blank))
                                  :blank)
                                 ")")
  function_type_parameter (:seq
                           (:choice uncurry :blank)
                           (:choice _type (:seq uncurry _type) labeled_parameter))
  let_declaration (:seq
                   (:choice "export" "let")
                   (:choice "rec" :blank)
                   (:seq let_binding (:repeat (:seq "and" let_binding))))
  let_binding (:seq
               (:field :pattern _pattern)
               (:choice
                (:seq type_annotation (:choice (:seq "=" (:field :body expression)) :blank))
                (:seq "=" (:field :body expression))))
  expression_statement expression
  expression (:choice
              primary_expression
              _jsx_element
              jsx_fragment
              unary_expression
              binary_expression
              coercion_expression
              ternary_expression
              for_expression
              while_expression
              mutation_expression
              await_expression
              block
              assert_expression)
  primary_expression (:choice
                      parenthesized_expression
                      value_identifier_path
                      value_identifier
                      number
                      string
                      template_string
                      character
                      (:ref "true")
                      (:ref "false")
                      function
                      unit
                      record
                      object
                      tuple
                      array
                      list
                      dict
                      variant
                      polyvar
                      if_expression
                      switch_expression
                      try_expression
                      call_expression
                      pipe_expression
                      subscript_expression
                      member_expression
                      module_pack
                      extension_expression
                      lazy_expression
                      _jsx_element
                      regex)
  parenthesized_expression (:seq "(" expression (:choice type_annotation :blank) ")")
  value_identifier_path (:seq module_primary_expression "." value_identifier)
  function (:prec-left 0
            (:seq
             (:choice "async" :blank)
             (:choice (:field :parameter value_identifier) _definition_signature)
             "=>"
             (:field :body expression)))
  record (:seq
          "{"
          (:choice
           _record_single_field
           _record_single_pun_field
           (:seq
            (:seq _record_element "," (:seq _record_element (:repeat (:seq "," _record_element))))
            (:choice "," :blank)))
          "}")
  _record_element (:choice spread_element record_field (:alias _record_pun_field record_field))
  record_field (:seq _record_field_name ":" (:choice "?" :blank) expression)
  _record_pun_field (:seq (:choice "?" :blank) _record_field_name)
  _record_single_field (:seq record_field (:choice "," :blank))
  _record_single_pun_field (:seq "?" _record_field_name (:choice "," :blank))
  _record_field_name (:choice
                      (:alias value_identifier property_identifier)
                      (:alias value_identifier_path property_identifier))
  object (:seq
          "{"
          (:choice
           (:seq (:seq _object_field (:repeat (:seq "," _object_field))) (:choice "," :blank))
           (:seq
            "."
            (:choice
             (:seq (:seq _object_field (:repeat (:seq "," _object_field))) (:choice "," :blank))
             :blank))
           (:seq
            ".."
            (:choice
             (:seq (:seq _object_field (:repeat (:seq "," _object_field))) (:choice "," :blank))
             :blank)))
          "}")
  _object_field (:alias object_field field)
  object_field (:seq (:alias string property_identifier) ":" expression)
  tuple (:seq
         "("
         (:seq
          (:seq expression "," (:seq expression (:repeat (:seq "," expression))))
          (:choice "," :blank))
         ")")
  array (:seq
         "["
         (:choice
          (:seq
           (:seq
            (:choice spread_element expression)
            (:repeat (:seq "," (:choice spread_element expression))))
           (:choice "," :blank))
          :blank)
         "]")
  list (:seq
        _list_constructor
        "{"
        (:choice
         (:seq (:seq _list_element (:repeat (:seq "," _list_element))) (:choice "," :blank))
         :blank)
        "}")
  _list_element (:choice expression spread_element)
  dict (:seq
        (:alias _dict_constructor "dict")
        "{"
        (:choice
         (:seq (:seq dict_entry (:repeat (:seq "," dict_entry))) (:choice "," :blank))
         :blank)
        "}")
  dict_entry (:seq string ":" expression)
  if_expression (:seq "if" expression block (:repeat else_if_clause) (:choice else_clause :blank))
  else_if_clause (:seq "else" "if" expression block)
  else_clause (:seq "else" block)
  switch_expression (:seq
                     "switch"
                     expression
                     "{"
                     (:repeat (:seq switch_match (:repeat _statement_delimeter)))
                     "}")
  switch_match (:prec-dynamic -1
                (:seq
                 "|"
                 (:field :pattern (:choice variant_spread_pattern _pattern))
                 (:choice guard :blank)
                 "=>"
                 (:field :body (:alias _switch_body sequence_expression))))
  guard (:seq (:choice "if" "when") expression)
  polyvar_type_pattern (:seq "#" "..." _type_identifier)
  variant_type_pattern (:seq "..." _type_identifier)
  variant_spread_pattern (:seq variant_type_pattern (:choice as_aliasing :blank))
  try_expression (:seq
                  "try"
                  expression
                  "catch"
                  "{"
                  (:repeat (:seq switch_match (:repeat _statement_delimeter)))
                  "}")
  as_aliasing (:prec-left 0 (:seq "as" _pattern (:choice type_annotation :blank)))
  as_aliasing_type (:seq _type "as" type_identifier)
  _as_aliasing_non_function_inline_type (:prec 2
                                         (:seq _non_function_inline_type "as" type_identifier))
  assert_expression (:prec-left 0 (:seq "assert" expression))
  call_expression (:prec "call"
                   (:seq
                    (:field :function primary_expression)
                    (:field :arguments (:alias call_arguments arguments))))
  pipe_expression (:prec-left 0
                   (:seq
                    (:choice primary_expression block)
                    (:choice "->" "|>")
                    (:choice primary_expression block)))
  module_pack (:seq "module" (:seq "(" (:choice _module_structure type_identifier_path) ")"))
  call_arguments (:seq
                  "("
                  (:choice uncurry :blank)
                  (:choice
                   (:seq
                    (:seq _call_argument (:repeat (:seq "," _call_argument)))
                    (:choice "," :blank))
                   :blank)
                  (:choice partial_application_spread :blank)
                  ")")
  _call_argument (:choice (:seq expression (:choice type_annotation :blank)) labeled_argument)
  partial_application_spread "..."
  labeled_argument (:seq
                    "~"
                    (:field :label value_identifier)
                    (:choice
                     (:choice
                      "?"
                      (:seq
                       "="
                       (:choice "?" :blank)
                       (:field :value expression)
                       (:choice (:field :type type_annotation) :blank)))
                     :blank))
  _definition_signature (:seq
                         (:field :parameters formal_parameters)
                         (:choice
                          (:field :return_type (:alias _return_type_annotation type_annotation))
                          :blank))
  _return_type_annotation (:seq ":" _non_function_inline_type)
  formal_parameters (:seq
                     "("
                     (:choice
                      (:seq (:seq parameter (:repeat (:seq "," parameter))) (:choice "," :blank))
                      :blank)
                     ")")
  parameter (:seq
             (:choice uncurry :blank)
             (:choice
              (:seq _pattern (:choice type_annotation :blank))
              labeled_parameter
              unit
              abstract_type))
  labeled_parameter (:seq
                     "~"
                     value_identifier
                     (:choice as_aliasing :blank)
                     (:choice type_annotation :blank)
                     (:choice (:field :default_value _labeled_parameter_default_value) :blank))
  abstract_type (:seq "type" (:repeat1 type_identifier))
  _labeled_parameter_default_value (:seq "=" (:choice "?" expression))
  _pattern (:prec-dynamic -1
            (:seq
             (:choice
              (:seq (:choice "?" :blank) value_identifier)
              _literal_pattern
              _destructuring_pattern
              polyvar_type_pattern
              unit
              module_pack
              lazy_pattern
              parenthesized_pattern
              or_pattern
              range_pattern
              exception_pattern)
             (:choice as_aliasing :blank)))
  parenthesized_pattern (:seq "(" _pattern (:choice type_annotation :blank) ")")
  range_pattern (:seq _literal_pattern ".." _literal_pattern)
  or_pattern (:prec-left 0 (:seq _pattern "|" _pattern))
  exception_pattern (:seq "exception" _pattern)
  _destructuring_pattern (:choice
                          variant_pattern
                          polyvar_pattern
                          record_pattern
                          tuple_pattern
                          array_pattern
                          list_pattern
                          dict_pattern)
  _literal_pattern (:choice
                    string
                    template_string
                    character
                    number
                    (:ref "true")
                    (:ref "false")
                    regex)
  variant_pattern (:seq
                   (:choice "?" :blank)
                   (:choice variant_identifier nested_variant_identifier)
                   (:choice (:alias _variant_pattern_parameters formal_parameters) :blank))
  _variant_pattern_parameters (:seq
                               "("
                               (:choice
                                (:seq
                                 (:seq
                                  _variant_pattern_parameter
                                  (:repeat (:seq "," _variant_pattern_parameter)))
                                 (:choice "," :blank))
                                :blank)
                               ")")
  _variant_pattern_parameter (:seq _pattern (:choice type_annotation :blank))
  polyvar_pattern (:seq
                   polyvar_identifier
                   (:choice (:alias _variant_pattern_parameters formal_parameters) :blank))
  record_pattern (:seq
                  "{"
                  (:seq
                   (:seq
                    (:seq
                     (:choice "?" :blank)
                     (:choice value_identifier value_identifier_path)
                     (:choice (:seq ":" _pattern) :blank))
                    (:repeat
                     (:seq
                      ","
                      (:seq
                       (:choice "?" :blank)
                       (:choice value_identifier value_identifier_path)
                       (:choice (:seq ":" _pattern) :blank)))))
                   (:choice "," :blank))
                  "}")
  tuple_item_pattern (:seq _pattern (:choice type_annotation :blank))
  tuple_pattern (:seq
                 "("
                 (:seq
                  (:seq
                   tuple_item_pattern
                   ","
                   (:seq tuple_item_pattern (:repeat (:seq "," tuple_item_pattern))))
                  (:choice "," :blank))
                 ")")
  array_pattern (:seq
                 "["
                 (:choice
                  (:seq
                   (:seq
                    _collection_element_pattern
                    (:repeat (:seq "," _collection_element_pattern)))
                   (:choice "," :blank))
                  :blank)
                 "]")
  list_pattern (:seq
                _list_constructor
                "{"
                (:choice
                 (:seq
                  (:seq
                   _collection_element_pattern
                   (:repeat (:seq "," _collection_element_pattern)))
                  (:choice "," :blank))
                 :blank)
                "}")
  dict_pattern (:seq
                (:alias _dict_constructor "dict")
                "{"
                (:choice
                 (:seq
                  (:seq dict_pattern_entry (:repeat (:seq "," dict_pattern_entry)))
                  (:choice "," :blank))
                 :blank)
                "}")
  dict_pattern_entry (:seq string ":" _pattern)
  _collection_element_pattern (:seq (:choice _pattern spread_pattern) (:choice as_aliasing :blank))
  spread_pattern (:seq "..." (:choice value_identifier list_pattern array_pattern))
  lazy_pattern (:seq "lazy" _pattern)
  _jsx_element (:choice jsx_element jsx_self_closing_element)
  jsx_element (:seq
               (:field :open_tag jsx_opening_element)
               (:repeat _jsx_child)
               (:field :close_tag jsx_closing_element))
  jsx_fragment (:seq "<" ">" (:repeat _jsx_child) "<" "/" ">")
  jsx_expression (:seq "{" (:choice (:choice _one_or_more_statements spread_element) :blank) "}")
  _jsx_child (:choice
              value_identifier
              value_identifier_path
              number
              string
              template_string
              character
              _jsx_element
              jsx_fragment
              block
              spread_element
              member_expression)
  jsx_opening_element (:prec-dynamic -1
                       (:seq
                        "<"
                        (:field :name _jsx_element_name)
                        (:repeat (:field :attribute _jsx_attribute))
                        ">"))
  _jsx_identifier (:alias (:choice value_identifier module_identifier) jsx_identifier)
  nested_jsx_identifier (:prec "member"
                         (:seq (:choice _jsx_identifier nested_jsx_identifier) "." _jsx_identifier))
  _jsx_element_name (:choice _jsx_identifier nested_jsx_identifier)
  jsx_closing_element (:seq "<" "/" (:field :name _jsx_element_name) ">")
  jsx_self_closing_element (:seq
                            "<"
                            (:field :name _jsx_element_name)
                            (:repeat (:field :attribute _jsx_attribute))
                            "/"
                            ">")
  _jsx_attribute_name (:alias value_identifier property_identifier)
  _jsx_attribute (:choice jsx_attribute jsx_expression)
  jsx_attribute (:seq
                 (:choice "?" :blank)
                 _jsx_attribute_name
                 (:choice (:seq "=" (:choice "?" :blank) _jsx_attribute_value) :blank))
  _jsx_attribute_value (:choice primary_expression jsx_expression)
  mutation_expression (:seq _mutation_lvalue (:choice "=" ":=") expression)
  _mutation_lvalue (:choice value_identifier member_expression subscript_expression)
  await_expression (:seq "await" expression)
  decorator (:choice
             (:alias _decorator_inline decorator_identifier)
             (:seq (:alias _decorator decorator_identifier) decorator_arguments))
  decorator_arguments (:seq
                       "("
                       (:choice
                        (:choice
                         (:seq
                          (:seq expression (:repeat (:seq "," expression)))
                          (:choice "," :blank))
                         :blank)
                        type_annotation)
                       ")")
  subscript_expression (:prec-right "member"
                        (:seq
                         (:field :object primary_expression)
                         "["
                         (:field :index expression)
                         "]"))
  member_expression (:prec "member"
                     (:seq
                      (:field :record primary_expression)
                      "."
                      (:choice
                       (:seq
                        (:field :module
                         (:seq (:repeat (:seq module_identifier ".")) module_identifier))
                        ".")
                       :blank)
                      (:field :property (:alias value_identifier property_identifier))))
  spread_element (:seq "..." expression)
  ternary_expression (:prec-left 0
                      (:seq
                       (:field :condition expression)
                       "?"
                       (:field :consequence expression)
                       ":"
                       (:field :alternative expression)))
  for_expression (:seq
                  "for"
                  value_identifier
                  "in"
                  expression
                  (:choice "to" "downto")
                  expression
                  block)
  while_expression (:seq "while" expression block)
  lazy_expression (:seq "lazy" expression)
  binary_expression (:choice
                     (:prec-left "binary_bitand"
                      (:seq
                       (:field :left expression)
                       (:field :operator "&&&")
                       (:field :right expression)))
                     (:prec-left "binary_and"
                      (:seq
                       (:field :left expression)
                       (:field :operator "&&")
                       (:field :right expression)))
                     (:prec-left "binary_bitor"
                      (:seq
                       (:field :left expression)
                       (:field :operator "|||")
                       (:field :right expression)))
                     (:prec-left "binary_or"
                      (:seq
                       (:field :left expression)
                       (:field :operator "||")
                       (:field :right expression)))
                     (:prec-left "binary_bitxor"
                      (:seq
                       (:field :left expression)
                       (:field :operator "^^^")
                       (:field :right expression)))
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left expression)
                       (:field :operator "++")
                       (:field :right expression)))
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left expression)
                       (:field :operator "+")
                       (:field :right expression)))
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left expression)
                       (:field :operator "+.")
                       (:field :right expression)))
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left expression)
                       (:field :operator "-")
                       (:field :right expression)))
                     (:prec-left "binary_plus"
                      (:seq
                       (:field :left expression)
                       (:field :operator "-.")
                       (:field :right expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left expression)
                       (:field :operator "*")
                       (:field :right expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left expression)
                       (:field :operator "*.")
                       (:field :right expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
                       (:field :right expression)))
                     (:prec-right "binary_pow"
                      (:seq
                       (:field :left expression)
                       (:field :operator "**")
                       (:field :right expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left expression)
                       (:field :operator "/")
                       (:field :right expression)))
                     (:prec-left "binary_times"
                      (:seq
                       (:field :left expression)
                       (:field :operator "/.")
                       (:field :right expression)))
                     (:prec-left "binary_shift"
                      (:seq
                       (:field :left expression)
                       (:field :operator "<<")
                       (:field :right expression)))
                     (:prec-left "binary_shift"
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>>")
                       (:field :right expression)))
                     (:prec-left "binary_shift"
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>")
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
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left expression)
                       (:field :operator "==")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left expression)
                       (:field :operator "===")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
                      (:seq
                       (:field :left expression)
                       (:field :operator "!=")
                       (:field :right expression)))
                     (:prec-left "binary_relation"
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
                       (:field :right expression))))
  coercion_expression (:prec-left "coercion_relation"
                       (:seq
                        (:field :left expression)
                        (:field :operator ":>")
                        (:field :right _type)))
  unary_expression (:choice
                    (:prec-left "unary_not"
                     (:seq (:field :operator "~~~") (:field :argument expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator "!") (:field :argument expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator "-") (:field :argument expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator "-.") (:field :argument expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator "+") (:field :argument expression)))
                    (:prec-left "unary_not"
                     (:seq (:field :operator "+.") (:field :argument expression))))
  extension_expression (:prec-right 0
                        (:seq
                         (:repeat1 "%")
                         extension_identifier
                         (:choice _extension_expression_payload :blank)))
  _extension_expression_payload (:seq "(" _one_or_more_statements (:choice _newline :blank) ")")
  variant (:prec-right 0
           (:seq
            (:choice variant_identifier nested_variant_identifier)
            (:choice (:alias variant_arguments arguments) :blank)))
  nested_variant_identifier (:seq module_primary_expression "." variant_identifier)
  variant_arguments (:seq
                     "("
                     (:choice
                      (:seq
                       (:seq
                        (:seq expression (:choice type_annotation :blank))
                        (:repeat (:seq "," (:seq expression (:choice type_annotation :blank)))))
                       (:choice "," :blank))
                      :blank)
                     ")")
  polyvar (:prec-right 0
           (:seq polyvar_identifier (:choice (:alias variant_arguments arguments) :blank)))
  _type_identifier (:choice type_identifier type_identifier_path)
  type_identifier_path (:seq module_primary_expression "." type_identifier)
  module_expression (:choice module_primary_expression module_type_of module_type_constraint)
  module_primary_expression (:choice
                             parenthesized_module_expression
                             module_identifier
                             module_identifier_path
                             functor_use
                             module_unpack)
  parenthesized_module_expression (:seq
                                   "("
                                   module_expression
                                   (:choice module_type_annotation :blank)
                                   ")")
  module_identifier_path (:choice
                          module_identifier
                          (:seq module_primary_expression "." module_identifier))
  module_type_of (:prec-left 0 (:seq "module" "type" "of" (:choice module_expression block)))
  _module_type_constraint_with (:prec-right 0
                                (:seq
                                 "with"
                                 (:seq
                                  (:choice constrain_module constrain_type)
                                  (:repeat
                                   (:seq
                                    (:choice "and" "with")
                                    (:choice constrain_module constrain_type))))))
  module_type_constraint (:prec-left 0
                          (:choice
                           (:seq module_expression _module_type_constraint_with)
                           (:seq
                            "("
                            module_expression
                            _module_type_constraint_with
                            ")"
                            _module_type_constraint_with)))
  constrain_module (:seq
                    "module"
                    module_primary_expression
                    (:choice "=" ":=")
                    module_primary_expression)
  constrain_type (:seq "type" _type (:choice "=" ":=") _type)
  functor_use (:seq module_primary_expression (:alias functor_arguments arguments))
  functor_arguments (:seq
                     "("
                     (:choice
                      (:seq
                       (:seq _functor_argument (:repeat (:seq "," _functor_argument)))
                       (:choice "," :blank))
                      :blank)
                     ")")
  _functor_argument (:choice module_expression block)
  variant_identifier (:pattern "[A-Z][a-zA-Z0-9_']*")
  polyvar_identifier (:seq
                      "#"
                      (:choice
                       (:pattern "[a-zA-Z0-9_']+")
                       (:seq (:choice "\\" :blank) (:alias string polyvar_string))))
  type_identifier (:choice (:pattern "[a-z_'][a-zA-Z0-9_']*") _escape_identifier)
  value_identifier (:choice
                    (:pattern "[a-z_][a-zA-Z0-9_']*")
                    _reserved_identifier
                    _escape_identifier)
  _escape_identifier (:token (:seq "\\\"" (:pattern "[^\"]+") "\""))
  module_identifier (:pattern "[A-Z][a-zA-Z0-9_']*")
  extension_identifier (:pattern "[a-zA-Z0-9_\\.]+")
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
           (:seq
            (:choice (:choice "-" "+") :blank)
            (:pattern "0[xX][0-9A-Fa-f][0-9A-Fa-f_]*(\\.[0-9A-Fa-f_]*)?([pP][+\\-]?[0-9][0-9_]*)?[g-zG-Z]?"))
           (:seq
            (:choice (:choice "-" "+") :blank)
            (:choice
             (:seq
              (:choice
               (:repeat1 "0")
               (:seq
                (:repeat "0")
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
               (:repeat1 "0")
               (:seq
                (:repeat "0")
                (:pattern "[1-9]")
                (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank)))
              (:seq
               (:choice "e" "E")
               (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*"))))
             (:pattern "\\d(_?\\d)*")))
           (:seq (:choice "0b" "0B") (:pattern "[0-1](_?[0-1])*"))
           (:seq (:choice "0o" "0O") (:pattern "[0-7](_?[0-7])*"))
           (:seq
            (:choice
             (:seq
              (:choice (:choice "-" "+") :blank)
              (:pattern "0[xX][0-9A-Fa-f][0-9A-Fa-f_]*(\\.[0-9A-Fa-f_]*)?([pP][+\\-]?[0-9][0-9_]*)?[g-zG-Z]?"))
             (:seq (:choice "0b" "0B") (:pattern "[0-1](_?[0-1])*"))
             (:seq (:choice "0o" "0O") (:pattern "[0-7](_?[0-7])*"))
             (:pattern "\\d(_?\\d)*"))
            "n")
           (:seq
            (:choice (:choice "-" "+") :blank)
            (:choice
             (:choice
              (:repeat1 "0")
              (:seq
               (:repeat "0")
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank)))
             (:seq (:choice "0b" "0B") (:pattern "[0-1](_?[0-1])*"))
             (:seq (:choice "0o" "0O") (:pattern "[0-7](_?[0-7])*"))
             (:seq
              (:choice (:choice "-" "+") :blank)
              (:pattern "0[xX][0-9A-Fa-f][0-9A-Fa-f_]*(\\.[0-9A-Fa-f_]*)?([pP][+\\-]?[0-9][0-9_]*)?[g-zG-Z]?")))
            (:choice "L" "l"))))
  unit (:seq "(" ")")
  unit_type "unit"
  (:ref "true") "true"
  (:ref "false") "false"
  string (:seq
          "\""
          (:repeat
           (:choice (:alias unescaped_double_string_fragment string_fragment) escape_sequence))
          "\"")
  unescaped_double_string_fragment (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+")))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu0-7]")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9a-fA-F]{2}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}"))))
  template_string (:seq
                   (:token
                    (:seq
                     (:choice
                      (:choice
                       (:pattern "[a-z_][a-zA-Z0-9_']*")
                       (:seq "\\\"" (:pattern "[^\"]+") "\""))
                      :blank)
                     "`"))
                   (:choice template_string_content :blank)
                   "`")
  template_string_content (:repeat1
                           (:choice
                            _template_chars
                            template_substitution
                            (:pattern "\\s")
                            (:choice (:alias "\\`" escape_sequence) escape_sequence)))
  template_substitution (:choice (:seq "$" value_identifier) (:seq "${" expression "}"))
  character (:seq "'" (:repeat (:choice (:pattern "[^\\\\']") escape_sequence)) "'")
  _unescaped_template_string_fragment (:token-immediate (:prec 1 (:pattern "[^`\\\\\\$]+")))
  lparen (:alias _lparen "(")
  rparen (:alias _rparen ")")
  uncurry "."
  _reserved_identifier (:choice "async" "unpack")}}
