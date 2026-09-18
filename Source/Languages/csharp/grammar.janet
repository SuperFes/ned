# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "c_sharp"
 :word _identifier_token
 :extras [(:pattern "[\\s\\u00A0\\uFEFF\\u3000]+")
          comment
          preproc_region
          preproc_endregion
          preproc_line
          preproc_pragma
          preproc_nullable
          preproc_error
          preproc_warning
          preproc_define
          preproc_undef]
 :conflicts [[_simple_name generic_name]
             [_simple_name type_parameter]
             [_simple_name subpattern]
             [tuple_element type_pattern]
             [tuple_element using_variable_declarator]
             [tuple_element declaration_expression]
             [tuple_pattern parameter]
             [tuple_pattern _simple_name]
             [lvalue_expression _name]
             [parameter lvalue_expression]
             [type attribute]
             [type nullable_type]
             [type nullable_type array_creation_expression]
             [type _array_base_type]
             [type _array_base_type array_creation_expression]
             [type array_creation_expression]
             [type _pointer_base_type]
             [qualified_name member_access_expression]
             [qualified_name explicit_interface_specifier]
             [_array_base_type stackalloc_expression]
             [constant_pattern non_lvalue_expression]
             [constant_pattern _expression_statement_expression]
             [constant_pattern lvalue_expression]
             [constant_pattern _name]
             [constant_pattern lvalue_expression _name]
             [type _name_invocation_pattern recursive_pattern]
             [attribute type _name_invocation_pattern recursive_pattern]
             [parenthesized_pattern _parenthesized_pattern_with_designation]
             [expression_element argument]
             [spread_element range_expression]
             [collection_expression list_pattern]
             [_reserved_identifier modifier]
             [_reserved_identifier scoped_type]
             [_reserved_identifier implicit_type]
             [_reserved_identifier from_clause]
             [_reserved_identifier implicit_type var_pattern]
             [_reserved_identifier type_parameter_constraint]
             [_reserved_identifier parameter scoped_type]
             [_reserved_identifier parameter]
             [_simple_name parameter]
             [tuple_element parameter declaration_expression]
             [parameter tuple_element]
             [event_declaration variable_declarator]
             [base_list]
             [using_directive modifier]
             [using_directive]
             [_constructor_declaration_initializer _simple_name]]
 :precedences [[_anonymous_object_member_declarator _simple_name] [block initializer_expression]]
 :externals [_optional_semi
             interpolation_regular_start
             interpolation_verbatim_start
             interpolation_raw_start
             interpolation_start_quote
             interpolation_end_quote
             interpolation_open_brace
             interpolation_close_brace
             interpolation_string_content
             raw_string_start
             raw_string_end
             raw_string_content]
 :inline [_namespace_member_declaration
          _object_creation_type
          _nullable_base_type
          _parameter_type_with_modifiers
          _top_level_item_no_statement]
 :supertypes [declaration
              expression
              non_lvalue_expression
              lvalue_expression
              literal
              statement
              type
              type_declaration
              pattern]
 :rules
 {compilation_unit (:seq (:choice shebang_directive :blank) (:repeat _top_level_item))
  _top_level_item (:prec 2 (:choice _top_level_item_no_statement global_statement))
  _top_level_item_no_statement (:choice
                                extern_alias_directive
                                using_directive
                                global_attribute
                                (:alias preproc_if_in_top_level preproc_if)
                                _namespace_member_declaration
                                file_scoped_namespace_declaration)
  global_statement (:prec 1 statement)
  extern_alias_directive (:seq "extern" "alias" (:field :name identifier) ";")
  using_directive (:seq
                   (:choice "global" :blank)
                   "using"
                   (:choice
                    (:seq (:choice "unsafe" :blank) (:field :name identifier) "=" type)
                    (:seq (:repeat (:choice "static" "unsafe")) _name))
                   ";")
  global_attribute (:seq
                    "["
                    (:choice "assembly" "module")
                    ":"
                    (:seq attribute (:repeat (:seq "," attribute)))
                    (:choice "," :blank)
                    "]")
  attribute (:seq (:field :name _name) (:choice attribute_argument_list :blank))
  attribute_argument_list (:prec -1
                           (:seq
                            "("
                            (:choice
                             (:seq attribute_argument (:repeat (:seq "," attribute_argument)))
                             :blank)
                            ")"))
  attribute_argument (:prec -1
                      (:seq
                       (:choice (:prec 1 (:seq (:field :name identifier) (:choice ":" "="))) :blank)
                       expression))
  attribute_list (:seq
                  "["
                  (:choice attribute_target_specifier :blank)
                  (:seq attribute (:repeat (:seq "," attribute)))
                  (:choice "," :blank)
                  "]")
  _attribute_list (:choice attribute_list preproc_if_in_attribute_list)
  attribute_target_specifier (:seq
                              (:choice
                               "field"
                               "event"
                               "method"
                               "param"
                               "property"
                               "return"
                               "type"
                               "typevar")
                              ":")
  _namespace_member_declaration (:choice namespace_declaration type_declaration)
  namespace_declaration (:seq
                         "namespace"
                         (:field :name _name)
                         (:field :body declaration_list)
                         _optional_semi)
  file_scoped_namespace_declaration (:seq "namespace" (:field :name _name) ";")
  type_declaration (:choice
                    class_declaration
                    struct_declaration
                    enum_declaration
                    interface_declaration
                    delegate_declaration
                    record_declaration)
  class_declaration (:seq _class_declaration_initializer _declaration_list_body)
  _class_declaration_initializer (:seq
                                  (:repeat _attribute_list)
                                  (:repeat modifier)
                                  "class"
                                  (:field :name identifier)
                                  (:repeat (:choice type_parameter_list parameter_list base_list))
                                  (:repeat type_parameter_constraints_clause))
  struct_declaration (:seq _struct_declaration_initializer _declaration_list_body)
  _struct_declaration_initializer (:seq
                                   (:repeat _attribute_list)
                                   (:repeat modifier)
                                   (:choice "ref" :blank)
                                   "struct"
                                   (:field :name identifier)
                                   (:repeat (:choice type_parameter_list parameter_list base_list))
                                   (:repeat type_parameter_constraints_clause))
  enum_declaration (:seq
                    _enum_declaration_initializer
                    (:choice (:seq (:field :body enum_member_declaration_list) _optional_semi) ";"))
  _enum_declaration_initializer (:seq
                                 (:repeat _attribute_list)
                                 (:repeat modifier)
                                 "enum"
                                 (:field :name identifier)
                                 (:choice base_list :blank))
  enum_member_declaration_list (:seq
                                "{"
                                (:choice
                                 (:seq
                                  (:choice
                                   enum_member_declaration
                                   (:alias preproc_if_in_enum_member_declaration preproc_if))
                                  (:repeat
                                   (:seq
                                    ","
                                    (:choice
                                     enum_member_declaration
                                     (:alias preproc_if_in_enum_member_declaration preproc_if)))))
                                 :blank)
                                (:choice "," :blank)
                                "}")
  enum_member_declaration (:seq
                           (:repeat _attribute_list)
                           (:field :name identifier)
                           (:choice (:seq "=" (:field :value expression)) :blank))
  interface_declaration (:seq _interface_declaration_initializer _declaration_list_body)
  _interface_declaration_initializer (:seq
                                      (:repeat _attribute_list)
                                      (:repeat modifier)
                                      "interface"
                                      (:field :name identifier)
                                      (:field :type_parameters (:choice type_parameter_list :blank))
                                      (:choice base_list :blank)
                                      (:repeat type_parameter_constraints_clause))
  delegate_declaration (:seq
                        _delegate_declaration_initializer
                        (:repeat type_parameter_constraints_clause)
                        ";")
  _delegate_declaration_initializer (:seq
                                     (:repeat _attribute_list)
                                     (:repeat modifier)
                                     "delegate"
                                     (:field :type type)
                                     (:field :name identifier)
                                     (:field :type_parameters (:choice type_parameter_list :blank))
                                     (:field :parameters parameter_list))
  record_declaration (:seq _record_declaration_initializer _declaration_list_body)
  _record_declaration_initializer (:seq
                                   (:repeat _attribute_list)
                                   (:repeat modifier)
                                   "record"
                                   (:choice (:choice "class" "struct") :blank)
                                   (:field :name identifier)
                                   (:repeat (:choice type_parameter_list parameter_list))
                                   (:choice (:alias record_base base_list) :blank)
                                   (:repeat type_parameter_constraints_clause))
  record_base (:choice
               (:seq ":" (:seq _name (:repeat (:seq "," _name))))
               (:seq
                ":"
                primary_constructor_base_type
                (:choice (:seq "," (:seq _name (:repeat (:seq "," _name)))) :blank)))
  _declaration_list_body (:choice (:seq (:field :body declaration_list) _optional_semi) ";")
  primary_constructor_base_type (:seq (:field :type _name) argument_list)
  modifier (:prec-right 0
            (:choice
             "abstract"
             "async"
             "const"
             "extern"
             "file"
             "fixed"
             "internal"
             "new"
             "override"
             "partial"
             "private"
             "protected"
             "public"
             "readonly"
             "required"
             "sealed"
             "static"
             "unsafe"
             "virtual"
             "volatile"))
  type_parameter_list (:seq "<" (:seq type_parameter (:repeat (:seq "," type_parameter))) ">")
  type_parameter (:seq
                  (:repeat _attribute_list)
                  (:choice (:choice "in" "out") :blank)
                  (:field :name identifier))
  base_list (:seq
             ":"
             (:seq
              (:seq type (:choice argument_list :blank))
              (:repeat (:seq "," (:seq type (:choice argument_list :blank))))))
  type_parameter_constraints_clause (:seq
                                     "where"
                                     identifier
                                     ":"
                                     (:seq
                                      type_parameter_constraint
                                      (:repeat (:seq "," type_parameter_constraint))))
  type_parameter_constraint (:choice
                             (:seq "class" (:choice "?" :blank))
                             "struct"
                             "notnull"
                             "unmanaged"
                             constructor_constraint
                             (:field :type type))
  constructor_constraint (:seq "new" "(" ")")
  operator_declaration (:seq
                        (:repeat _attribute_list)
                        (:repeat modifier)
                        (:field :type type)
                        (:choice explicit_interface_specifier :blank)
                        "operator"
                        (:choice "checked" :blank)
                        (:field :operator
                         (:choice
                          "!"
                          "~"
                          "++"
                          "--"
                          "true"
                          "false"
                          "+"
                          "-"
                          "*"
                          "/"
                          "%"
                          "^"
                          "|"
                          "&"
                          "<<"
                          ">>"
                          ">>>"
                          "=="
                          "!="
                          ">"
                          "<"
                          ">="
                          "<="))
                        (:field :parameters parameter_list)
                        _function_body)
  conversion_operator_declaration (:seq
                                   (:repeat _attribute_list)
                                   (:repeat modifier)
                                   (:choice "implicit" "explicit")
                                   (:repeat1
                                    (:choice explicit_interface_specifier "operator" "checked"))
                                   (:field :type type)
                                   (:field :parameters parameter_list)
                                   _function_body)
  declaration_list (:seq "{" (:repeat declaration) "}")
  declaration (:choice
               class_declaration
               struct_declaration
               enum_declaration
               delegate_declaration
               field_declaration
               method_declaration
               event_declaration
               event_field_declaration
               record_declaration
               constructor_declaration
               destructor_declaration
               indexer_declaration
               interface_declaration
               namespace_declaration
               operator_declaration
               conversion_operator_declaration
               property_declaration
               using_directive
               preproc_if)
  field_declaration (:seq (:repeat _attribute_list) (:repeat modifier) variable_declaration ";")
  constructor_declaration (:seq _constructor_declaration_initializer _function_body)
  _constructor_declaration_initializer (:seq
                                        (:repeat _attribute_list)
                                        (:repeat modifier)
                                        (:field :name identifier)
                                        (:field :parameters parameter_list)
                                        (:choice constructor_initializer :blank))
  destructor_declaration (:seq
                          (:repeat _attribute_list)
                          (:choice "extern" :blank)
                          "~"
                          (:field :name identifier)
                          (:field :parameters parameter_list)
                          _function_body)
  method_declaration (:seq
                      (:repeat _attribute_list)
                      (:repeat modifier)
                      (:field :returns type)
                      (:choice explicit_interface_specifier :blank)
                      (:field :name identifier)
                      (:field :type_parameters (:choice type_parameter_list :blank))
                      (:field :parameters parameter_list)
                      (:repeat type_parameter_constraints_clause)
                      _function_body)
  event_declaration (:seq
                     (:repeat _attribute_list)
                     (:repeat modifier)
                     "event"
                     (:field :type type)
                     (:choice explicit_interface_specifier :blank)
                     (:field :name identifier)
                     (:choice (:field :accessors accessor_list) ";"))
  event_field_declaration (:prec-dynamic 1
                           (:seq
                            (:repeat _attribute_list)
                            (:repeat modifier)
                            "event"
                            variable_declaration
                            ";"))
  accessor_list (:seq "{" (:repeat accessor_declaration) "}")
  accessor_declaration (:seq
                        (:repeat _attribute_list)
                        (:repeat modifier)
                        (:field :name (:choice "get" "set" "add" "remove" "init" identifier))
                        _function_body)
  indexer_declaration (:seq
                       (:repeat _attribute_list)
                       (:repeat modifier)
                       (:field :type type)
                       (:choice explicit_interface_specifier :blank)
                       "this"
                       (:field :parameters bracketed_parameter_list)
                       (:choice
                        (:field :accessors accessor_list)
                        (:seq (:field :value arrow_expression_clause) ";")))
  bracketed_parameter_list (:seq
                            "["
                            (:choice
                             (:seq
                              (:choice parameter _parameter_array)
                              (:repeat (:seq "," (:choice parameter _parameter_array))))
                             :blank)
                            "]")
  property_declaration (:seq
                        (:repeat _attribute_list)
                        (:repeat modifier)
                        (:field :type type)
                        (:choice explicit_interface_specifier :blank)
                        (:field :name identifier)
                        (:choice
                         (:seq
                          (:field :accessors accessor_list)
                          (:choice (:seq "=" (:field :value expression) ";") :blank))
                         (:seq (:field :value arrow_expression_clause) ";")))
  explicit_interface_specifier (:prec 18 (:seq _name "."))
  parameter_list (:seq
                  "("
                  (:choice
                   (:seq
                    (:choice parameter _parameter_array)
                    (:repeat (:seq "," (:choice parameter _parameter_array))))
                   :blank)
                  ")")
  _parameter_type_with_modifiers (:seq
                                  (:repeat
                                   (:prec-left 0
                                    (:alias
                                     (:choice "this" "scoped" "ref" "out" "in" "readonly")
                                     modifier)))
                                  (:field :type type))
  parameter (:seq
             (:repeat _attribute_list)
             (:choice _parameter_type_with_modifiers :blank)
             (:field :name identifier)
             (:choice (:seq "=" expression) :blank))
  _parameter_array (:seq
                    (:repeat _attribute_list)
                    "params"
                    (:field :type type)
                    (:field :name identifier))
  constructor_initializer (:seq ":" (:choice "base" "this") argument_list)
  argument_list (:seq "(" (:choice (:seq argument (:repeat (:seq "," argument))) :blank) ")")
  tuple_pattern (:seq
                 "("
                 (:seq
                  (:choice (:field :name identifier) discard tuple_pattern)
                  (:repeat (:seq "," (:choice (:field :name identifier) discard tuple_pattern))))
                 ")")
  argument (:prec 1
            (:seq
             (:choice (:seq (:field :name identifier) ":") :blank)
             (:choice (:choice "ref" "out" "in") :blank)
             (:choice expression declaration_expression)))
  block (:seq "{" (:repeat statement) "}")
  arrow_expression_clause (:seq "=>" expression)
  _function_body (:choice
                  (:field :body block)
                  (:seq (:field :body arrow_expression_clause) ";")
                  ";")
  variable_declaration (:seq
                        (:field :type type)
                        (:seq variable_declarator (:repeat (:seq "," variable_declarator))))
  using_variable_declaration (:seq
                              (:field :type type)
                              (:seq
                               (:alias using_variable_declarator variable_declarator)
                               (:repeat
                                (:seq "," (:alias using_variable_declarator variable_declarator)))))
  variable_declarator (:seq
                       (:choice (:field :name identifier) tuple_pattern)
                       (:choice bracketed_argument_list :blank)
                       (:choice (:seq "=" expression) :blank))
  using_variable_declarator (:seq (:field :name identifier) (:choice (:seq "=" expression) :blank))
  bracketed_argument_list (:seq
                           "["
                           (:seq argument (:repeat (:seq "," argument)))
                           (:choice "," :blank)
                           "]")
  qualified_identifier (:seq identifier (:repeat (:seq "." identifier)))
  _name (:choice alias_qualified_name qualified_name _simple_name)
  alias_qualified_name (:seq (:field :alias identifier) "::" (:field :name _simple_name))
  _simple_name (:choice identifier generic_name)
  qualified_name (:prec 18 (:seq (:field :qualifier _name) "." (:field :name _simple_name)))
  generic_name (:seq identifier type_argument_list)
  type_argument_list (:seq "<" (:choice (:repeat ",") (:seq type (:repeat (:seq "," type)))) ">")
  type (:choice
        implicit_type
        array_type
        _name
        nullable_type
        pointer_type
        function_pointer_type
        predefined_type
        tuple_type
        ref_type
        scoped_type)
  implicit_type (:prec-dynamic 1 "var")
  array_type (:seq (:field :type _array_base_type) (:field :rank array_rank_specifier))
  _array_base_type (:choice
                    array_type
                    _name
                    nullable_type
                    pointer_type
                    function_pointer_type
                    predefined_type
                    tuple_type)
  array_rank_specifier (:seq
                        "["
                        (:choice
                         (:seq
                          (:choice expression :blank)
                          (:repeat (:seq "," (:choice expression :blank))))
                         :blank)
                        "]")
  nullable_type (:seq (:field :type _nullable_base_type) "?")
  _nullable_base_type (:choice array_type _name predefined_type tuple_type)
  pointer_type (:seq (:field :type _pointer_base_type) "*")
  _pointer_base_type (:choice
                      _name
                      nullable_type
                      pointer_type
                      function_pointer_type
                      predefined_type
                      tuple_type)
  function_pointer_type (:seq
                         "delegate"
                         "*"
                         (:choice calling_convention :blank)
                         "<"
                         (:repeat (:seq function_pointer_parameter ","))
                         (:field :returns type)
                         ">")
  calling_convention (:choice
                      "managed"
                      (:seq
                       "unmanaged"
                       (:choice
                        (:seq
                         "["
                         (:seq
                          (:choice "Cdecl" "Stdcall" "Thiscall" "Fastcall" identifier)
                          (:repeat
                           (:seq "," (:choice "Cdecl" "Stdcall" "Thiscall" "Fastcall" identifier))))
                         "]")
                        :blank)))
  function_pointer_parameter (:seq
                              (:choice (:choice "ref" "out" "in") :blank)
                              (:field :type _ref_base_type))
  predefined_type (:token
                   (:choice
                    "bool"
                    "byte"
                    "char"
                    "decimal"
                    "double"
                    "float"
                    "int"
                    "long"
                    "object"
                    "sbyte"
                    "short"
                    "string"
                    "uint"
                    "ulong"
                    "ushort"
                    "nint"
                    "nuint"
                    "void"))
  ref_type (:seq "ref" (:choice "readonly" :blank) (:field :type type))
  _ref_base_type (:choice
                  implicit_type
                  _name
                  nullable_type
                  array_type
                  pointer_type
                  function_pointer_type
                  predefined_type
                  tuple_type)
  scoped_type (:seq "scoped" (:field :type _scoped_base_type))
  _scoped_base_type (:choice _name ref_type)
  tuple_type (:seq "(" (:seq tuple_element (:repeat1 (:seq "," tuple_element))) ")")
  tuple_element (:seq (:field :type type) (:field :name (:choice identifier :blank)))
  statement (:prec 1
             (:choice
              block
              break_statement
              checked_statement
              continue_statement
              do_statement
              empty_statement
              expression_statement
              fixed_statement
              for_statement
              return_statement
              lock_statement
              yield_statement
              switch_statement
              throw_statement
              try_statement
              unsafe_statement
              using_statement
              foreach_statement
              goto_statement
              labeled_statement
              if_statement
              while_statement
              local_declaration_statement
              local_function_statement
              (:alias preproc_if_in_top_level preproc_if)))
  break_statement (:seq "break" ";")
  checked_statement (:seq (:choice "checked" "unchecked") block)
  continue_statement (:seq "continue" ";")
  do_statement (:seq
                "do"
                (:field :body statement)
                "while"
                "("
                (:field :condition expression)
                ")"
                ";")
  empty_statement ";"
  expression_statement (:seq _expression_statement_expression ";")
  fixed_statement (:seq "fixed" "(" variable_declaration ")" statement)
  for_statement (:seq "for" _for_statement_conditions (:field :body statement))
  _for_statement_conditions (:seq
                             "("
                             (:field :initializer
                              (:choice
                               (:choice
                                variable_declaration
                                (:seq expression (:repeat (:seq "," expression))))
                               :blank))
                             ";"
                             (:field :condition (:choice expression :blank))
                             ";"
                             (:field :update
                              (:choice (:seq expression (:repeat (:seq "," expression))) :blank))
                             ")")
  return_statement (:seq "return" (:choice expression :blank) ";")
  lock_statement (:seq "lock" "(" expression ")" statement)
  yield_statement (:seq "yield" (:choice (:seq "return" expression) "break") ";")
  switch_statement (:seq
                    "switch"
                    (:choice
                     (:seq "(" (:field :value expression) ")")
                     (:field :value tuple_expression))
                    (:field :body switch_body))
  switch_body (:seq "{" (:repeat switch_section) "}")
  switch_section (:prec-left 0
                  (:seq
                   (:choice
                    (:seq "case" (:choice expression (:seq pattern (:choice when_clause :blank))))
                    "default")
                   ":"
                   (:repeat statement)))
  throw_statement (:seq "throw" (:choice expression :blank) ";")
  try_statement (:seq
                 "try"
                 (:field :body block)
                 (:repeat catch_clause)
                 (:choice finally_clause :blank))
  catch_clause (:seq
                "catch"
                (:repeat (:choice catch_declaration catch_filter_clause))
                (:field :body block))
  catch_declaration (:seq "(" (:field :type type) (:choice (:field :name identifier) :blank) ")")
  catch_filter_clause (:seq "when" "(" expression ")")
  finally_clause (:seq "finally" block)
  unsafe_statement (:seq "unsafe" block)
  using_statement (:seq
                   (:choice "await" :blank)
                   "using"
                   "("
                   (:choice (:alias using_variable_declaration variable_declaration) expression)
                   ")"
                   (:field :body statement))
  foreach_statement (:seq _foreach_statement_initializer (:field :body statement))
  _foreach_statement_initializer (:seq
                                  (:choice "await" :blank)
                                  "foreach"
                                  "("
                                  (:choice
                                   (:seq
                                    (:field :type type)
                                    (:field :left (:choice identifier tuple_pattern)))
                                   (:field :left expression))
                                  "in"
                                  (:field :right expression)
                                  ")")
  goto_statement (:seq
                  "goto"
                  (:choice (:choice "case" "default") :blank)
                  (:choice expression :blank)
                  ";")
  labeled_statement (:seq identifier ":" statement)
  if_statement (:prec-right 0
                (:seq
                 "if"
                 "("
                 (:field :condition expression)
                 ")"
                 (:field :consequence statement)
                 (:choice (:seq "else" (:field :alternative statement)) :blank)))
  while_statement (:seq "while" "(" (:field :condition expression) ")" (:field :body statement))
  local_declaration_statement (:seq
                               (:choice "await" :blank)
                               (:choice "using" :blank)
                               (:repeat modifier)
                               variable_declaration
                               ";")
  local_function_statement (:seq
                            _local_function_declaration
                            (:repeat type_parameter_constraints_clause)
                            _function_body)
  _local_function_declaration (:seq
                               (:repeat _attribute_list)
                               (:repeat modifier)
                               (:field :type type)
                               (:field :name identifier)
                               (:field :type_parameters (:choice type_parameter_list :blank))
                               (:field :parameters parameter_list))
  pattern (:choice
           constant_pattern
           declaration_pattern
           discard
           recursive_pattern
           var_pattern
           negated_pattern
           (:prec-dynamic 1 (:alias _parenthesized_pattern_with_designation recursive_pattern))
           parenthesized_pattern
           relational_pattern
           or_pattern
           and_pattern
           list_pattern
           type_pattern)
  _parenthesized_pattern_with_designation (:seq "(" pattern ")" _variable_designation)
  constant_pattern (:choice
                    binary_expression
                    default_expression
                    interpolated_string_expression
                    parenthesized_expression
                    postfix_unary_expression
                    prefix_unary_expression
                    sizeof_expression
                    tuple_expression
                    typeof_expression
                    member_access_expression
                    (:alias _name_invocation_pattern invocation_expression)
                    (:alias _complex_invocation_expression invocation_expression)
                    cast_expression
                    _simple_name
                    literal)
  _name_invocation_pattern (:seq (:field :function _name) (:field :arguments argument_list))
  _complex_invocation_expression (:prec 18
                                  (:seq
                                   (:field :function
                                    (:choice
                                     member_access_expression
                                     element_access_expression
                                     invocation_expression
                                     parenthesized_expression
                                     conditional_access_expression
                                     cast_expression))
                                   (:field :arguments argument_list)))
  discard "_"
  parenthesized_pattern (:seq "(" pattern ")")
  var_pattern (:seq "var" _variable_designation)
  type_pattern (:prec-right 0 (:field :type type))
  list_pattern (:prec-right 0
                (:seq
                 "["
                 (:choice
                  (:seq
                   (:seq (:choice pattern "..") (:repeat (:seq "," (:choice pattern ".."))))
                   (:choice "," :blank))
                  :blank)
                 "]"
                 (:choice _variable_designation :blank)))
  recursive_pattern (:prec-left 0
                     (:choice
                      (:prec-dynamic 1
                       (:seq
                        (:field :type _name)
                        positional_pattern_clause
                        (:choice property_pattern_clause :blank)
                        _variable_designation))
                      (:prec-dynamic -1
                       (:seq
                        (:field :type _name)
                        positional_pattern_clause
                        (:choice property_pattern_clause :blank)))
                      (:prec-dynamic 1 (:seq positional_pattern_clause _variable_designation))
                      positional_pattern_clause
                      (:seq
                       (:field :type type)
                       (:choice
                        (:seq positional_pattern_clause (:choice property_pattern_clause :blank))
                        property_pattern_clause)
                       (:choice _variable_designation :blank))
                      (:seq
                       (:choice
                        (:seq positional_pattern_clause property_pattern_clause)
                        property_pattern_clause)
                       (:choice _variable_designation :blank))))
  positional_pattern_clause (:prec 1
                             (:seq
                              "("
                              (:choice
                               (:choice (:seq subpattern (:repeat (:seq "," subpattern))) :blank)
                               :blank)
                              ")"))
  property_pattern_clause (:prec 1
                           (:seq
                            "{"
                            (:choice (:seq subpattern (:repeat (:seq "," subpattern))) :blank)
                            (:choice "," :blank)
                            "}"))
  subpattern (:prec-right 0
              (:seq (:choice (:choice (:seq expression ":") (:seq identifier ":")) :blank) pattern))
  relational_pattern (:choice
                      (:seq "<" expression)
                      (:seq "<=" expression)
                      (:seq ">" expression)
                      (:seq ">=" expression))
  negated_pattern (:seq "not" pattern)
  and_pattern (:prec-left 8
               (:seq (:field :left pattern) (:field :operator "and") (:field :right pattern)))
  or_pattern (:prec-left 6
              (:seq (:field :left pattern) (:field :operator "or") (:field :right pattern)))
  declaration_pattern (:seq (:field :type type) _variable_designation)
  _variable_designation (:prec 1
                         (:choice
                          discard
                          parenthesized_variable_designation
                          (:field :name identifier)))
  parenthesized_variable_designation (:seq
                                      "("
                                      (:choice
                                       (:seq
                                        _variable_designation
                                        (:repeat (:seq "," _variable_designation)))
                                       :blank)
                                      ")")
  expression (:choice non_lvalue_expression lvalue_expression)
  non_lvalue_expression (:choice
                         "base"
                         binary_expression
                         interpolated_string_expression
                         conditional_expression
                         conditional_access_expression
                         literal
                         _expression_statement_expression
                         is_expression
                         is_pattern_expression
                         as_expression
                         cast_expression
                         checked_expression
                         collection_expression
                         switch_expression
                         throw_expression
                         default_expression
                         lambda_expression
                         with_expression
                         sizeof_expression
                         typeof_expression
                         makeref_expression
                         ref_expression
                         reftype_expression
                         refvalue_expression
                         stackalloc_expression
                         range_expression
                         array_creation_expression
                         anonymous_method_expression
                         anonymous_object_creation_expression
                         implicit_array_creation_expression
                         implicit_object_creation_expression
                         implicit_stackalloc_expression
                         initializer_expression
                         query_expression
                         (:alias preproc_if_in_expression preproc_if))
  lvalue_expression (:choice
                     "this"
                     member_access_expression
                     tuple_expression
                     _simple_name
                     element_access_expression
                     (:alias bracketed_argument_list element_binding_expression)
                     (:alias _pointer_indirection_expression prefix_unary_expression)
                     (:alias _parenthesized_lvalue_expression parenthesized_expression))
  _expression_statement_expression (:choice
                                    assignment_expression
                                    invocation_expression
                                    postfix_unary_expression
                                    prefix_unary_expression
                                    await_expression
                                    object_creation_expression
                                    parenthesized_expression)
  assignment_expression (:seq
                         (:field :left lvalue_expression)
                         (:field :operator
                          (:choice
                           "="
                           "+="
                           "-="
                           "*="
                           "/="
                           "%="
                           "&="
                           "^="
                           "|="
                           "<<="
                           ">>="
                           ">>>="
                           "??="))
                         (:field :right expression))
  binary_expression (:choice
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
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator ">>>")
                       (:field :right expression)))
                     (:prec-left 11
                      (:seq
                       (:field :left expression)
                       (:field :operator "<<")
                       (:field :right expression)))
                     (:prec-left 8
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "^")
                       (:field :right expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
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
                     (:prec-left 13
                      (:seq
                       (:field :left expression)
                       (:field :operator "%")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator "<")
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
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator ">=")
                       (:field :right expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left expression)
                       (:field :operator ">")
                       (:field :right expression)))
                     (:prec-right 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "??")
                       (:field :right expression))))
  postfix_unary_expression (:prec 18 (:seq expression (:choice "++" "--" "!")))
  prefix_unary_expression (:prec 17 (:seq (:choice "++" "--" "+" "-" "!" "~" "&" "^") expression))
  _pointer_indirection_expression (:prec-right 17 (:seq "*" lvalue_expression))
  query_expression (:seq from_clause _query_body)
  from_clause (:seq
               "from"
               (:choice (:field :type type) :blank)
               (:field :name identifier)
               "in"
               expression)
  _query_body (:prec-right 0
               (:seq
                (:seq (:repeat _query_clause) _select_or_group_clause)
                (:repeat
                 (:seq
                  (:seq "into" identifier)
                  (:seq (:repeat _query_clause) _select_or_group_clause)))))
  _query_clause (:choice from_clause join_clause let_clause order_by_clause where_clause)
  join_clause (:seq "join" _join_header _join_body (:choice join_into_clause :blank))
  _join_header (:seq (:choice (:field :type type) :blank) identifier "in" expression)
  _join_body (:seq "on" expression "equals" expression)
  join_into_clause (:seq "into" identifier)
  let_clause (:seq "let" identifier "=" expression)
  order_by_clause (:seq "orderby" (:seq _ordering (:repeat (:seq "," _ordering))))
  _ordering (:seq expression (:choice (:choice "ascending" "descending") :blank))
  where_clause (:seq "where" expression)
  _select_or_group_clause (:choice group_clause select_clause)
  group_clause (:seq "group" expression "by" expression)
  select_clause (:seq "select" expression)
  conditional_expression (:prec-right 2
                          (:seq
                           (:field :condition expression)
                           "?"
                           (:field :consequence expression)
                           ":"
                           (:field :alternative expression)))
  conditional_access_expression (:prec-right 2
                                 (:seq
                                  (:field :condition expression)
                                  "?"
                                  (:choice
                                   member_binding_expression
                                   (:alias bracketed_argument_list element_binding_expression))))
  as_expression (:prec 10
                 (:seq (:field :left expression) (:field :operator "as") (:field :right type)))
  is_expression (:prec 10
                 (:seq (:field :left expression) (:field :operator "is") (:field :right type)))
  is_pattern_expression (:prec 10
                         (:seq (:field :expression expression) "is" (:field :pattern pattern)))
  cast_expression (:prec 17
                   (:prec-dynamic 1 (:seq "(" (:field :type type) ")" (:field :value expression))))
  checked_expression (:seq (:choice "checked" "unchecked") "(" expression ")")
  invocation_expression (:prec 18
                         (:seq (:field :function expression) (:field :arguments argument_list)))
  switch_expression (:prec 15 (:seq expression "switch" _switch_expression_body))
  _switch_expression_body (:seq
                           "{"
                           (:choice
                            (:seq switch_expression_arm (:repeat (:seq "," switch_expression_arm)))
                            :blank)
                           (:choice "," :blank)
                           "}")
  switch_expression_arm (:seq pattern (:choice when_clause :blank) "=>" expression)
  when_clause (:seq "when" expression)
  await_expression (:prec-right 17 (:seq "await" expression))
  throw_expression (:seq "throw" expression)
  element_access_expression (:prec 18
                             (:seq
                              (:field :expression expression)
                              (:field :subscript bracketed_argument_list)))
  interpolated_string_expression (:choice
                                  (:seq
                                   (:alias interpolation_regular_start interpolation_start)
                                   (:alias interpolation_start_quote "\"")
                                   (:repeat _interpolated_string_content)
                                   (:alias interpolation_end_quote "\""))
                                  (:seq
                                   (:alias interpolation_verbatim_start interpolation_start)
                                   (:alias interpolation_start_quote "\"")
                                   (:repeat _interpolated_verbatim_string_content)
                                   (:alias interpolation_end_quote "\""))
                                  (:seq
                                   (:alias interpolation_raw_start interpolation_start)
                                   (:alias interpolation_start_quote interpolation_quote)
                                   (:repeat _interpolated_raw_string_content)
                                   (:alias interpolation_end_quote interpolation_quote)))
  _interpolated_string_content (:choice
                                (:alias interpolation_string_content string_content)
                                escape_sequence
                                interpolation)
  _interpolated_verbatim_string_content (:choice
                                         (:alias interpolation_string_content string_content)
                                         interpolation)
  _interpolated_raw_string_content (:choice
                                    (:alias interpolation_string_content string_content)
                                    interpolation)
  interpolation (:seq
                 (:alias interpolation_open_brace interpolation_brace)
                 expression
                 (:choice interpolation_alignment_clause :blank)
                 (:choice interpolation_format_clause :blank)
                 (:alias interpolation_close_brace interpolation_brace))
  interpolation_alignment_clause (:seq "," expression)
  interpolation_format_clause (:seq ":" (:pattern "[^}\"]+"))
  member_access_expression (:prec 18
                            (:seq
                             (:field :expression (:choice expression predefined_type _name))
                             (:choice "." "->")
                             (:field :name _simple_name)))
  member_binding_expression (:seq "." (:field :name _simple_name))
  object_creation_expression (:prec-right 0
                              (:seq
                               "new"
                               (:field :type type)
                               (:field :arguments (:choice argument_list :blank))
                               (:field :initializer (:choice initializer_expression :blank))))
  _object_creation_type (:choice _name nullable_type predefined_type)
  parenthesized_expression (:seq "(" non_lvalue_expression ")")
  _parenthesized_lvalue_expression (:seq "(" lvalue_expression ")")
  lambda_expression (:prec -1
                     (:seq _lambda_expression_init "=>" (:field :body (:choice block expression))))
  _lambda_expression_init (:prec -1
                           (:seq
                            (:repeat _attribute_list)
                            (:repeat (:prec -1 (:alias (:choice "static" "async") modifier)))
                            (:choice (:field :type type) :blank)
                            (:field :parameters _lambda_parameters)))
  _lambda_parameters (:prec -1 (:choice parameter_list (:alias identifier implicit_parameter)))
  array_creation_expression (:prec-dynamic 17
                             (:seq
                              "new"
                              (:field :type array_type)
                              (:choice initializer_expression :blank)))
  anonymous_method_expression (:seq
                               (:repeat (:prec -1 (:alias (:choice "static" "async") modifier)))
                               "delegate"
                               (:choice (:field :parameters parameter_list) :blank)
                               block)
  anonymous_object_creation_expression (:seq
                                        "new"
                                        "{"
                                        (:choice
                                         (:seq
                                          _anonymous_object_member_declarator
                                          (:repeat (:seq "," _anonymous_object_member_declarator)))
                                         :blank)
                                        (:choice "," :blank)
                                        "}")
  _anonymous_object_member_declarator (:choice (:seq identifier "=" expression) expression)
  implicit_array_creation_expression (:seq "new" "[" (:repeat ",") "]" initializer_expression)
  implicit_object_creation_expression (:prec-right 0
                                       (:seq
                                        "new"
                                        argument_list
                                        (:choice initializer_expression :blank)))
  implicit_stackalloc_expression (:seq "stackalloc" "[" "]" initializer_expression)
  collection_expression (:seq
                         "["
                         (:choice
                          (:seq
                           (:seq collection_element (:repeat (:seq "," collection_element)))
                           (:choice "," :blank))
                          :blank)
                         "]")
  collection_element (:choice expression_element spread_element)
  expression_element (:prec 1 expression)
  spread_element (:prec-dynamic 1 (:prec 16 (:seq ".." expression)))
  initializer_expression (:seq
                          "{"
                          (:choice (:seq expression (:repeat (:seq "," expression))) :blank)
                          (:choice "," :blank)
                          "}")
  declaration_expression (:prec-dynamic 1 (:seq (:field :type type) (:field :name identifier)))
  default_expression (:prec-right 0
                      (:seq "default" (:choice (:seq "(" (:field :type type) ")") :blank)))
  with_expression (:prec-left 14 (:seq expression "with" _with_body))
  _with_body (:seq
              "{"
              (:choice (:seq with_initializer (:repeat (:seq "," with_initializer))) :blank)
              (:choice "," :blank)
              "}")
  with_initializer (:seq identifier "=" expression)
  sizeof_expression (:seq "sizeof" "(" (:field :type type) ")")
  typeof_expression (:seq "typeof" "(" (:field :type type) ")")
  makeref_expression (:seq "__makeref" "(" expression ")")
  ref_expression (:seq "ref" expression)
  reftype_expression (:seq "__reftype" "(" expression ")")
  refvalue_expression (:seq "__refvalue" "(" (:field :value expression) "," (:field :type type) ")")
  stackalloc_expression (:prec-left 0
                         (:seq
                          "stackalloc"
                          (:field :type array_type)
                          (:choice initializer_expression :blank)))
  range_expression (:prec-right 16
                    (:seq (:choice expression :blank) ".." (:choice expression :blank)))
  tuple_expression (:seq "(" (:seq argument (:repeat1 (:seq "," argument))) ")")
  literal (:choice
           null_literal
           character_literal
           integer_literal
           real_literal
           boolean_literal
           string_literal
           verbatim_string_literal
           raw_string_literal)
  null_literal "null"
  character_literal (:seq "'" (:choice character_literal_content escape_sequence) "'")
  character_literal_content (:token-immediate (:pattern "[^'\\\\]"))
  integer_literal (:token
                   (:seq
                    (:choice
                     (:pattern "([0-9][0-9_]*[0-9]|[0-9])")
                     (:pattern "0[xX][0-9a-fA-F_]*[0-9a-fA-F]+")
                     (:pattern "0[bB][01_]*[01]+"))
                    (:choice (:pattern "([uU][lL]?|[lL][uU]?)") :blank)))
  real_literal (:token
                (:choice
                 (:seq
                  (:pattern "([0-9][0-9_]*[0-9]|[0-9])")
                  "."
                  (:pattern "([0-9][0-9_]*[0-9]|[0-9])")
                  (:choice (:pattern "[eE][+-]?[0-9][0-9_]*") :blank)
                  (:choice (:pattern "[fFdDmM]") :blank))
                 (:seq
                  "."
                  (:pattern "([0-9][0-9_]*[0-9]|[0-9])")
                  (:choice (:pattern "[eE][+-]?[0-9][0-9_]*") :blank)
                  (:choice (:pattern "[fFdDmM]") :blank))
                 (:seq
                  (:pattern "([0-9][0-9_]*[0-9]|[0-9])")
                  (:pattern "[eE][+-]?[0-9][0-9_]*")
                  (:choice (:pattern "[fFdDmM]") :blank))
                 (:seq (:pattern "([0-9][0-9_]*[0-9]|[0-9])") (:pattern "[fFdDmM]"))))
  string_literal (:seq
                  "\""
                  (:repeat (:choice string_literal_content escape_sequence))
                  "\""
                  (:choice string_literal_encoding :blank))
  string_literal_content (:token-immediate (:prec 1 (:pattern "[^\"\\\\\\n]+")))
  escape_sequence (:token
                   (:choice
                    (:pattern "\\\\x[0-9a-fA-F]{1,4}")
                    (:pattern "\\\\u[0-9a-fA-F]{4}")
                    (:pattern "\\\\U[0-9a-fA-F]{8}")
                    (:pattern "\\\\[abefnrtv'\\\"\\\\\\?0]")))
  string_literal_encoding (:token-immediate (:pattern "(u|U)8"))
  verbatim_string_literal (:token
                           (:seq
                            "@\""
                            (:repeat (:choice (:pattern "[^\"]") "\"\""))
                            "\""
                            (:choice (:pattern "(u|U)8") :blank)))
  raw_string_literal (:seq
                      raw_string_start
                      raw_string_content
                      raw_string_end
                      (:choice (:pattern "(u|U)8") :blank))
  boolean_literal (:choice "true" "false")
  _identifier_token (:token
                     (:seq
                      (:choice "@" :blank)
                      (:pattern "(\\p{XID_Start}|_|\\\\u[0-9A-Fa-f]{4}|\\\\U[0-9A-Fa-f]{8})(\\p{XID_Continue}|\\\\u[0-9A-Fa-f]{4}|\\\\U[0-9A-Fa-f]{8})*")))
  identifier (:choice _identifier_token _reserved_identifier)
  _reserved_identifier (:choice
                        "alias"
                        "ascending"
                        "by"
                        "descending"
                        "equals"
                        "file"
                        "from"
                        "global"
                        "group"
                        "into"
                        "join"
                        "let"
                        "notnull"
                        "on"
                        "orderby"
                        "scoped"
                        "select"
                        "unmanaged"
                        "var"
                        "when"
                        "where"
                        "yield")
  preproc_if (:prec 0
              (:seq
               (:alias (:pattern "#[ \t]*if") "#if")
               (:field :condition _preproc_expression)
               (:pattern "\\n")
               (:repeat declaration)
               (:field :alternative (:choice (:choice preproc_else preproc_elif) :blank))
               (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else (:prec 0 (:seq (:alias (:pattern "#[ \t]*else") "#else") (:repeat declaration)))
  preproc_elif (:prec 0
                (:seq
                 (:alias (:pattern "#[ \t]*elif") "#elif")
                 (:field :condition _preproc_expression)
                 (:pattern "\\n")
                 (:repeat declaration)
                 (:field :alternative (:choice (:choice preproc_else preproc_elif) :blank))))
  preproc_if_in_top_level (:prec 0
                           (:seq
                            (:alias (:pattern "#[ \t]*if") "#if")
                            (:field :condition _preproc_expression)
                            (:pattern "\\n")
                            (:repeat (:choice _top_level_item_no_statement statement))
                            (:field :alternative
                             (:choice
                              (:choice
                               (:alias preproc_else_in_top_level preproc_else)
                               (:alias preproc_elif_in_top_level preproc_elif))
                              :blank))
                            (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else_in_top_level (:prec 0
                             (:seq
                              (:alias (:pattern "#[ \t]*else") "#else")
                              (:repeat (:choice _top_level_item_no_statement statement))))
  preproc_elif_in_top_level (:prec 0
                             (:seq
                              (:alias (:pattern "#[ \t]*elif") "#elif")
                              (:field :condition _preproc_expression)
                              (:pattern "\\n")
                              (:repeat (:choice _top_level_item_no_statement statement))
                              (:field :alternative
                               (:choice
                                (:choice
                                 (:alias preproc_else_in_top_level preproc_else)
                                 (:alias preproc_elif_in_top_level preproc_elif))
                                :blank))))
  preproc_if_in_expression (:prec -2
                            (:seq
                             (:alias (:pattern "#[ \t]*if") "#if")
                             (:field :condition _preproc_expression)
                             (:pattern "\\n")
                             (:choice expression :blank)
                             (:field :alternative
                              (:choice
                               (:choice
                                (:alias preproc_else_in_expression preproc_else)
                                (:alias preproc_elif_in_expression preproc_elif))
                               :blank))
                             (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else_in_expression (:prec -2
                              (:seq
                               (:alias (:pattern "#[ \t]*else") "#else")
                               (:choice expression :blank)))
  preproc_elif_in_expression (:prec -2
                              (:seq
                               (:alias (:pattern "#[ \t]*elif") "#elif")
                               (:field :condition _preproc_expression)
                               (:pattern "\\n")
                               (:choice expression :blank)
                               (:field :alternative
                                (:choice
                                 (:choice
                                  (:alias preproc_else_in_expression preproc_else)
                                  (:alias preproc_elif_in_expression preproc_elif))
                                 :blank))))
  preproc_if_in_enum_member_declaration (:prec 0
                                         (:seq
                                          (:alias (:pattern "#[ \t]*if") "#if")
                                          (:field :condition _preproc_expression)
                                          (:pattern "\\n")
                                          (:choice enum_member_declaration :blank)
                                          (:field :alternative
                                           (:choice
                                            (:choice
                                             (:alias
                                              preproc_else_in_enum_member_declaration
                                              preproc_else)
                                             (:alias
                                              preproc_elif_in_enum_member_declaration
                                              preproc_elif))
                                            :blank))
                                          (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else_in_enum_member_declaration (:prec 0
                                           (:seq
                                            (:alias (:pattern "#[ \t]*else") "#else")
                                            (:choice enum_member_declaration :blank)))
  preproc_elif_in_enum_member_declaration (:prec 0
                                           (:seq
                                            (:alias (:pattern "#[ \t]*elif") "#elif")
                                            (:field :condition _preproc_expression)
                                            (:pattern "\\n")
                                            (:choice enum_member_declaration :blank)
                                            (:field :alternative
                                             (:choice
                                              (:choice
                                               (:alias
                                                preproc_else_in_enum_member_declaration
                                                preproc_else)
                                               (:alias
                                                preproc_elif_in_enum_member_declaration
                                                preproc_elif))
                                              :blank))))
  preproc_if_in_attribute_list (:prec -1
                                (:seq
                                 (:alias (:pattern "#[ \t]*if") "#if")
                                 (:field :condition _preproc_expression)
                                 (:pattern "\\n")
                                 (:choice attribute_list :blank)
                                 (:field :alternative
                                  (:choice
                                   (:choice
                                    (:alias preproc_else_in_attribute_list preproc_else)
                                    (:alias preproc_elif_in_attribute_list preproc_elif))
                                   :blank))
                                 (:alias (:pattern "#[ \t]*endif") "#endif")))
  preproc_else_in_attribute_list (:prec -1
                                  (:seq
                                   (:alias (:pattern "#[ \t]*else") "#else")
                                   (:choice attribute_list :blank)))
  preproc_elif_in_attribute_list (:prec -1
                                  (:seq
                                   (:alias (:pattern "#[ \t]*elif") "#elif")
                                   (:field :condition _preproc_expression)
                                   (:pattern "\\n")
                                   (:choice attribute_list :blank)
                                   (:field :alternative
                                    (:choice
                                     (:choice
                                      (:alias preproc_else_in_attribute_list preproc_else)
                                      (:alias preproc_elif_in_attribute_list preproc_elif))
                                     :blank))))
  preproc_arg (:token (:prec -1 (:pattern "\\S([^/\\n]|\\/[^*]|\\\\\\r?\\n)*")))
  preproc_directive (:pattern "#[ \\t]*[a-zA-Z0-9]\\w*")
  _preproc_expression (:choice
                       identifier
                       boolean_literal
                       integer_literal
                       character_literal
                       (:alias preproc_unary_expression unary_expression)
                       (:alias preproc_binary_expression binary_expression)
                       (:alias preproc_parenthesized_expression parenthesized_expression))
  preproc_parenthesized_expression (:seq "(" _preproc_expression ")")
  preproc_unary_expression (:prec-left 17
                            (:seq (:field :operator "!") (:field :argument _preproc_expression)))
  preproc_binary_expression (:choice
                             (:prec-left 4
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "||")
                               (:field :right _preproc_expression)))
                             (:prec-left 5
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "&&")
                               (:field :right _preproc_expression)))
                             (:prec-left 9
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "==")
                               (:field :right _preproc_expression)))
                             (:prec-left 9
                              (:seq
                               (:field :left _preproc_expression)
                               (:field :operator "!=")
                               (:field :right _preproc_expression))))
  preproc_region (:seq
                  (:alias (:pattern "#[ \t]*region") "#region")
                  (:choice (:field :content preproc_arg) :blank)
                  (:pattern "\\n"))
  preproc_endregion (:seq
                     (:alias (:pattern "#[ \t]*endregion") "#endregion")
                     (:choice (:field :content preproc_arg) :blank)
                     (:pattern "\\n"))
  preproc_line (:seq
                (:alias (:pattern "#[ \t]*line") "#line")
                (:choice
                 "default"
                 "hidden"
                 (:seq integer_literal (:choice string_literal :blank))
                 (:seq
                  "("
                  integer_literal
                  ","
                  integer_literal
                  ")"
                  "-"
                  "("
                  integer_literal
                  ","
                  integer_literal
                  ")"
                  (:choice integer_literal :blank)
                  string_literal))
                (:pattern "\\n"))
  preproc_pragma (:seq
                  (:alias (:pattern "#[ \t]*pragma") "#pragma")
                  (:choice
                   (:seq
                    "warning"
                    (:choice "disable" "restore")
                    (:choice
                     (:seq
                      (:choice identifier integer_literal)
                      (:repeat (:seq "," (:choice identifier integer_literal))))
                     :blank))
                   (:seq "checksum" string_literal string_literal string_literal))
                  (:pattern "\\n"))
  preproc_nullable (:seq
                    (:alias (:pattern "#[ \t]*nullable") "#nullable")
                    (:choice "enable" "disable" "restore")
                    (:choice (:choice "annotations" "warnings") :blank)
                    (:pattern "\\n"))
  preproc_error (:seq (:alias (:pattern "#[ \t]*error") "#error") preproc_arg (:pattern "\\n"))
  preproc_warning (:seq
                   (:alias (:pattern "#[ \t]*warning") "#warning")
                   preproc_arg
                   (:pattern "\\n"))
  preproc_define (:seq (:alias (:pattern "#[ \t]*define") "#define") preproc_arg (:pattern "\\n"))
  preproc_undef (:seq (:alias (:pattern "#[ \t]*undef") "#undef") preproc_arg (:pattern "\\n"))
  shebang_directive (:token (:seq "#!" (:pattern ".*")))
  comment (:token
           (:choice
            (:seq "//" (:pattern "[^\\n\\r]*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))}}
