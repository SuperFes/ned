# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "ocaml_interface"
 :word _lowercase_identifier
 :inherits "ocaml"
 :extras [(:pattern "\\s") comment line_number_directive attribute]
 :conflicts [[_proper_tuple_type labeled_tuple_element_type]
             [_include_or_include_functor]
             [_module_typed functor_type]
             [_type _argument_type]]
 :precedences [[constructed_type
                hash_type
                parenthesized_type
                kind_annotated_type_variable
                function_type
                aliased_type
                _type]
               ["prefix"
                "dot"
                "hash"
                "method"
                "app"
                "sign"
                "pow"
                "mult"
                "add"
                "cons"
                "concat"
                "rel"
                "and"
                "or"
                "tuple"
                "assign"
                "set"
                "if"
                "seq"
                _sequence_expression
                _expression
                _non_function_expression
                _simple_expression
                _delimited_expression]
               ["range_pattern"
                "constructor_pattern"
                "cons_pattern"
                "tuple_pattern"
                "or_pattern"
                "alias_pattern"
                _pattern
                _binding_pattern
                _simple_pattern
                _simple_binding_pattern
                _delimited_pattern
                _delimited_binding_pattern]
               [_product_kind mod_bounded_kind with_bounded_kind _kind]
               [module_path constructor_path]]
 :externals [comment
             _left_quoted_string_delimiter
             _right_quoted_string_delimiter
             "\""
             line_number_directive
             _null
             _error_sentinel]
 :inline [_parameter
          _argument
          _type_or_kind_annotated_type_variable
          _inline_expression
          _extension
          _item_extension
          _value_pattern
          _label_name
          _field_name
          _class_name
          _class_type_name
          _method_name
          _simple_module_name
          _module_type_name
          _simple_constructor_name
          _constructor_path
          _type_variable
          _label
          _tuple_label
          _mode
          _modality
          _kind_name]
 :supertypes [_local_structure_item
              _structure_item
              _signature_item
              _module_type
              _simple_module_expression
              _module_expression
              _simple_class_type
              _class_type
              _class_field_specification
              _simple_class_expression
              _class_expression
              _class_field
              _delimited_type
              _simple_type
              _type
              _delimited_expression
              _simple_expression
              _non_function_expression
              _expression
              _sequence_expression
              _delimited_pattern
              _simple_pattern
              _effect_pattern
              _pattern
              _delimited_binding_pattern
              _simple_binding_pattern
              _effect_binding_pattern
              _binding_pattern
              _binding_pattern_no_exn
              _constant
              _signed_constant
              _infix_operator
              _kind]
 :reserved
 {:global ["and"
            "as"
            "assert"
            "begin"
            "class"
            "constraint"
            "do"
            "done"
            "downto"
            "else"
            "end"
            "exception"
            "external"
            "false"
            "for"
            "fun"
            "function"
            "functor"
            "if"
            "in"
            "include"
            "inherit"
            "initializer"
            "lazy"
            "let"
            "match"
            "method"
            "module"
            "mutable"
            "new"
            "object"
            "of"
            "open"
            "or"
            "private"
            "rec"
            "sig"
            "struct"
            "then"
            "to"
            "true"
            "try"
            "type"
            "val"
            "virtual"
            "when"
            "while"
            "with"
            "lor"
            "lxor"
            "mod"
            "land"
            "lsl"
            "lsr"
            "asr"]
  :attribute_id ["lor" "lxor" "mod" "land" "lsl" "lsr" "asr"]}
 :rules
 {compilation_unit (:choice _signature :blank)
  shebang (:pattern "#!.*")
  _structure (:choice
              (:repeat1 ";;")
              (:seq
               (:repeat ";;")
               (:choice _structure_item toplevel_directive expression_item)
               (:repeat
                (:choice
                 (:seq (:repeat ";;") (:choice _structure_item toplevel_directive))
                 (:seq (:repeat1 ";;") expression_item)))
               (:repeat ";;")))
  expression_item (:seq _sequence_expression (:repeat item_attribute))
  _signature (:choice
              (:repeat1 ";;")
              (:seq _at_at_modality (:repeat ";;"))
              (:seq
               (:choice _at_at_modality :blank)
               (:repeat1 (:seq (:repeat ";;") _signature_item))
               (:repeat ";;")))
  toplevel_directive (:seq
                      directive
                      (:choice
                       (:choice string quoted_string number value_path module_path boolean)
                       :blank))
  _local_structure_item (:choice
                         external
                         type_definition
                         exception_definition
                         module_definition
                         module_type_definition
                         open_module
                         class_definition
                         class_type_definition
                         floating_attribute
                         _item_extension)
  _structure_item (:choice
                   _local_structure_item
                   value_definition
                   value_specification
                   kind_definition
                   include_module)
  value_definition (:seq
                    (:choice
                     (:seq
                      "let"
                      (:choice _attribute :blank)
                      (:choice "mutable" :blank)
                      (:choice "rec" :blank))
                     let_operator)
                    (:seq let_binding (:repeat (:seq (:choice "and" let_and_operator) let_binding))))
  let_binding (:seq
               (:choice
                (:field :pattern _binding_pattern_no_exn)
                (:seq "(" (:seq _value_name _at_mode) ")"))
               (:choice
                (:seq
                 (:repeat _parameter)
                 (:choice _polymorphic_typed :blank)
                 (:choice _coerced :blank)
                 (:choice _at_mode :blank)
                 "="
                 (:field :body _sequence_expression))
                :blank)
               (:repeat item_attribute))
  _parameter (:choice parameter (:alias _parenthesized_abstract_type abstract_type))
  parameter (:choice
             (:field :pattern _simple_pattern)
             (:seq (:choice "~" "?") (:field :pattern _simple_value_pattern))
             (:seq (:seq _label (:token-immediate ":")) (:field :pattern _simple_pattern))
             (:seq
              (:choice "~" "?")
              "("
              (:repeat "local_")
              (:field :pattern _simple_value_pattern)
              (:choice _polymorphic_typed :blank)
              (:choice _at_mode :blank)
              (:choice (:seq "=" (:field :default _sequence_expression)) :blank)
              ")")
             (:seq
              (:seq _label (:token-immediate ":"))
              "("
              (:field :pattern _pattern)
              (:choice _typed :blank)
              (:choice
               (:seq (:choice _at_mode :blank) (:seq "=" (:field :default _sequence_expression)))
               _at_mode)
              ")")
             (:seq
              (:choice (:seq _label (:token-immediate ":")) :blank)
              "("
              (:repeat1 "local_")
              (:field :pattern _pattern)
              (:choice _polymorphic_typed :blank)
              (:choice _at_mode :blank)
              (:choice (:seq "=" (:field :default _sequence_expression)) :blank)
              ")")
             (:seq
              (:seq _label (:token-immediate ":"))
              "("
              (:field :pattern _pattern)
              _strictly_polymorphic_typed
              (:choice _at_mode :blank)
              (:choice (:seq "=" (:field :default _sequence_expression)) :blank)
              ")")
             (:seq
              "("
              (:field :pattern _pattern)
              (:choice
               (:seq _strictly_polymorphic_typed (:choice _at_mode :blank))
               (:seq (:choice _typed :blank) _at_mode))
              ")"))
  external (:seq
            "external"
            (:choice _attribute :blank)
            _value_name
            _polymorphic_typed
            (:choice _at_at_modality :blank)
            "="
            (:repeat1 (:choice string quoted_string))
            (:repeat item_attribute))
  type_definition (:seq
                   "type"
                   (:choice _attribute :blank)
                   (:choice "nonrec" :blank)
                   (:seq type_binding (:repeat (:seq "and" type_binding))))
  type_binding (:seq
                (:choice _type_params :blank)
                (:choice
                 (:seq
                  (:field :name type_constructor)
                  (:choice _kind_annotation :blank)
                  (:choice
                   (:seq
                    (:choice "=" ":=")
                    (:choice
                     (:seq (:choice "private" :blank) (:field :body _type))
                     (:seq
                      (:choice (:seq (:field :synonym _type) "=") :blank)
                      (:choice "private" :blank)
                      (:field :body (:choice variant_declaration record_declaration "..")))))
                   :blank)
                  (:repeat type_constraint))
                 (:seq
                  (:field :name type_constructor)
                  (:choice _kind_annotation :blank)
                  "="
                  (:field :body external_declaration)
                  (:repeat type_constraint))
                 (:seq
                  (:field :name type_constructor_path)
                  (:seq "+=" (:choice "private" :blank) (:field :body variant_declaration))))
                (:repeat item_attribute))
  _type_params (:choice
                _type_param
                (:seq
                 "("
                 (:seq
                  (:seq _type_param (:choice _kind_annotation :blank))
                  (:repeat (:seq "," (:seq _type_param (:choice _kind_annotation :blank)))))
                 ")"))
  _type_param (:seq (:repeat (:choice "+" "-" "!")) _type_variable)
  variant_declaration (:choice
                       (:seq
                        "|"
                        (:choice
                         (:seq constructor_declaration (:repeat (:seq "|" constructor_declaration)))
                         :blank))
                       (:seq constructor_declaration (:repeat (:seq "|" constructor_declaration))))
  constructor_declaration (:seq
                           _constructor_name
                           (:choice
                            (:choice
                             (:seq "of" _constructor_argument)
                             (:seq
                              ":"
                              (:choice
                               (:seq (:repeat1 _maybe_kind_annotated_type_variable) ".")
                               :blank)
                              (:choice (:seq _constructor_argument "->") :blank)
                              _simple_type)
                             (:seq "=" _constructor_path))
                            :blank))
  _constructor_argument (:choice
                         (:seq
                          (:seq
                           (:choice "global_" :blank)
                           _simple_type
                           (:choice _at_at_modality :blank))
                          (:repeat
                           (:seq
                            "*"
                            (:seq
                             (:choice "global_" :blank)
                             _simple_type
                             (:choice _at_at_modality :blank)))))
                         record_declaration)
  record_declaration (:seq
                      (:choice "{" "#{")
                      (:seq field_declaration (:repeat (:seq ";" field_declaration)))
                      (:choice ";" :blank)
                      "}")
  field_declaration (:seq
                     (:choice (:choice "mutable" "global_") :blank)
                     _field_name
                     _polymorphic_typed
                     (:choice _at_at_modality :blank))
  external_declaration (:seq "external" (:choice string quoted_string))
  type_constraint (:seq "constraint" (:field :type _type) "=" (:field :constraint _type))
  exception_definition (:seq
                        "exception"
                        (:choice _attribute :blank)
                        constructor_declaration
                        (:repeat item_attribute))
  module_definition (:seq
                     "module"
                     (:choice _attribute :blank)
                     (:choice "rec" :blank)
                     (:seq module_binding (:repeat (:seq "and" module_binding))))
  module_binding (:seq
                  (:choice
                   _module_name
                   (:seq "(" (:seq _module_name (:choice _at_mode _at_at_modality)) ")"))
                  (:repeat module_parameter)
                  (:choice
                   (:seq
                    (:choice _module_typed :blank)
                    (:choice _at_mode :blank)
                    "="
                    (:field :body _module_expression))
                   (:seq _module_typed (:choice _at_mode :blank))
                   (:seq ":=" (:field :body extended_module_path)))
                  (:choice _at_at_modality :blank)
                  (:repeat item_attribute))
  module_parameter (:seq
                    "("
                    (:choice (:seq _module_name _module_typed (:choice _at_mode :blank)) :blank)
                    ")")
  module_type_definition (:seq
                          "module"
                          "type"
                          (:choice _attribute :blank)
                          _module_type_name
                          (:choice (:seq (:choice "=" ":=") (:field :body _module_type)) :blank)
                          (:repeat item_attribute))
  kind_definition (:seq
                   "kind_"
                   (:choice _attribute :blank)
                   _kind_name
                   (:choice (:seq "=" (:field :body _kind)) :blank)
                   (:repeat item_attribute))
  open_module (:seq
               "open"
               (:choice "!" :blank)
               (:choice _attribute :blank)
               (:field :module _module_expression)
               (:repeat item_attribute))
  include_module (:seq
                  _include_or_include_functor
                  (:choice _attribute :blank)
                  (:field :module _module_expression)
                  (:choice _at_at_modality :blank)
                  (:repeat item_attribute))
  _include_or_include_functor (:seq "include" (:choice "functor" :blank))
  class_definition (:seq
                    "class"
                    (:choice _attribute :blank)
                    (:seq class_binding (:repeat (:seq "and" class_binding))))
  class_binding (:seq
                 (:choice "virtual" :blank)
                 (:choice (:seq "[" (:seq _type_param (:repeat (:seq "," _type_param))) "]") :blank)
                 _class_name
                 (:repeat _parameter)
                 (:choice _class_typed :blank)
                 (:choice (:seq "=" (:field :body _class_expression)) :blank)
                 (:repeat item_attribute))
  class_type_definition (:seq
                         "class"
                         "type"
                         (:choice _attribute :blank)
                         (:seq class_type_binding (:repeat (:seq "and" class_type_binding))))
  class_type_binding (:seq
                      (:choice "virtual" :blank)
                      (:choice
                       (:seq "[" (:seq _type_param (:repeat (:seq "," _type_param))) "]")
                       :blank)
                      _class_type_name
                      "="
                      (:field :body _simple_class_type)
                      (:repeat item_attribute))
  _signature_item (:choice
                   value_specification
                   external
                   type_definition
                   exception_definition
                   module_definition
                   module_type_definition
                   kind_definition
                   open_module_signature
                   include_module_type
                   class_definition
                   class_type_definition
                   floating_attribute
                   _item_extension)
  value_specification (:seq
                       "val"
                       (:choice _attribute :blank)
                       _value_name
                       _polymorphic_typed
                       (:choice _at_at_modality :blank)
                       (:repeat item_attribute))
  open_module_signature (:seq
                         "open"
                         (:choice "!" :blank)
                         (:choice _attribute :blank)
                         (:field :module extended_module_path)
                         (:repeat item_attribute))
  include_module_type (:seq
                       _include_or_include_functor
                       (:choice _attribute :blank)
                       (:field :module_type _module_type)
                       (:repeat item_attribute)
                       (:choice _at_at_modality :blank))
  _module_typed (:seq ":" (:field :module_type _module_type))
  _module_type (:choice
                module_type_path
                signature
                module_type_constraint
                module_type_of
                functor_type
                parenthesized_module_type
                _extension)
  signature (:seq "sig" (:choice _signature :blank) "end")
  module_type_constraint (:prec-right 0
                          (:seq
                           (:field :module_type _module_type)
                           "with"
                           (:choice
                            extended_module_path
                            (:seq
                             (:choice
                              constrain_type
                              constrain_module
                              constrain_module_type
                              constrain_kind)
                             (:repeat
                              (:seq
                               "and"
                               (:choice
                                constrain_type
                                constrain_module
                                constrain_module_type
                                constrain_kind)))))))
  constrain_type (:seq
                  "type"
                  (:choice _type_params :blank)
                  type_constructor_path
                  (:choice "=" ":=")
                  (:choice "private" :blank)
                  (:field :equation _type)
                  (:repeat type_constraint))
  constrain_module (:seq
                    "module"
                    module_path
                    (:choice "=" ":=")
                    (:field :constraint extended_module_path))
  constrain_module_type (:prec-right 0
                         (:seq
                          "module"
                          "type"
                          module_type_path
                          (:choice "=" ":=")
                          (:field :constraint _module_type)))
  constrain_kind (:prec-left 0
                  (:seq "kind_" kind_path (:choice "=" ":=") (:field :constraint _kind)))
  module_type_of (:seq "module" "type" "of" (:field :module _module_expression))
  functor_type (:prec-dynamic 1
                (:prec-right 0
                 (:seq
                  (:choice
                   (:seq (:choice "functor" :blank) (:repeat1 module_parameter))
                   (:seq (:field :domain _module_type) (:choice _at_mode :blank)))
                  "->"
                  (:seq (:field :codomain _module_type) (:choice _at_mode :blank)))))
  parenthesized_module_type (:seq "(" _module_type ")")
  _simple_module_expression (:choice
                             typed_module_expression
                             parenthesized_module_expression
                             packed_module
                             _extension)
  _module_expression (:choice
                      _simple_module_expression
                      module_path
                      structure
                      functor
                      module_application)
  structure (:seq "struct" (:choice _structure :blank) "end")
  functor (:prec-right 0
           (:seq "functor" (:repeat1 module_parameter) "->" (:field :body _module_expression)))
  module_application (:seq
                      (:field :functor _module_expression)
                      (:choice (:field :argument _simple_module_expression) (:seq "(" ")")))
  typed_module_expression (:seq
                           "("
                           (:seq
                            (:field :module _module_expression)
                            (:choice (:seq _module_typed (:choice _at_mode :blank)) _at_mode))
                           ")")
  packed_module (:seq
                 "("
                 (:seq
                  "val"
                  (:field :value _expression)
                  (:choice _module_typed :blank)
                  (:choice (:seq ":>" (:field :coercion _module_type)) :blank))
                 ")")
  parenthesized_module_expression (:seq "(" _module_expression ")")
  _class_typed (:seq ":" (:field :class_type _class_type))
  _simple_class_type (:choice
                      class_type_path
                      instantiated_class_type
                      class_body_type
                      let_open_class_type
                      _extension)
  _class_type (:choice _simple_class_type class_function_type)
  instantiated_class_type (:seq "[" (:seq _type (:repeat (:seq "," _type))) "]" class_type_path)
  class_body_type (:seq
                   "object"
                   (:choice (:seq "(" (:field :self_type _type) ")") :blank)
                   (:repeat (:choice _class_field_specification floating_attribute))
                   "end")
  _class_field_specification (:choice
                              inheritance_specification
                              instance_variable_specification
                              method_specification
                              type_parameter_constraint
                              _item_extension)
  inheritance_specification (:seq
                             "inherit"
                             (:field :class_type _simple_class_type)
                             (:repeat item_attribute))
  instance_variable_specification (:seq
                                   "val"
                                   (:repeat (:choice "mutable" "virtual"))
                                   _instance_variable_name
                                   _typed
                                   (:repeat item_attribute))
  method_specification (:seq
                        "method"
                        (:repeat (:choice "private" "virtual"))
                        _method_name
                        _polymorphic_typed
                        (:repeat item_attribute))
  type_parameter_constraint (:seq
                             "constraint"
                             (:field :type _type)
                             "="
                             (:field :constraint _type)
                             (:repeat item_attribute))
  let_open_class_type (:seq "let" open_module "in" (:field :body _simple_class_type))
  class_function_type (:seq (:field :domain _argument_type) "->" (:field :codomain _class_type))
  _simple_class_expression (:choice
                            class_path
                            instantiated_class
                            object_expression
                            typed_class_expression
                            parenthesized_class_expression
                            _extension)
  _class_expression (:choice
                     _simple_class_expression
                     class_function
                     class_application
                     let_class_expression
                     let_open_class_expression)
  instantiated_class (:seq "[" (:seq _type (:repeat (:seq "," _type))) "]" class_path)
  typed_class_expression (:seq "(" (:seq (:field :class _class_expression) _class_typed) ")")
  class_function (:seq "fun" (:repeat1 _parameter) "->" (:field :body _class_expression))
  class_application (:seq
                     (:field :class _simple_class_expression)
                     (:repeat1 (:field :argument _argument)))
  let_class_expression (:seq value_definition "in" (:field :body _class_expression))
  _class_field (:choice
                inheritance_definition
                instance_variable_definition
                method_definition
                type_parameter_constraint
                class_initializer
                _item_extension)
  inheritance_definition (:seq
                          "inherit"
                          (:choice "!" :blank)
                          (:field :class _class_expression)
                          (:choice (:seq "as" (:field :alias _value_pattern)) :blank)
                          (:repeat item_attribute))
  instance_variable_definition (:seq
                                "val"
                                (:choice "!" :blank)
                                (:repeat (:choice "mutable" "virtual"))
                                _instance_variable_name
                                (:choice _type_constrained :blank)
                                (:choice (:seq "=" (:field :body _sequence_expression)) :blank)
                                (:repeat item_attribute))
  method_definition (:seq
                     "method"
                     (:choice "!" :blank)
                     (:repeat (:choice "private" "virtual"))
                     _method_name
                     (:repeat _parameter)
                     (:choice _polymorphic_typed :blank)
                     (:choice _coerced :blank)
                     (:choice (:seq "=" (:field :body _sequence_expression)) :blank)
                     (:repeat item_attribute))
  class_initializer (:seq
                     "initializer"
                     (:field :initializer _sequence_expression)
                     (:repeat item_attribute))
  let_open_class_expression (:seq "let" open_module "in" (:field :body _class_expression))
  parenthesized_class_expression (:seq "(" _class_expression ")")
  _typed (:seq ":" (:field :type _type))
  _simple_typed (:seq ":" (:field :type _simple_type))
  _strictly_polymorphic_typed (:seq ":" (:field :type (:alias _polymorphic_type polymorphic_type)))
  _polymorphic_typed (:choice _typed _strictly_polymorphic_typed)
  _coerced (:seq ":>" (:field :coercion _type))
  _type_constrained (:choice (:seq _typed (:choice _coerced :blank)) _coerced)
  _polymorphic_type (:seq
                     (:choice
                      (:repeat1 _maybe_kind_annotated_type_variable)
                      (:alias _abstract_type abstract_type))
                     "."
                     (:field :type _type))
  _parenthesized_polymorphic_type (:seq "(" _polymorphic_type ")")
  _abstract_type (:seq "type" (:choice (:seq _new_type _kind_annotation) (:repeat1 _new_type)))
  _new_type (:choice type_constructor (:seq "(" (:seq type_constructor _kind_annotation) ")"))
  _parenthesized_abstract_type (:seq "(" _abstract_type ")")
  _delimited_type (:choice
                   (:alias _unboxed_tuple_type tuple_type)
                   polymorphic_variant_type
                   package_type
                   kind_annotated_type_variable
                   parenthesized_type)
  _simple_type (:choice
                _delimited_type
                type_variable
                type_constructor_path
                constructed_type
                local_open_type
                hash_type
                object_type
                any_type
                _extension)
  _type (:choice
         _simple_type
         (:alias _proper_tuple_type tuple_type)
         (:alias _labeled_tuple_type tuple_type)
         function_type
         aliased_type)
  function_type (:prec-dynamic 1
                 (:prec-right 0
                  (:seq
                   (:field :domain (:choice _argument_type local_type))
                   (:choice _at_mode :blank)
                   "->"
                   (:field :codomain (:choice _type local_type))
                   (:choice _at_mode :blank))))
  _argument_type (:choice
                  _simple_type
                  (:alias _proper_tuple_type tuple_type)
                  labeled_argument_type
                  (:alias _parenthesized_polymorphic_type polymorphic_type))
  local_type (:seq
              (:repeat1 "local_")
              (:choice
               _simple_type
               (:alias _proper_tuple_type tuple_type)
               (:alias _labeled_tuple_type tuple_type)
               (:alias _parenthesized_polymorphic_type polymorphic_type)))
  labeled_argument_type (:seq
                         (:choice "?" :blank)
                         _label_name
                         ":"
                         (:field :type (:choice _argument_type local_type)))
  _proper_tuple_type (:prec-dynamic 1 (:seq _simple_type "*" _tuple_type_rhs))
  _labeled_tuple_type (:seq labeled_tuple_element_type "*" _tuple_type_rhs)
  labeled_tuple_element_type (:seq _label_name ":" (:field :type _simple_type))
  _tuple_type_rhs (:seq
                   (:choice _simple_type labeled_tuple_element_type)
                   (:choice (:seq "*" (:choice _tuple_type_rhs)) :blank))
  _unboxed_tuple_type (:seq "#(" (:choice _proper_tuple_type _labeled_tuple_type) ")")
  constructed_type (:seq
                    (:choice
                     _simple_type
                     (:seq
                      "("
                      (:seq
                       _type_or_kind_annotated_type_variable
                       (:repeat (:seq "," _type_or_kind_annotated_type_variable)))
                      ")"))
                    type_constructor_path)
  aliased_type (:seq (:field :type _type) "as" (:field :alias _maybe_kind_annotated_type_variable))
  local_open_type (:seq extended_module_path "." (:field :type _delimited_type))
  polymorphic_variant_type (:choice
                            (:seq "[" tag_specification "]")
                            (:seq
                             "["
                             (:choice _tag_spec :blank)
                             "|"
                             (:seq _tag_spec (:repeat (:seq "|" _tag_spec)))
                             "]")
                            (:seq
                             "[>"
                             (:choice "|" :blank)
                             (:choice (:seq _tag_spec (:repeat (:seq "|" _tag_spec))) :blank)
                             "]")
                            (:seq
                             "[<"
                             (:choice "|" :blank)
                             (:seq _tag_spec (:repeat (:seq "|" _tag_spec)))
                             (:choice (:seq ">" (:repeat1 tag)) :blank)
                             "]"))
  _tag_spec (:choice _type tag_specification)
  tag_specification (:seq
                     tag
                     (:choice
                      (:seq "of" (:choice "&" :blank) (:seq _type (:repeat (:seq "&" _type))))
                      :blank))
  package_type (:seq
                "("
                (:seq
                 "module"
                 (:choice _attribute :blank)
                 (:choice (:seq (:field :module _simple_module_name) ":") :blank)
                 (:field :module_type _module_type))
                ")")
  _anonymous_kind_annotated_type_variable (:seq (:choice _type_variable "type") _kind_annotation)
  kind_annotated_type_variable (:seq "(" _anonymous_kind_annotated_type_variable ")")
  _maybe_kind_annotated_type_variable (:choice _type_variable kind_annotated_type_variable)
  _type_or_kind_annotated_type_variable (:choice
                                         _type
                                         (:alias
                                          _anonymous_kind_annotated_type_variable
                                          kind_annotated_type_variable))
  object_type (:seq
               "<"
               (:choice
                (:choice
                 (:seq
                  (:seq
                   (:choice method_type _simple_type)
                   (:repeat (:seq ";" (:choice method_type _simple_type))))
                  (:choice (:seq ";" (:choice ".." :blank)) :blank))
                 "..")
                :blank)
               ">")
  method_type (:seq _method_name _polymorphic_typed)
  hash_type (:seq
             (:choice
              (:choice
               _simple_type
               (:seq
                "("
                (:seq
                 _type_or_kind_annotated_type_variable
                 (:repeat (:seq "," _type_or_kind_annotated_type_variable)))
                ")"))
              :blank)
             "#"
             class_type_path)
  any_type "_"
  parenthesized_type (:seq "(" _type ")")
  _delimited_expression (:choice
                         _extra_constructor
                         list_expression
                         array_expression
                         iarray_expression
                         record_expression
                         package_expression
                         object_copy_expression
                         parenthesized_expression)
  _simple_expression (:choice
                      _delimited_expression
                      value_path
                      _constant
                      typed_expression
                      _constructor_path
                      tag
                      (:alias _unboxed_tuple_expression tuple_expression)
                      prefix_expression
                      hash_expression
                      field_get_expression
                      array_get_expression
                      string_get_expression
                      bigarray_get_expression
                      block_index_expression
                      local_open_expression
                      new_expression
                      method_invocation
                      object_expression
                      hole_expression
                      ocamlyacc_value
                      _extension)
  _non_function_expression (:choice
                            _simple_expression
                            (:alias _tuple_expression tuple_expression)
                            cons_expression
                            application_expression
                            infix_expression
                            sign_expression
                            set_expression
                            if_expression
                            while_expression
                            for_expression
                            match_expression
                            fun_expression
                            try_expression
                            let_expression
                            assert_expression
                            lazy_expression
                            stack_expression
                            borrow_expression
                            local_expression
                            exclave_expression)
  _inline_expression (:choice
                      _non_function_expression
                      (:alias _stack_function_expression stack_expression)
                      function_expression)
  _expression _inline_expression
  _sequence_expression (:choice
                        _expression
                        (:alias _sequence_expression_anonymous sequence_expression))
  typed_expression (:seq
                    "("
                    (:seq
                     (:field :expression _sequence_expression)
                     (:choice
                      (:seq _type_constrained (:choice _at_mode :blank))
                      (:seq ":" _at_mode)))
                    ")")
  labeled_tuple_element (:choice
                         _tuple_label
                         (:seq
                          _tuple_label
                          (:token-immediate ":")
                          (:field :expression _simple_expression))
                         (:seq "~" "(" _label_name _type_constrained ")"))
  _tuple_expression (:prec-right "tuple"
                     (:seq
                      (:choice _inline_expression labeled_tuple_element)
                      ","
                      (:choice _inline_expression labeled_tuple_element _tuple_expression)))
  _unboxed_tuple_expression (:seq "#(" _tuple_expression ")")
  cons_expression (:prec-right "cons"
                   (:seq
                    (:field :left _non_function_expression)
                    "::"
                    (:field :right _inline_expression)))
  list_expression (:seq "[" _sequence_expression_content "]")
  array_expression (:seq "[|" (:choice _sequence_expression_content :blank) "|]")
  iarray_expression (:seq "[:" (:choice _sequence_expression_content :blank) ":]")
  _sequence_expression_content (:choice
                                (:seq
                                 (:seq _expression (:repeat (:seq ";" _expression)))
                                 (:choice ";" :blank))
                                comprehension)
  comprehension (:seq (:field :expression _expression) (:repeat1 _comprehension_clause))
  _comprehension_clause (:choice comprehension_iterator comprehension_guard)
  comprehension_iterator (:seq
                          "for"
                          (:seq comprehension_binding (:repeat (:seq "and" comprehension_binding))))
  comprehension_binding (:seq
                         (:field :name _pattern)
                         (:choice
                          (:seq
                           "="
                           (:field :from _expression)
                           (:choice "to" "downto")
                           (:field :to _expression))
                          (:seq "in" (:field :in _expression))))
  comprehension_guard (:seq "when" _expression)
  _sequence_content (:seq (:seq _expression (:repeat (:seq ";" _expression))) (:choice ";" :blank))
  record_expression (:seq
                     (:choice "{" "#{")
                     (:choice (:seq (:field :record _simple_expression) "with") :blank)
                     (:seq field_expression (:repeat (:seq ";" field_expression)))
                     (:choice ";" :blank)
                     "}")
  field_expression (:seq
                    field_path
                    (:choice _type_constrained :blank)
                    (:choice (:seq "=" (:field :body _expression)) :blank))
  application_expression (:prec "app"
                          (:seq
                           (:field :function _simple_expression)
                           (:repeat1 (:field :argument _argument))))
  _argument (:choice _simple_expression labeled_argument)
  labeled_argument (:choice
                    _label
                    (:seq _label (:token-immediate ":") (:field :expression _simple_expression))
                    (:seq (:choice "~" "?") "(" _label_name _type_constrained ")"))
  prefix_expression (:prec "prefix"
                     (:seq
                      (:field :operator prefix_operator)
                      (:field :expression _simple_expression)))
  sign_expression (:prec "sign"
                   (:seq (:field :operator sign_operator) (:field :expression _inline_expression)))
  hash_expression (:prec-left "hash"
                   (:seq
                    (:field :left _simple_expression)
                    (:field :operator hash_operator)
                    (:field :right _simple_expression)))
  infix_expression (:choice
                    (:prec-right "pow"
                     (:seq
                      (:field :left _non_function_expression)
                      (:field :operator pow_operator)
                      (:field :right _inline_expression)))
                    (:prec-left "mult"
                     (:seq
                      (:field :left _non_function_expression)
                      (:field :operator mult_operator)
                      (:field :right _inline_expression)))
                    (:prec-left "add"
                     (:seq
                      (:field :left _non_function_expression)
                      (:field :operator add_operator)
                      (:field :right _inline_expression)))
                    (:prec-right "concat"
                     (:seq
                      (:field :left _non_function_expression)
                      (:field :operator concat_operator)
                      (:field :right _inline_expression)))
                    (:prec-left "rel"
                     (:seq
                      (:field :left _non_function_expression)
                      (:field :operator rel_operator)
                      (:field :right _inline_expression)))
                    (:prec-right "and"
                     (:seq
                      (:field :left _non_function_expression)
                      (:field :operator and_operator)
                      (:field :right _inline_expression)))
                    (:prec-right "or"
                     (:seq
                      (:field :left _non_function_expression)
                      (:field :operator or_operator)
                      (:field :right _inline_expression)))
                    (:prec-right "assign"
                     (:seq
                      (:field :left _non_function_expression)
                      (:field :operator assign_operator)
                      (:field :right _inline_expression))))
  field_get_expression (:prec-left "dot"
                        (:seq
                         (:field :record _simple_expression)
                         (:choice "." ".#")
                         (:field :field field_path)))
  _indexing_prefix (:prec "dot"
                    (:seq
                     (:field :sequence _simple_expression)
                     (:choice "." indexing_operator_path)))
  array_get_expression (:seq _indexing_prefix "(" (:field :index _sequence_expression) ")")
  string_get_expression (:seq _indexing_prefix "[" (:field :index _sequence_expression) "]")
  bigarray_get_expression (:seq _indexing_prefix "{" (:field :index _sequence_expression) "}")
  set_expression (:prec "set"
                  (:seq
                   (:choice
                    field_get_expression
                    array_get_expression
                    string_get_expression
                    bigarray_get_expression
                    _instance_variable_name)
                   "<-"
                   (:field :body _inline_expression)))
  block_index_expression (:seq "(" (:repeat1 block_access) ")")
  block_access (:choice
                (:seq (:choice "." ".#") (:field :field field_path))
                (:seq "." _block_access_type (:seq "(" (:field :index _sequence_expression) ")")))
  if_expression (:prec-right 0
                 (:seq
                  "if"
                  (:choice _attribute :blank)
                  (:field :condition _sequence_expression)
                  then_clause
                  (:choice else_clause :blank)))
  then_clause (:prec "if" (:seq "then" (:field :expression _inline_expression)))
  else_clause (:prec "if" (:seq "else" (:field :expression _inline_expression)))
  while_expression (:seq
                    "while"
                    (:choice _attribute :blank)
                    (:field :condition _sequence_expression)
                    do_clause)
  do_clause (:seq "do" (:choice _sequence_expression :blank) "done")
  for_expression (:seq
                  "for"
                  (:choice _attribute :blank)
                  (:field :name _pattern)
                  "="
                  (:field :from _sequence_expression)
                  (:choice "to" "downto")
                  (:field :to _sequence_expression)
                  do_clause)
  _sequence_expression_anonymous (:prec-right "seq"
                                  (:seq
                                   _expression
                                   ";"
                                   (:choice
                                    (:seq
                                     (:choice _attribute :blank)
                                     (:choice _expression _sequence_expression_anonymous))
                                    :blank)))
  match_expression (:seq
                    (:choice (:seq "match" (:choice _attribute :blank)) match_operator)
                    (:field :expression _sequence_expression)
                    "with"
                    _match_cases)
  _match_cases (:prec-right 0
                (:seq (:choice "|" :blank) (:seq match_case (:repeat (:seq "|" match_case)))))
  match_case (:seq
              (:field :pattern _pattern)
              (:choice guard :blank)
              "->"
              (:field :body (:choice _sequence_expression refutation_case)))
  guard (:seq "when" (:field :expression _sequence_expression))
  refutation_case "."
  function_expression (:seq "function" (:choice _attribute :blank) _match_cases)
  fun_expression (:seq
                  "fun"
                  (:choice _attribute :blank)
                  (:repeat1 _parameter)
                  (:choice (:choice _simple_typed _at_mode) :blank)
                  "->"
                  (:field :body _sequence_expression))
  try_expression (:seq
                  "try"
                  (:choice _attribute :blank)
                  (:field :expression _sequence_expression)
                  "with"
                  _match_cases)
  let_expression (:seq
                  (:choice
                   value_definition
                   (:seq "let" (:choice _attribute :blank) _local_structure_item))
                  "in"
                  (:field :body _sequence_expression))
  assert_expression (:seq
                     "assert"
                     (:choice _attribute :blank)
                     (:field :expression _simple_expression))
  lazy_expression (:seq "lazy" (:choice _attribute :blank) (:field :expression _simple_expression))
  stack_expression (:prec "app" (:seq "stack_" (:field :expression _non_function_expression)))
  _stack_function_expression (:prec "app" (:seq "stack_" (:field :expression function_expression)))
  borrow_expression (:seq "borrow_" (:field :expression _simple_expression))
  local_expression (:seq "local_" (:field :expression _sequence_expression))
  exclave_expression (:seq "exclave_" (:field :expression _sequence_expression))
  local_open_expression (:seq module_path "." (:field :expression _delimited_expression))
  package_expression (:seq
                      "("
                      (:seq
                       "module"
                       (:choice _attribute :blank)
                       (:field :module _module_expression)
                       (:choice _module_typed :blank))
                      ")")
  new_expression (:seq "new" (:choice _attribute :blank) class_path)
  object_copy_expression (:seq
                          "{<"
                          (:choice
                           (:seq
                            instance_variable_expression
                            (:repeat (:seq ";" instance_variable_expression)))
                           :blank)
                          (:choice ";" :blank)
                          ">}")
  instance_variable_expression (:seq
                                _instance_variable_name
                                (:choice (:seq "=" (:field :expression _expression)) :blank))
  method_invocation (:prec "method"
                     (:seq (:field :object _simple_expression) "#" (:field :method _method_name)))
  object_expression (:seq
                     "object"
                     (:choice _attribute :blank)
                     (:choice
                      (:seq "(" (:seq (:field :self _pattern) (:choice _typed :blank)) ")")
                      :blank)
                     (:repeat (:choice _class_field floating_attribute))
                     "end")
  parenthesized_expression (:choice
                            (:seq
                             "begin"
                             (:choice _attribute :blank)
                             (:field :expression _sequence_expression)
                             "end")
                            (:seq "(" (:field :expression _sequence_expression) ")"))
  hole_expression "_"
  ocamlyacc_value (:pattern "\\$[0-9]+")
  _delimited_pattern (:choice
                      _extra_constructor
                      record_pattern
                      list_pattern
                      array_pattern
                      iarray_pattern
                      parenthesized_pattern
                      (:alias _unboxed_tuple_pattern tuple_pattern))
  _simple_pattern (:choice
                   _delimited_pattern
                   _value_pattern
                   _signed_constant
                   typed_pattern
                   _constructor_path
                   tag
                   polymorphic_variant_pattern
                   range_pattern
                   local_open_pattern
                   package_pattern
                   any_pattern
                   _extension)
  _effect_pattern (:choice _simple_pattern constructor_pattern tag_pattern lazy_pattern)
  _pattern (:choice
            _effect_pattern
            alias_pattern
            (:alias _or_pattern_anonymous or_pattern)
            (:alias _tuple_pattern tuple_pattern)
            cons_pattern
            exception_pattern
            effect_pattern)
  _delimited_binding_pattern (:choice
                              _extra_constructor
                              (:alias record_binding_pattern record_pattern)
                              (:alias list_binding_pattern list_pattern)
                              (:alias array_binding_pattern array_pattern)
                              (:alias iarray_binding_pattern iarray_pattern)
                              (:alias parenthesized_binding_pattern parenthesized_pattern)
                              (:alias _unboxed_tuple_binding_pattern tuple_pattern))
  _simple_binding_pattern (:choice
                           _delimited_binding_pattern
                           _value_name
                           _signed_constant
                           (:alias typed_binding_pattern typed_pattern)
                           _constructor_path
                           tag
                           polymorphic_variant_pattern
                           range_pattern
                           (:alias local_open_binding_pattern local_open_pattern)
                           package_pattern
                           any_pattern
                           _extension)
  _effect_binding_pattern (:choice
                           _simple_binding_pattern
                           (:alias constructor_binding_pattern constructor_pattern)
                           (:alias tag_binding_pattern tag_pattern)
                           (:alias lazy_binding_pattern lazy_pattern))
  _binding_pattern_no_exn (:choice
                           _effect_binding_pattern
                           (:alias alias_binding_pattern_no_exn alias_pattern)
                           (:alias _or_binding_pattern_no_exn_anonymous or_pattern)
                           (:alias _tuple_binding_pattern_no_exn tuple_pattern)
                           (:alias cons_binding_pattern_no_exn cons_pattern))
  _binding_pattern (:choice
                    _effect_binding_pattern
                    (:alias alias_binding_pattern alias_pattern)
                    (:alias _or_binding_pattern_anonymous or_pattern)
                    (:alias _tuple_binding_pattern tuple_pattern)
                    (:alias cons_binding_pattern cons_pattern)
                    (:alias exception_binding_pattern exception_pattern)
                    (:alias effect_binding_pattern effect_pattern))
  alias_pattern (:prec "alias_pattern"
                 (:seq (:field :pattern _pattern) "as" (:field :alias _value_pattern)))
  alias_binding_pattern_no_exn (:prec "alias_pattern"
                                (:seq
                                 (:field :pattern _binding_pattern_no_exn)
                                 "as"
                                 (:field :alias _value_name)))
  alias_binding_pattern (:prec "alias_pattern"
                         (:seq (:field :pattern _binding_pattern) "as" (:field :alias _value_name)))
  typed_pattern (:seq "(" (:seq (:field :pattern _pattern) _typed) ")")
  typed_binding_pattern (:seq "(" (:seq (:field :pattern _binding_pattern) _typed) ")")
  _or_pattern_anonymous (:prec-right "or_pattern"
                         (:seq _pattern "|" (:choice _pattern _or_pattern_anonymous)))
  _or_binding_pattern_no_exn_anonymous (:prec-right "or_pattern"
                                        (:seq
                                         _binding_pattern_no_exn
                                         "|"
                                         (:choice _binding_pattern _or_binding_pattern_anonymous)))
  _or_binding_pattern_anonymous (:prec-right "or_pattern"
                                 (:seq
                                  _binding_pattern
                                  "|"
                                  (:choice _binding_pattern _or_binding_pattern_anonymous)))
  constructor_pattern (:prec "constructor_pattern"
                       (:seq
                        _constructor_path
                        (:choice (:alias _parenthesized_abstract_type abstract_type) :blank)
                        (:field :pattern _pattern)))
  constructor_binding_pattern (:prec "constructor_pattern"
                               (:seq
                                _constructor_path
                                (:choice (:alias _parenthesized_abstract_type abstract_type) :blank)
                                (:field :pattern _binding_pattern)))
  tag_pattern (:prec "constructor_pattern" (:seq tag (:field :pattern _pattern)))
  tag_binding_pattern (:prec "constructor_pattern" (:seq tag (:field :pattern _binding_pattern)))
  polymorphic_variant_pattern (:seq "#" type_constructor_path)
  labeled_tuple_element_pattern (:choice
                                 _tuple_label
                                 (:seq
                                  _tuple_label
                                  (:token-immediate ":")
                                  (:field :pattern _simple_pattern))
                                 (:seq "~" "(" _label_name _typed ")"))
  _tuple_pattern (:prec-right "tuple_pattern"
                  (:seq
                   (:choice _pattern labeled_tuple_element_pattern)
                   ","
                   (:choice _pattern labeled_tuple_element_pattern _tuple_pattern "..")))
  labeled_tuple_element_binding_pattern (:choice
                                         _tuple_label
                                         (:seq
                                          _tuple_label
                                          (:token-immediate ":")
                                          (:field :pattern _simple_binding_pattern))
                                         (:seq "~" "(" _label_name _typed ")"))
  _tuple_binding_pattern_no_exn (:prec-right "tuple_pattern"
                                 (:seq
                                  (:choice
                                   _binding_pattern_no_exn
                                   labeled_tuple_element_binding_pattern)
                                  ","
                                  (:choice
                                   _binding_pattern
                                   labeled_tuple_element_binding_pattern
                                   _tuple_binding_pattern
                                   "..")))
  _tuple_binding_pattern (:prec-right "tuple_pattern"
                          (:seq
                           (:choice _binding_pattern labeled_tuple_element_binding_pattern)
                           ","
                           (:choice
                            _binding_pattern
                            labeled_tuple_element_binding_pattern
                            _tuple_binding_pattern
                            "..")))
  _unboxed_tuple_pattern (:seq "#(" _tuple_pattern ")")
  _unboxed_tuple_binding_pattern (:seq "#(" _tuple_binding_pattern ")")
  record_pattern (:seq
                  (:choice "{" "#{")
                  (:seq field_pattern (:repeat (:seq ";" field_pattern)))
                  (:choice (:seq ";" "_") :blank)
                  (:choice ";" :blank)
                  "}")
  field_pattern (:seq
                 field_path
                 (:choice _typed :blank)
                 (:choice (:seq "=" (:field :pattern _pattern)) :blank))
  record_binding_pattern (:seq
                          (:choice "{" "#{")
                          (:seq
                           (:alias field_binding_pattern field_pattern)
                           (:repeat (:seq ";" (:alias field_binding_pattern field_pattern))))
                          (:choice (:seq ";" "_") :blank)
                          (:choice ";" :blank)
                          "}")
  field_binding_pattern (:seq
                         field_path
                         (:choice _typed :blank)
                         (:choice (:seq "=" (:field :pattern _binding_pattern)) :blank))
  list_pattern (:seq "[" _sequence_pattern_content "]")
  list_binding_pattern (:seq "[" _sequence_binding_pattern_content "]")
  cons_pattern (:prec-right "cons_pattern"
                (:seq (:field :left _pattern) "::" (:field :right _pattern)))
  cons_binding_pattern_no_exn (:prec-right "cons_pattern"
                               (:seq
                                (:field :left _binding_pattern_no_exn)
                                "::"
                                (:field :right _binding_pattern)))
  cons_binding_pattern (:prec-right "cons_pattern"
                        (:seq (:field :left _binding_pattern) "::" (:field :right _binding_pattern)))
  array_pattern (:seq "[|" (:choice _sequence_pattern_content :blank) "|]")
  array_binding_pattern (:seq "[|" (:choice _sequence_binding_pattern_content :blank) "|]")
  iarray_pattern (:seq "[:" (:choice _sequence_pattern_content :blank) ":]")
  iarray_binding_pattern (:seq "[:" (:choice _sequence_binding_pattern_content :blank) ":]")
  _sequence_pattern_content (:seq
                             (:seq _pattern (:repeat (:seq ";" _pattern)))
                             (:choice ";" :blank))
  _sequence_binding_pattern_content (:seq
                                     (:seq _binding_pattern (:repeat (:seq ";" _binding_pattern)))
                                     (:choice ";" :blank))
  range_pattern (:prec "range_pattern"
                 (:seq (:field :left _signed_constant) ".." (:field :right _signed_constant)))
  lazy_pattern (:prec "constructor_pattern"
                (:seq "lazy" (:choice _attribute :blank) (:field :pattern _pattern)))
  lazy_binding_pattern (:prec "constructor_pattern"
                        (:seq "lazy" (:choice _attribute :blank) (:field :pattern _binding_pattern)))
  local_open_pattern (:seq module_path "." (:field :pattern _delimited_pattern))
  local_open_binding_pattern (:seq module_path "." (:field :pattern _delimited_binding_pattern))
  package_pattern (:seq
                   "("
                   (:seq
                    "module"
                    (:choice _attribute :blank)
                    _module_name
                    (:choice _module_typed :blank))
                   ")")
  any_pattern "_"
  parenthesized_pattern (:seq "(" _pattern ")")
  parenthesized_binding_pattern (:seq "(" _binding_pattern ")")
  exception_pattern (:prec "constructor_pattern"
                     (:seq "exception" (:choice _attribute :blank) (:field :pattern _pattern)))
  exception_binding_pattern (:prec "constructor_pattern"
                             (:seq
                              "exception"
                              (:choice _attribute :blank)
                              (:field :pattern _binding_pattern)))
  effect_pattern (:seq
                  "effect"
                  (:field :effect _effect_pattern)
                  ","
                  (:field :continuation _simple_pattern))
  effect_binding_pattern (:seq
                          "effect"
                          (:field :effect _effect_binding_pattern)
                          ","
                          (:field :continuation _simple_binding_pattern))
  attribute (:seq
             (:alias (:pattern "\\[@") "[@")
             attribute_id
             (:choice attribute_payload :blank)
             "]")
  item_attribute (:seq "[@@" attribute_id (:choice attribute_payload :blank) "]")
  floating_attribute (:seq "[@@@" attribute_id (:choice attribute_payload :blank) "]")
  attribute_payload (:choice
                     _structure
                     (:seq ":" (:choice (:choice _type _signature) :blank))
                     (:seq "?" _pattern (:choice guard :blank)))
  _extension (:choice extension quoted_extension)
  extension (:seq "[%" attribute_id (:choice attribute_payload :blank) "]")
  quoted_extension (:seq "{%" attribute_id (:choice (:pattern "\\s+") :blank) _quoted_string "}")
  _item_extension (:choice item_extension quoted_item_extension)
  item_extension (:seq
                  "[%%"
                  attribute_id
                  (:choice attribute_payload :blank)
                  "]"
                  (:repeat item_attribute))
  quoted_item_extension (:seq
                         "{%%"
                         attribute_id
                         (:choice (:pattern "\\s+") :blank)
                         _quoted_string
                         "}"
                         (:repeat item_attribute))
  _attribute (:seq "%" attribute_id)
  _constant (:choice
             number
             character
             string
             quoted_string
             (:alias _unboxed_boolean boolean)
             (:alias _unboxed_unit unit))
  _signed_constant (:choice _constant signed_number)
  number (:token
          (:choice
           (:pattern "#?[0-9][0-9_]*(\\.[0-9_]*)?([eE][+\\-]?[0-9][0-9_]*)?[g-zG-Z]?")
           (:pattern "#?0[xX][0-9A-Fa-f][0-9A-Fa-f_]*(\\.[0-9A-Fa-f_]*)?([pP][+\\-]?[0-9][0-9_]*)?[g-zG-Z]?")
           (:pattern "#?0[oO][0-7][0-7_]*[g-zG-Z]?")
           (:pattern "#?0[bB][01][01_]*[g-zG-Z]?")))
  signed_number (:seq
                 (:pattern "[+-]")
                 (:token
                  (:choice
                   (:pattern "#?[0-9][0-9_]*(\\.[0-9_]*)?([eE][+\\-]?[0-9][0-9_]*)?[g-zG-Z]?")
                   (:pattern "#?0[xX][0-9A-Fa-f][0-9A-Fa-f_]*(\\.[0-9A-Fa-f_]*)?([pP][+\\-]?[0-9][0-9_]*)?[g-zG-Z]?")
                   (:pattern "#?0[oO][0-7][0-7_]*[g-zG-Z]?")
                   (:pattern "#?0[bB][01][01_]*[g-zG-Z]?"))))
  character (:seq (:choice "'" "#'") character_content (:token-immediate "'"))
  character_content (:choice
                     (:token-immediate (:pattern "\\r*\\n"))
                     (:token-immediate (:pattern "[^\\\\'\\r\\n]"))
                     _null
                     escape_sequence)
  string (:seq "\"" (:choice string_content :blank) "\"")
  string_content (:repeat1
                  (:choice
                   (:token-immediate (:pattern "\\s"))
                   (:token-immediate (:pattern "\\[@"))
                   (:pattern "[^\\\\\"%@]+|%|@")
                   _null
                   escape_sequence
                   (:alias (:pattern "\\\\u\\{[0-9A-Fa-f]+\\}") escape_sequence)
                   (:alias (:pattern "\\\\\\r*\\n[\\t ]*") escape_sequence)
                   conversion_specification
                   pretty_printing_indication))
  quoted_string (:seq "{" _quoted_string "}")
  _quoted_string (:seq
                  _left_quoted_string_delimiter
                  (:choice quoted_string_content :blank)
                  _right_quoted_string_delimiter)
  quoted_string_content (:repeat1
                         (:choice
                          (:token-immediate (:pattern "\\s"))
                          (:token-immediate (:pattern "\\[@"))
                          (:pattern "[^%@|]+|%|@|\\|")
                          _null
                          conversion_specification
                          pretty_printing_indication))
  escape_sequence (:choice
                   (:pattern "\\\\[\\\\\"'ntbr ]")
                   (:pattern "\\\\[0-9][0-9][0-9]")
                   (:pattern "\\\\x[0-9A-Fa-f][0-9A-Fa-f]")
                   (:pattern "\\\\o[0-3][0-7][0-7]"))
  conversion_specification (:token
                            (:seq
                             "%"
                             (:choice (:pattern "[\\-0+ #]") :blank)
                             (:choice (:pattern "[1-9][0-9]*|\\*") :blank)
                             (:choice (:pattern "\\.([0-9]*|\\*)") :blank)
                             (:choice
                              (:pattern "[diunlLNxXosScCfFeEgGhHbBat!%@,]")
                              (:pattern "[lnL][diuxXo]"))))
  pretty_printing_indication (:pattern "@([\\[\\], ;.{}?]|\\\\n|<[0-9]+>)")
  _unboxed_boolean (:choice "#true" "#false")
  _unboxed_unit (:seq "#(" ")")
  prefix_operator (:token
                   (:choice
                    (:seq
                     "!"
                     (:choice
                      (:choice (:pattern "[#!$%&*+\\-./:<>?@^|~]") :blank)
                      (:seq
                       (:pattern "[#!$%&*+\\-./:<=>?@^|~]")
                       (:repeat1 (:pattern "[#!$%&*+\\-./:<=>?@^|~]")))))
                    (:seq (:pattern "[~?]") (:repeat1 (:pattern "[#!$%&*+\\-./:<=>?@^|~]")))))
  sign_operator (:choice (:pattern "[+-]") (:pattern "[+-]\\."))
  _infix_operator (:choice
                   pow_operator
                   mult_operator
                   add_operator
                   concat_operator
                   rel_operator
                   and_operator
                   or_operator
                   assign_operator)
  hash_operator (:token (:seq "#" (:repeat1 (:pattern "[#!$%&*+\\-./:<=>?@^|~]"))))
  pow_operator (:choice
                "lsl"
                "lsr"
                "asr"
                (:token (:seq "**" (:repeat (:pattern "[!$%&*+\\-./:<=>?@^|~]")))))
  mult_operator (:choice
                 "mod"
                 "land"
                 "lor"
                 "lxor"
                 (:token (:seq (:pattern "[*/%]") (:repeat (:pattern "[!$%&*+\\-./:<=>?@^|~]")))))
  add_operator (:choice
                (:pattern "[+-]")
                (:pattern "[+-]\\.")
                (:token
                 (:choice
                  (:seq "+" (:repeat1 (:pattern "[!$%&*+\\-./:<=>?@^|~]")))
                  (:seq
                   "-"
                   (:choice
                    (:repeat1 (:pattern "[!$%&*+\\-./:<=?@^|~]"))
                    (:seq
                     (:pattern "[!$%&*+\\-./:<=>?@^|~]")
                     (:repeat1 (:pattern "[!$%&*+\\-./:<=>?@^|~]"))))))))
  concat_operator (:token (:seq (:pattern "[@^]") (:repeat (:pattern "[!$%&*+\\-./:<=>?@^|~]"))))
  rel_operator (:token
                (:choice
                 (:seq (:pattern "[=>$]") (:repeat (:pattern "[!$%&*+\\-./:<=>?@^|~]")))
                 (:seq
                  "<"
                  (:choice
                   (:choice (:pattern "[!$%&*+./:<=>?@^|~]") :blank)
                   (:seq
                    (:pattern "[!$%&*+\\-./:<=>?@^|~]")
                    (:repeat1 (:pattern "[!$%&*+\\-./:<=>?@^|~]")))))
                 (:seq
                  "&"
                  (:choice
                   (:pattern "[!$%*+\\-./:<=>?@^|~]")
                   (:seq
                    (:pattern "[!$%&*+\\-./:<=>?@^|~]")
                    (:repeat1 (:pattern "[!$%&*+\\-./:<=>?@^|~]")))))
                 (:seq
                  "|"
                  (:choice
                   (:pattern "[!$%&*+\\-./:<=>?@^~]")
                   (:seq
                    (:pattern "[!$%&*+\\-./:<=>?@^|~]")
                    (:repeat1 (:pattern "[!$%&*+\\-./:<=>?@^|~]")))))
                 "!="))
  and_operator (:choice "&" "&&")
  or_operator (:choice "or" "||")
  assign_operator (:pattern ":=")
  indexing_operator (:token
                     (:seq
                      "."
                      (:pattern "[!$%&*+\\-/:=>?@^|]")
                      (:repeat (:pattern "[!$%&*+\\-./:<=>?@^|~]"))))
  indexing_operator_path (:choice indexing_operator (:seq "." module_path indexing_operator))
  let_operator (:token
                (:seq
                 "let"
                 (:pattern "[$&*+\\-/<=>@^|]")
                 (:repeat (:pattern "[!$%&*+\\-./:<=>?@^|~]"))))
  let_and_operator (:token
                    (:seq
                     "and"
                     (:pattern "[$&*+\\-/<=>@^|]")
                     (:repeat (:pattern "[!$%&*+\\-./:<=>?@^|~]"))))
  match_operator (:token
                  (:seq
                   "match"
                   (:pattern "[$&*+\\-/<=>@^|]")
                   (:repeat (:pattern "[!$%&*+\\-./:<=>?@^|~]"))))
  _at_mode (:seq "@" (:field :mode (:repeat1 _mode)))
  _at_at_modality (:seq "@@" (:field :modality (:repeat1 _modality)))
  _kind_annotation (:seq ":" (:field :kind _kind))
  _kind (:choice
         kind_path
         mod_bounded_kind
         with_bounded_kind
         scannable_axes_kind
         kind_of_kind
         (:alias _product_kind product_kind)
         any_kind
         parenthesized_kind)
  mod_bounded_kind (:seq _kind "mod" (:repeat1 _mode))
  with_bounded_kind (:prec-left 0 (:seq _kind "with" _type (:choice _at_at_modality :blank)))
  scannable_axes_kind (:seq (:choice kind_path parenthesized_kind) (:repeat1 _scannable_axis))
  kind_of_kind (:seq "kind_of_" _type)
  _product_kind (:prec-right 0 (:seq _kind "&" (:choice _product_kind _kind)))
  any_kind "_"
  parenthesized_kind (:seq "(" _kind ")")
  _value_name (:choice (:alias _lowercase_identifier value_name) parenthesized_operator)
  _simple_value_pattern (:alias _lowercase_identifier value_pattern)
  _value_pattern (:choice _simple_value_pattern parenthesized_operator)
  parenthesized_operator (:seq
                          "("
                          (:choice
                           prefix_operator
                           _infix_operator
                           hash_operator
                           (:seq
                            indexing_operator
                            (:choice
                             (:seq "(" (:choice (:seq ";" "..") :blank) ")")
                             (:seq "[" (:choice (:seq ";" "..") :blank) "]")
                             (:seq "{" (:choice (:seq ";" "..") :blank) "}"))
                            (:choice "<-" :blank))
                           let_operator
                           let_and_operator
                           match_operator)
                          ")")
  value_path (:choice _value_name (:seq module_path "." _value_name))
  module_path (:choice _simple_module_name (:seq module_path "." _simple_module_name))
  extended_module_path (:choice
                        (:choice
                         _simple_module_name
                         (:seq extended_module_path "." _simple_module_name))
                        (:seq extended_module_path (:seq "(" extended_module_path ")")))
  module_type_path (:choice _module_type_name (:seq extended_module_path "." _module_type_name))
  field_path (:choice _field_name (:seq module_path "." _field_name))
  constructor_path (:choice
                    _simple_constructor_name
                    (:seq module_path "." _simple_constructor_name))
  _constructor_path (:choice constructor_path _extra_constructor)
  type_constructor_path (:choice type_constructor (:seq extended_module_path "." type_constructor))
  class_path (:choice _class_name (:seq module_path "." _class_name))
  class_type_path (:choice _class_type_name (:seq extended_module_path "." _class_type_name))
  kind_path (:choice _kind_name (:seq extended_module_path "." _kind_name))
  _label_name (:alias _lowercase_identifier label_name)
  _field_name (:alias _lowercase_identifier field_name)
  _class_name (:alias _lowercase_identifier class_name)
  _class_type_name (:alias _lowercase_identifier class_type_name)
  _method_name (:alias _lowercase_identifier method_name)
  type_constructor (:seq _lowercase_identifier (:choice (:token-immediate "#") :blank))
  _instance_variable_name (:alias _lowercase_identifier instance_variable_name)
  _mode (:alias _lowercase_identifier mode)
  _modality (:alias _lowercase_identifier modality)
  _kind_name (:alias _lowercase_identifier kind_name)
  _scannable_axis (:alias _lowercase_identifier scannable_axis)
  _simple_module_name (:alias _uppercase_identifier module_name)
  _module_name (:choice _simple_module_name (:alias "_" module_name))
  _module_type_name (:alias (:choice _uppercase_identifier _lowercase_identifier) module_type_name)
  _simple_constructor_name (:choice
                            (:alias _uppercase_identifier constructor_name)
                            (:seq "(" (:alias "::" constructor_name) ")"))
  _constructor_name (:choice _simple_constructor_name _extra_constructor)
  _block_access_type (:alias
                      (:choice _uppercase_identifier _lowercase_identifier)
                      block_access_type)
  type_variable (:seq (:pattern "'") (:choice _lowercase_identifier _uppercase_identifier))
  _type_variable (:choice type_variable (:alias "_" type_variable))
  _extra_constructor (:choice unit boolean empty_list)
  unit (:choice (:seq "(" ")") (:seq "begin" (:choice _attribute :blank) "end"))
  boolean (:choice "true" "false")
  empty_list (:seq "[" "]")
  _lowercase_identifier (:pattern "(\\\\#)?[\\p{Ll}_][\\p{XID_Continue}']*")
  _uppercase_identifier (:pattern "[\\p{Lu}][\\p{XID_Continue}']*")
  _label (:seq (:choice "~" "?") _label_name)
  _tuple_label (:seq "~" _label_name)
  directive (:seq (:pattern "#") (:choice _lowercase_identifier _uppercase_identifier))
  tag (:seq (:pattern "`") (:choice _lowercase_identifier _uppercase_identifier))
  attribute_id (:seq
                (:choice (:reserved :attribute_id _lowercase_identifier) _uppercase_identifier)
                (:repeat
                 (:seq
                  (:pattern "\\.")
                  (:choice (:reserved :attribute_id _lowercase_identifier) _uppercase_identifier))))}}
