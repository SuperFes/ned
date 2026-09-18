# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "fsharp"
 :word identifier
 :extras [(:pattern "[ \\s\\f\\uFEFF\\u2060\\u200B]|\\\\\\r?n")
          block_comment
          line_comment
          preproc_line
          compiler_directive_decl
          fsi_directive_decl
          ";"]
 :conflicts [[long_identifier _identifier_or_op]
             [simple_type type_argument]
             [preproc_if preproc_if_in_expression]
             [rules]]
 :precedences []
 :externals [_newline
             _indent
             _dedent
             "then"
             "else"
             "elif"
             "#if"
             "#else"
             "#endif"
             "class"
             _struct_begin
             _interface_begin
             "end"
             "and"
             "with"
             _triple_quoted_content
             block_comment_content
             _inside_string_marker
             _newline_not_aligned
             _tuple_marker
             _error_sentinel]
 :inline [_module_elem
          _expression_or_range
          _object_expression_inner
          _record_type_defn_inner
          _union_type_defn_inner
          _then_expression]
 :supertypes [_module_elem _pattern _expression _type _type_defn_body _static_parameter]
 :rules
 {file (:choice named_module (:repeat1 namespace) (:repeat _module_elem))
  namespace (:seq
             "namespace"
             (:choice "global" (:field :name (:seq (:choice "rec" :blank) long_identifier)))
             (:repeat _module_elem))
  named_module (:seq
                (:choice attributes :blank)
                "module"
                (:choice access_modifier :blank)
                (:field :name long_identifier)
                (:repeat _module_elem))
  _module_elem (:choice
                (:alias value_declaration declaration_expression)
                module_defn
                module_abbrev
                import_decl
                fsi_directive_decl
                type_definition
                exception_definition
                _expression
                preproc_if)
  module_abbrev (:seq
                 (:choice attributes :blank)
                 "module"
                 identifier
                 "="
                 (:field :block (:seq _indent long_identifier _dedent)))
  module_defn (:prec-left 0
               (:seq
                (:choice attributes :blank)
                "module"
                (:choice access_modifier :blank)
                identifier
                "="
                (:field :block (:seq _indent _module_body _dedent))))
  _module_body (:seq _module_elem (:repeat (:prec 2 (:seq (:alias _newline ";") _module_elem))))
  import_decl (:seq "open" long_identifier)
  attributes (:prec-left 0 (:repeat1 _attribute_set))
  _attribute_set (:seq "[<" attribute (:prec 2 (:repeat (:seq _newline attribute))) ">]")
  attribute (:seq (:choice (:seq (:field :target identifier) ":") :blank) _object_construction)
  _object_construction (:prec-left 2 (:seq _type (:choice _expression :blank)))
  value_declaration (:seq
                     (:choice attributes :blank)
                     (:choice (:prec 7 function_or_value_defn) (:prec 10 do)))
  do (:prec 9 (:seq "do" _expression_block))
  _function_or_value_defns (:prec-right 0
                            (:seq
                             _function_or_value_defn_body
                             (:repeat (:seq "and" _function_or_value_defn_body))))
  function_or_value_defn (:seq
                          (:choice "let" "let!")
                          (:choice
                           _function_or_value_defn_body
                           (:seq "rec" _function_or_value_defns)))
  _function_or_value_defn_body (:seq
                                (:choice function_declaration_left value_declaration_left)
                                (:choice (:seq ":" _type) :blank)
                                "="
                                (:field :body _expression_block))
  function_declaration_left (:prec-left 3
                             (:seq
                              (:choice "inline" :blank)
                              (:choice access_modifier :blank)
                              (:prec 100 _identifier_or_op)
                              (:choice type_arguments :blank)
                              argument_patterns))
  value_declaration_left (:prec-left 2
                          (:seq
                           (:choice "mutable" :blank)
                           (:choice access_modifier :blank)
                           _pattern
                           (:choice type_arguments :blank)))
  access_modifier (:prec 100 (:token (:prec 1000 (:choice "private" "internal" "public"))))
  class_as_reference (:seq "as" identifier)
  primary_constr_args (:seq
                       (:choice attributes :blank)
                       (:choice access_modifier :blank)
                       "("
                       (:choice _pattern :blank)
                       ")"
                       (:choice class_as_reference :blank))
  repeat_pattern (:prec-right 0 (:seq _pattern (:repeat1 (:prec 1 (:seq "," _pattern)))))
  _pattern (:choice
            "null"
            (:alias "_" wildcard_pattern)
            const
            as_pattern
            disjunct_pattern
            conjunct_pattern
            cons_pattern
            repeat_pattern
            paren_pattern
            list_pattern
            array_pattern
            record_pattern
            typed_pattern
            attribute_pattern
            type_check_pattern
            optional_pattern
            identifier_pattern
            named_field_pattern)
  optional_pattern (:prec-left 0 (:seq "?" _pattern))
  type_check_pattern (:prec-right 0 (:seq ":?" atomic_type (:choice (:seq "as" identifier) :blank)))
  attribute_pattern (:prec-left 0 (:seq attributes _pattern))
  paren_pattern (:prec 1 (:seq "(" _pattern ")"))
  as_pattern (:prec-left 0 (:seq _pattern "as" identifier))
  cons_pattern (:prec-left 0 (:seq _pattern "::" _pattern))
  disjunct_pattern (:prec-left 0 (:seq _pattern "|" _pattern))
  conjunct_pattern (:prec-left 0 (:seq _pattern "&" _pattern))
  typed_pattern (:prec-left -1
                 (:seq
                  _pattern
                  ":"
                  _type
                  (:field :constraints (:choice type_argument_constraints :blank))))
  argument_patterns (:prec-left 1000 (:repeat1 _atomic_pattern))
  field_pattern (:prec 1 (:seq long_identifier "=" _pattern))
  _atomic_pattern (:choice
                   "null"
                   "_"
                   const
                   long_identifier
                   list_pattern
                   record_pattern
                   array_pattern
                   (:seq "(" _pattern ")"))
  _list_pattern_content (:field :block
                         (:seq
                          _indent
                          (:seq
                           (:choice _newline :blank)
                           _pattern
                           (:repeat (:seq _newline _pattern)))
                          _dedent))
  list_pattern (:seq "[" (:choice _list_pattern_content :blank) "]")
  array_pattern (:seq "[|" (:choice _list_pattern_content :blank) "|]")
  record_pattern (:prec-left 0 (:seq "{" field_pattern (:repeat (:seq _newline field_pattern)) "}"))
  named_field (:seq (:choice (:seq identifier "=") :blank) _pattern)
  named_field_pattern (:prec-left 0
                       (:seq "(" named_field (:repeat (:seq _newline named_field)) ")"))
  identifier_pattern (:prec-left 1
                      (:seq
                       long_identifier_or_op
                       (:choice _pattern :blank)
                       (:choice _pattern :blank)))
  _expression_block (:seq _indent _expression _dedent)
  _expression (:choice
               "null"
               const
               paren_expression
               begin_end_expression
               long_identifier_or_op
               typed_expression
               infix_expression
               index_expression
               mutate_expression
               list_expression
               array_expression
               ce_expression
               prefixed_expression
               brace_expression
               anon_record_expression
               typecast_expression
               declaration_expression
               do_expression
               fun_expression
               function_expression
               sequential_expression
               if_expression
               while_expression
               for_expression
               match_expression
               try_expression
               literal_expression
               tuple_expression
               application_expression
               dot_expression
               (:alias preproc_if_in_expression preproc_if))
  literal_expression (:prec 21
                      (:choice (:seq "<@" _expression "@>") (:seq "<@@" _expression "@@>")))
  long_identifier_or_op (:prec-right 0
                         (:choice
                          long_identifier
                          (:seq long_identifier "." _identifier_or_op)
                          _identifier_or_op))
  tuple_expression (:prec-right 16
                    (:seq _expression "," (:choice _tuple_marker :blank) _expression))
  brace_expression (:prec 16
                    (:seq
                     "{"
                     (:field :block
                      (:seq
                       _indent
                       (:choice field_initializers object_expression with_field_expression)
                       _dedent))
                     "}"))
  anon_record_expression (:prec 21
                          (:seq
                           "{|"
                           (:field :block
                            (:seq
                             _indent
                             (:choice field_initializers with_field_expression)
                             _dedent))
                           "|}"))
  _object_expression_inner (:seq _object_members (:repeat interface_implementation))
  object_expression (:prec 25
                     (:seq
                      "new"
                      _expression
                      (:choice (:seq "as" identifier) :blank)
                      _object_expression_inner))
  with_field_expression (:seq
                         _expression
                         "with"
                         (:field :block (:seq _indent field_initializers _dedent)))
  prefixed_expression (:seq
                       (:choice
                        "return"
                        "return!"
                        "yield"
                        "yield!"
                        "lazy"
                        "assert"
                        "upcast"
                        "downcast"
                        "new"
                        prefix_op)
                       (:prec-right 15 _expression))
  typecast_expression (:prec-right 16 (:seq _expression (:choice ":" ":>" ":?" ":?>") _type))
  for_expression (:prec 9
                  (:seq
                   "for"
                   (:choice
                    (:seq _pattern "in" _expression_or_range)
                    (:seq identifier "=" _expression (:choice "to" "downto") _expression))
                   "do"
                   _expression_block
                   (:choice "done" :blank)))
  while_expression (:prec 9
                    (:seq
                     (:choice "while" "while!")
                     _expression
                     "do"
                     _expression_block
                     (:choice "done" :blank)))
  _else_expression (:seq "else" (:field :else _expression_block))
  _then_expression (:seq "then" (:field :then _expression_block))
  elif_expression (:seq "elif" (:field :guard _expression_block) _then_expression)
  _if_branch (:seq "if" (:field :guard _expression_block))
  if_expression (:seq
                 _if_branch
                 _then_expression
                 (:repeat elif_expression)
                 (:choice _else_expression :blank))
  fun_expression (:prec-right 8 (:seq "fun" argument_patterns "->" _expression_block))
  try_expression (:prec 8
                  (:seq
                   "try"
                   _expression_block
                   (:choice _newline :blank)
                   (:choice (:seq "with" rules) (:seq "finally" _expression_block))))
  match_expression (:seq
                    (:choice "match" "match!")
                    _expression
                    (:choice _newline :blank)
                    "with"
                    (:choice (:seq _newline rules) (:field :block (:seq _indent rules _dedent))))
  function_expression (:prec 8 (:seq "function" (:field :block (:seq _indent rules _dedent))))
  mutate_expression (:prec-right 16
                     (:seq (:field :assignee _expression) "<-" (:field :value _expression)))
  index_expression (:prec 20
                    (:seq _expression ".[" (:choice (:field :index _expression) slice_ranges) "]"))
  typed_expression (:prec 21
                    (:seq
                     _expression
                     (:token-immediate (:prec 21 "<"))
                     (:choice types :blank)
                     (:prec 21 ">")))
  declaration_expression (:seq
                          (:choice
                           (:seq (:choice "use" "use!") identifier "=" _expression_block)
                           function_or_value_defn)
                          (:field :in _expression))
  do_expression (:prec 8 (:seq (:choice "do" "do!") _expression_block))
  _list_elements (:prec-right 113
                  (:seq
                   (:choice _newline :blank)
                   _expression
                   (:repeat (:prec-right 113 (:seq (:alias _newline ";") _expression)))))
  _list_element (:seq
                 _indent
                 (:choice _list_elements _comp_or_range_expression slice_ranges)
                 _dedent)
  list_expression (:seq "[" (:choice _list_element :blank) "]")
  array_expression (:seq "[|" (:choice _list_element :blank) "|]")
  range_expression (:prec 22
                    (:seq _expression ".." _expression (:choice (:seq ".." _expression) :blank)))
  _expression_or_range (:choice _expression range_expression)
  rule (:prec-right 0
        (:seq
         (:field :pattern _pattern)
         (:choice (:seq "when" (:field :guard _expression)) :blank)
         "->"
         (:field :block _expression_block)))
  rules (:seq (:choice "|" :blank) rule (:repeat (:seq (:choice _newline :blank) "|" rule)))
  begin_end_expression (:prec 21
                        (:seq "begin" (:field :block (:seq _indent _expression _dedent)) "end"))
  paren_expression (:prec 21 (:seq "(" _expression_block ")"))
  _high_prec_app (:prec-left 20
                  (:seq
                   _expression
                   (:choice unit (:seq (:token-immediate (:prec 10000 "(")) _expression_block ")"))))
  _low_prec_app (:prec-left 16 (:seq _expression _expression))
  application_expression (:choice _high_prec_app _low_prec_app)
  dot_expression (:prec-right 19
                  (:seq (:field :base _expression) "." (:field :field long_identifier_or_op)))
  infix_expression (:prec-left 16 (:seq _expression infix_op _expression))
  ce_expression (:prec-left 15
                 (:seq
                  (:prec -1 _expression)
                  "{"
                  (:field :block (:seq _indent _comp_or_range_expression _dedent))
                  "}"))
  sequential_expression (:prec-right 1
                         (:seq
                          _expression
                          (:repeat1 (:prec-right 1 (:seq (:alias _newline ";") _expression)))))
  _comp_or_range_expression (:choice _expression short_comp_expression)
  short_comp_expression (:seq "for" _pattern "in" _expression_or_range "->" _expression)
  slice_ranges (:seq slice_range (:repeat (:seq "," slice_range)))
  _slice_range_special (:prec-left 23
                        (:choice
                         (:seq (:field :from _expression) (:token (:prec 22 "..")))
                         (:seq (:token (:prec 100022 "..")) (:field :to _expression))
                         (:seq
                          (:field :from _expression)
                          (:token (:prec 22 ".."))
                          (:field :to _expression))))
  slice_range (:choice _slice_range_special _expression "*")
  _type (:prec 4
         (:choice
          simple_type
          generic_type
          paren_type
          function_type
          compound_type
          postfix_type
          list_type
          static_type
          type_argument
          constrained_type
          flexible_type
          anon_record_type))
  simple_type (:choice long_identifier _static_type_identifier)
  generic_type (:prec-right 5 (:seq long_identifier "<" (:choice type_attributes :blank) ">"))
  paren_type (:seq "(" _type ")")
  function_type (:prec-right 0 (:seq _type "->" _type))
  compound_type (:prec-right 0 (:seq _type (:repeat1 (:prec-right 0 (:seq "*" _type)))))
  postfix_type (:prec-left 4 (:seq _type long_identifier))
  list_type (:seq _type "[]")
  static_type (:prec 10 (:seq _type type_arguments))
  constrained_type (:prec-right 0 (:seq type_argument ":>" _type))
  flexible_type (:prec-right 0 (:seq "#" _type))
  anon_record_type (:seq "{|" (:field :block (:seq _indent record_fields _dedent)) "|}")
  types (:seq _type (:repeat (:prec-left 12 (:seq "," _type))))
  _static_type_identifier (:prec 10 (:seq (:choice "^" (:token (:prec 100 "'"))) identifier))
  _static_parameter (:choice static_parameter_value named_static_parameter)
  named_static_parameter (:prec 3 (:seq identifier "=" static_parameter_value))
  type_attribute (:choice _type _static_parameter)
  type_attributes (:seq type_attribute (:repeat (:prec-right 13 (:seq "," type_attribute))))
  atomic_type (:prec-right 0
               (:choice
                (:seq "#" _type)
                type_argument
                (:seq "(" _type ")")
                long_identifier
                (:seq long_identifier "<" type_attributes ">")))
  constraint (:prec 1000000
              (:choice
               (:seq type_argument ":>" _type)
               (:seq type_argument ":" "null")
               (:seq
                type_argument
                ":"
                "("
                (:choice trait_member_constraint (:seq "new" ":" "unit" "->" _type))
                ")")
               (:seq type_argument ":" "struct")
               (:seq type_argument ":" "not" "struct")
               (:seq type_argument ":" "enum" "<" _type ">")
               (:seq type_argument ":" "unmanaged")
               (:seq type_argument ":" "equality")
               (:seq type_argument ":" "comparison")
               (:seq type_argument ":" "delegate" "<" _type "," _type ">")
               (:seq "default" type_argument ":" _type)))
  type_argument_constraints (:seq "when" constraint (:repeat (:seq "and" constraint)))
  type_argument (:prec 10
                 (:choice
                  "_"
                  (:seq _static_type_identifier (:repeat (:seq "or" _static_type_identifier)))))
  type_argument_defn (:seq (:choice attributes :blank) type_argument)
  type_arguments (:seq
                  "<"
                  type_argument_defn
                  (:repeat (:prec-left 13 (:seq "," type_argument_defn)))
                  (:choice type_argument_constraints :blank)
                  ">")
  trait_member_constraint (:seq (:choice "static" :blank) "member" _identifier_or_op ":" _type)
  member_signature (:prec-left 0
                    (:seq
                     identifier
                     (:choice type_arguments :blank)
                     ":"
                     curried_spec
                     (:choice
                      (:choice
                       (:seq "with" "get")
                       (:seq "with" "set")
                       (:seq "with" "get" "," "set")
                       (:seq "with" "set" "," "get"))
                      :blank)))
  curried_spec (:seq (:repeat (:seq arguments_spec "->")) _type)
  argument_spec (:prec-left 0
                 (:seq (:choice attributes :blank) (:choice argument_name_spec :blank) _type))
  arguments_spec (:seq argument_spec (:repeat (:seq "*" argument_spec)))
  argument_name_spec (:seq (:choice "?" :blank) (:field :name identifier) ":")
  interface_spec (:seq "interface" _type)
  static_parameter (:choice static_parameter_value (:seq "id" "=" static_parameter_value))
  static_parameter_value (:choice const (:seq const _expression))
  exception_definition (:seq
                        (:choice attributes :blank)
                        "exception"
                        (:choice access_modifier :blank)
                        (:field :exception_name long_identifier)
                        (:choice (:seq "of" _type) :blank))
  type_definition (:prec-left 0
                   (:seq
                    (:choice attributes :blank)
                    "type"
                    _type_defn_body
                    (:repeat (:seq (:choice attributes :blank) "and" _type_defn_body))))
  _type_defn_body (:choice
                   delegate_type_defn
                   record_type_defn
                   union_type_defn
                   interface_type_defn
                   anon_type_defn
                   enum_type_defn
                   type_abbrev_defn
                   type_extension)
  type_name (:prec 2
             (:seq
              (:choice attributes :blank)
              (:choice access_modifier :blank)
              (:choice
               (:seq (:field :type_name long_identifier) (:choice type_arguments :blank))
               (:seq (:choice type_argument :blank) (:field :type_name identifier)))))
  type_extension (:seq type_name type_extension_elements)
  delegate_type_defn (:seq type_name "=" (:field :block (:seq _indent delegate_signature _dedent)))
  delegate_signature (:seq "delegate" "of" _type)
  type_abbrev_defn (:seq type_name "=" (:field :block (:seq _indent _type _dedent)))
  _class_type_body_inner (:choice class_inherits_decl type_extension_elements)
  _class_type_body (:seq _class_type_body_inner (:repeat (:seq _newline _class_type_body_inner)))
  _record_type_defn_inner (:seq
                           (:choice access_modifier :blank)
                           "{"
                           (:field :block (:seq _indent record_fields _dedent))
                           "}"
                           (:choice type_extension_elements :blank))
  record_type_defn (:prec-left 0
                    (:seq
                     type_name
                     "="
                     (:field :block (:seq _indent _record_type_defn_inner _dedent))))
  record_fields (:seq record_field (:repeat (:seq _newline record_field)) (:choice _newline :blank))
  record_field (:seq
                (:choice attributes :blank)
                (:choice "mutable" :blank)
                (:choice access_modifier :blank)
                identifier
                ":"
                _type)
  enum_type_defn (:seq
                  type_name
                  "="
                  (:choice (:field :block (:seq _indent enum_type_cases _dedent)) enum_type_cases))
  enum_type_cases (:seq (:choice "|" :blank) enum_type_case (:repeat (:seq "|" enum_type_case)))
  enum_type_case (:seq identifier "=" const)
  _union_type_defn_inner (:seq
                          (:choice access_modifier :blank)
                          union_type_cases
                          (:choice type_extension_elements :blank))
  union_type_defn (:prec-left 0
                   (:seq
                    type_name
                    "="
                    (:choice
                     (:field :block (:seq _indent _union_type_defn_inner _dedent))
                     _union_type_defn_inner)))
  union_type_cases (:seq (:choice "|" :blank) union_type_case (:repeat (:seq "|" union_type_case)))
  union_type_case (:prec 8
                   (:seq
                    (:choice attributes :blank)
                    identifier
                    (:choice (:choice (:seq "of" union_type_fields) (:seq ":" _type)) :blank)))
  union_type_fields (:seq union_type_field (:repeat (:seq "*" union_type_field)))
  union_type_field (:prec-left 0 (:choice _type (:seq identifier ":" _type)))
  interface_type_defn (:prec-left 1
                       (:seq
                        type_name
                        "="
                        (:seq
                         (:alias _interface_begin "interface")
                         (:field :block (:seq _indent (:repeat _type_defn_elements) _dedent))
                         "end")))
  anon_type_defn (:prec-left 0
                  (:seq
                   type_name
                   (:choice primary_constr_args :blank)
                   "="
                   (:choice
                    (:field :block (:seq _indent _class_type_body _dedent))
                    (:seq
                     (:choice "begin" "class")
                     (:field :block (:seq _indent (:choice _class_type_body :blank) _dedent))
                     "end")
                    (:seq
                     (:alias _struct_begin "struct")
                     (:field :block (:seq _indent (:repeat _type_defn_elements) _dedent))
                     "end"))))
  _class_function_or_value_defn (:seq
                                 (:choice attributes :blank)
                                 (:choice "static" :blank)
                                 (:choice function_or_value_defn (:seq "do" _expression_block)))
  _type_extension_inner (:repeat1 (:choice _class_function_or_value_defn _type_defn_elements))
  type_extension_elements (:prec-left 0
                           (:seq
                            (:choice
                             (:seq
                              "with"
                              (:field :block (:seq _indent _type_extension_inner _dedent)))
                             _type_extension_inner)))
  _type_defn_elements (:choice
                       member_defn
                       interface_implementation
                       (:alias preproc_if_in_class_definition preproc_if))
  interface_implementation (:prec-left 0 (:seq "interface" _type (:choice _object_members :blank)))
  _member_defns (:prec-left 0
                 (:seq member_defn (:repeat (:seq (:choice _newline :blank) member_defn))))
  _object_members (:seq "with" (:field :block (:seq _indent _member_defns _dedent)))
  member_defn (:prec 100016
               (:seq
                (:choice attributes :blank)
                (:choice
                 (:seq
                  (:choice "static" :blank)
                  "member"
                  (:choice "inline" :blank)
                  (:choice access_modifier :blank)
                  method_or_prop_defn)
                 (:seq
                  "abstract"
                  (:choice "member" :blank)
                  (:choice access_modifier :blank)
                  member_signature)
                 (:seq "member" "val" property_or_ident _val_property_defn)
                 (:seq "override" (:choice access_modifier :blank) method_or_prop_defn)
                 (:seq "default" (:choice access_modifier :blank) method_or_prop_defn)
                 (:seq
                  (:choice "static" :blank)
                  "val"
                  (:choice "mutable" :blank)
                  (:choice access_modifier :blank)
                  identifier
                  ":"
                  _type)
                 (:seq "static" value_declaration)
                 additional_constr_defn)))
  property_or_ident (:choice
                     (:seq (:field :instance identifier) "." (:field :method identifier))
                     _identifier_or_op)
  _method_defn (:choice
                (:seq
                 (:choice type_arguments :blank)
                 (:field :args (:repeat1 _pattern))
                 "="
                 _expression_block))
  _property_accessor_body (:seq
                           argument_patterns
                           (:choice (:seq ":" _type) :blank)
                           "="
                           _expression_block)
  property_accessor (:seq (:choice "get" "set") _property_accessor_body)
  _property_defn (:prec-left 100017
                  (:seq
                   (:choice (:seq ":" _type) :blank)
                   (:choice
                    (:seq "=" _expression_block)
                    (:seq
                     "with"
                     (:field :block
                      (:seq
                       _indent
                       (:seq property_accessor (:repeat (:seq "and" property_accessor)))
                       _dedent))))))
  _val_property_defn (:prec-left 100016
                      (:seq
                       (:choice (:seq ":" _type) :blank)
                       "="
                       _expression
                       (:choice
                        (:seq
                         "with"
                         (:choice "get" "set" (:seq "get" "," "set") (:seq "set" "," "get")))
                        :blank)))
  method_or_prop_defn (:prec 3
                       (:seq
                        (:field :name property_or_ident)
                        (:choice
                         _method_defn
                         _property_defn
                         (:seq
                          "with"
                          (:field :block (:seq _indent _function_or_value_defns _dedent))))))
  additional_constr_defn (:seq
                          (:choice access_modifier :blank)
                          "new"
                          _pattern
                          "="
                          _expression_block)
  class_inherits_decl (:prec-left 0
                       (:seq
                        "inherit"
                        (:field :block
                         (:seq _indent (:seq _type (:choice _expression :blank)) _dedent))))
  field_initializer (:prec 17
                     (:seq
                      (:field :field long_identifier)
                      (:token (:prec 10000000 "="))
                      (:field :value _expression)))
  field_initializers (:prec 10000000
                      (:seq field_initializer (:repeat (:seq _newline field_initializer))))
  _escape_char (:token-immediate (:prec 100 (:pattern "\\\\[\"\\'ntbrafv]")))
  _non_escape_char (:token-immediate (:prec 100 (:pattern "\\\\[^\"\\'ntbrafv]")))
  _simple_char_char (:token-immediate (:pattern "[^\\n\\t\\r\\u0008\\a\\f\\v'\\\\]"))
  _unicodegraph_short (:pattern "\\\\u[0-9a-fA-F]{4}")
  _unicodegraph_long (:pattern "\\\\u[0-9a-fA-F]{8}")
  _trigraph (:pattern "\\\\[0-9]{3}")
  _char_char (:choice _simple_char_char _escape_char _trigraph _unicodegraph_short)
  _simple_string_char (:choice
                       _inside_string_marker
                       (:token-immediate (:prec 1 (:pattern "[^\\t\\r\\u0008\\a\\f\\v\\\\\"]"))))
  _string_char (:choice
                _simple_string_char
                _escape_char
                _trigraph
                _unicodegraph_short
                _non_escape_char
                _unicodegraph_long)
  char (:prec -1
        (:pattern "'([^\\n\\t\\r\\u0008\\a\\f\\v\\\\]|\\\\[\"\\'ntbrafv]|\\\\[0-9]{3}|\\\\u[0-9a-fA-F]{4}|(\\\\\\\\))?'B?"))
  format_string_eval (:seq (:token-immediate (:prec 1000 "{")) _expression "}")
  format_string (:seq
                 (:token (:prec 100 "$\""))
                 (:repeat (:choice format_string_eval _string_char))
                 "\"")
  _string_literal (:seq "\"" (:repeat _string_char) "\"")
  string (:choice _string_literal format_string)
  _verbatim_string_char (:choice _simple_string_char _non_escape_char "\\" (:pattern "\\\"\\\""))
  verbatim_string (:seq "@\"" (:repeat _verbatim_string_char) (:token-immediate "\""))
  bytearray (:seq "\"" (:repeat _string_char) (:token-immediate "\"B"))
  verbatim_bytearray (:seq "@\"" (:repeat _verbatim_string_char) (:token-immediate "\"B"))
  format_triple_quoted_string (:seq (:token (:prec 100 "$\"\"\"")) _triple_quoted_content "\"\"\"")
  triple_quoted_string (:choice
                        (:seq "\"\"\"" _triple_quoted_content "\"\"\"")
                        format_triple_quoted_string)
  bool (:token (:choice "true" "false"))
  unit (:token (:prec 100000 "()"))
  const (:choice
         sbyte
         int16
         int32
         int64
         byte
         uint16
         uint32
         int
         xint
         nativeint
         unativeint
         decimal
         float
         uint64
         ieee32
         ieee64
         bignum
         char
         string
         verbatim_string
         triple_quoted_string
         bytearray
         verbatim_bytearray
         bool
         unit)
  identifier (:token
              (:choice
               (:pattern "[_\\p{XID_Start}][_'\\p{XID_Continue}]*")
               (:pattern "``([^`\\n\\r\\t])+``")))
  long_identifier (:prec-right 0 (:seq identifier (:repeat (:seq "." identifier))))
  active_pattern (:prec 1000
                  (:seq
                   "(|"
                   (:alias identifier active_pattern_op_name)
                   (:repeat (:seq "|" (:alias identifier active_pattern_op_name)))
                   (:choice (:seq "|" (:alias "_" wildcard_active_pattern_op)) :blank)
                   "|)"))
  op_identifier (:token
                 (:prec 1000
                  (:seq
                   "("
                   (:pattern "\\s*")
                   (:choice "?" (:pattern "[!%&*+-./<=>@^|~$?][!%&*+-./<=>@^|~?]*") ".. ..")
                   (:pattern "\\s*")
                   ")")))
  _identifier_or_op (:choice identifier op_identifier active_pattern)
  _infix_or_prefix_op (:choice "+" "-" "+." "-." "%" "&" "&&")
  prefix_op (:prec-left 0
             (:choice _infix_or_prefix_op (:repeat1 "~") (:pattern "[!?][!%&*+-./<=>@^|~?]*")))
  infix_op (:prec 4
            (:choice
             _infix_or_prefix_op
             (:token-immediate (:prec 1 (:pattern "[+-]")))
             (:pattern "[-+<>|&^*/'%@?][!%&*+./<=>@^|~?-]*")
             "||"
             "="
             "!="
             ":="
             "::"
             "$"
             "or"
             "?"
             "?"
             "?<-"
             "?->"))
  int (:token (:pattern "[+-]?([0-9]_?)+"))
  xint (:token
        (:choice
         (:pattern "0[xX]([0-9a-fA-F]_?)+")
         (:pattern "0[oO]([0-7]_?)+")
         (:pattern "0[bB]([0-1]_?)+")))
  sbyte (:seq (:choice int xint) (:token-immediate "y"))
  byte (:seq (:choice int xint) (:token-immediate "uy"))
  int16 (:seq (:choice int xint) (:token-immediate "s"))
  uint16 (:seq (:choice int xint) (:token-immediate "us"))
  int32 (:seq (:choice int xint) (:token-immediate "l"))
  uint32 (:seq (:choice int xint) (:token-immediate (:choice "ul" "u")))
  nativeint (:seq (:choice int xint) (:token-immediate "n"))
  unativeint (:seq (:choice int xint) (:token-immediate "un"))
  int64 (:seq (:choice int xint) (:token-immediate "L"))
  uint64 (:seq (:choice int xint) (:token-immediate (:choice "UL" "uL")))
  ieee32 (:choice (:seq float (:token-immediate "f")) (:seq xint (:token-immediate "lf")))
  ieee64 (:seq xint (:token-immediate "LF"))
  bignum (:seq int (:token-immediate (:pattern "[QRZING]")))
  decimal (:seq (:choice float int) (:token-immediate (:pattern "[Mm]")))
  float (:prec-right 0
         (:alias
          (:choice
           (:seq int (:token-immediate ".") (:choice int :blank))
           (:seq
            int
            (:choice (:seq (:token-immediate ".") int) :blank)
            (:token-immediate (:pattern "[eE][+-]?"))
            int))
          "float"))
  block_comment (:seq "(*" block_comment_content (:token-immediate "*)"))
  line_comment (:token (:pattern "\\/\\/+[^\\n\\r]*"))
  compiler_directive_decl (:prec 100000
                           (:choice
                            (:seq "#nowarn" (:alias _string_literal string) _newline_not_aligned)
                            (:seq "#light" _newline_not_aligned)))
  fsi_directive_decl (:seq
                      (:choice "#r" "#load")
                      (:choice (:choice (:alias _string_literal string) verbatim_string) :blank)
                      (:pattern "\\n"))
  preproc_line (:seq
                (:alias (:pattern "#(line)?") "#line")
                int
                (:choice (:choice (:alias _string_literal string) verbatim_string) :blank)
                _newline_not_aligned)
  preproc_if (:prec 0
              (:seq
               "#if"
               (:field :condition identifier)
               _newline_not_aligned
               _module_elem
               (:field :alternative (:choice preproc_else :blank))
               "#endif"))
  preproc_else (:prec 0 (:seq "#else" _module_elem))
  preproc_if_in_expression (:prec -2
                            (:seq
                             "#if"
                             (:field :condition identifier)
                             _newline_not_aligned
                             (:repeat (:seq (:choice _newline :blank) _expression))
                             (:field :alternative
                              (:choice (:alias preproc_else_in_expression preproc_else) :blank))
                             "#endif"))
  preproc_else_in_expression (:prec -2
                              (:seq "#else" (:repeat (:seq (:choice _newline :blank) _expression))))
  preproc_if_in_class_definition (:prec -2
                                  (:seq
                                   "#if"
                                   (:field :condition identifier)
                                   _newline_not_aligned
                                   (:repeat (:seq (:choice _newline :blank) _class_type_body_inner))
                                   (:field :alternative
                                    (:choice
                                     (:alias preproc_else_in_class_definition preproc_else)
                                     :blank))
                                   "#endif"))
  preproc_else_in_class_definition (:prec -2
                                    (:seq
                                     "#else"
                                     (:repeat
                                      (:seq (:choice _newline :blank) _class_type_body_inner))))
  preproc_if_in_member_definition (:prec -2
                                   (:seq
                                    "#if"
                                    (:field :condition identifier)
                                    _newline_not_aligned
                                    (:repeat member_defn)
                                    (:field :alternative
                                     (:choice
                                      (:alias preproc_else_in_member_definition preproc_else)
                                      :blank))
                                    "#endif"))
  preproc_else_in_member_definition (:prec -2 (:seq "#else" (:repeat member_defn)))}}
