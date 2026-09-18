# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "rust"
 :word identifier
 :extras [(:pattern "\\s") line_comment block_comment]
 :conflicts [[_type _pattern]
             [unit_type tuple_pattern]
             [scoped_identifier scoped_type_identifier]
             [parameters _pattern]
             [parameters tuple_struct_pattern]
             [array_expression]
             [visibility_modifier]
             [visibility_modifier scoped_identifier scoped_type_identifier]
             [foreign_mod_item function_modifiers]]
 :precedences []
 :externals [string_content
             string_close
             _raw_string_literal_start
             raw_string_literal_content
             _raw_string_literal_end
             float_literal
             _outer_block_doc_comment_marker
             _inner_block_doc_comment_marker
             _block_comment_content
             _line_doc_content
             _error_sentinel]
 :inline [_path
          _type_identifier
          _tokens
          _field_identifier
          _non_special_token
          _declaration_statement
          _reserved_identifier
          _expression_ending_with_block]
 :supertypes [_expression _type _literal _literal_pattern _declaration_statement _pattern]
 :rules
 {source_file (:seq (:choice shebang :blank) (:repeat _statement))
  _statement (:choice expression_statement _declaration_statement)
  empty_statement ";"
  expression_statement (:choice (:seq _expression ";") (:prec 1 _expression_ending_with_block))
  _declaration_statement (:choice
                          const_item
                          macro_invocation
                          macro_definition
                          empty_statement
                          attribute_item
                          inner_attribute_item
                          mod_item
                          foreign_mod_item
                          struct_item
                          union_item
                          enum_item
                          type_item
                          function_item
                          function_signature_item
                          impl_item
                          trait_item
                          associated_type
                          let_declaration
                          use_declaration
                          extern_crate_declaration
                          static_item)
  macro_definition (:seq
                    "macro_rules!"
                    (:field :name (:choice identifier _reserved_identifier))
                    (:choice
                     (:seq
                      "("
                      (:seq (:repeat (:seq macro_rule ";")) (:choice macro_rule :blank))
                      ")"
                      ";")
                     (:seq
                      "["
                      (:seq (:repeat (:seq macro_rule ";")) (:choice macro_rule :blank))
                      "]"
                      ";")
                     (:seq
                      "{"
                      (:seq (:repeat (:seq macro_rule ";")) (:choice macro_rule :blank))
                      "}")))
  macro_rule (:seq (:field :left token_tree_pattern) "=>" (:field :right token_tree))
  _token_pattern (:choice
                  token_tree_pattern
                  token_repetition_pattern
                  token_binding_pattern
                  metavariable
                  _non_special_token)
  token_tree_pattern (:choice
                      (:seq "(" (:repeat _token_pattern) ")")
                      (:seq "[" (:repeat _token_pattern) "]")
                      (:seq "{" (:repeat _token_pattern) "}"))
  token_binding_pattern (:prec 1
                         (:seq (:field :name metavariable) ":" (:field :type fragment_specifier)))
  token_repetition_pattern (:seq
                            "$"
                            "("
                            (:repeat _token_pattern)
                            ")"
                            (:choice (:pattern "[^+*?]+") :blank)
                            (:choice "+" "*" "?"))
  fragment_specifier (:choice
                      "block"
                      "expr"
                      "expr_2021"
                      "ident"
                      "item"
                      "lifetime"
                      "literal"
                      "meta"
                      "pat"
                      "pat_param"
                      "path"
                      "stmt"
                      "tt"
                      "ty"
                      "vis")
  _tokens (:choice token_tree token_repetition metavariable _non_special_token)
  token_tree (:choice
              (:seq "(" (:repeat _tokens) ")")
              (:seq "[" (:repeat _tokens) "]")
              (:seq "{" (:repeat _tokens) "}"))
  token_repetition (:seq
                    "$"
                    "("
                    (:repeat _tokens)
                    ")"
                    (:choice (:pattern "[^+*?]+") :blank)
                    (:choice "+" "*" "?"))
  _non_special_token (:choice
                      _literal
                      identifier
                      mutable_specifier
                      self
                      super
                      crate
                      (:alias
                       (:choice
                        "u8"
                        "i8"
                        "u16"
                        "i16"
                        "u32"
                        "i32"
                        "u64"
                        "i64"
                        "u128"
                        "i128"
                        "isize"
                        "usize"
                        "f32"
                        "f64"
                        "bool"
                        "str"
                        "char")
                       primitive_type)
                      (:prec-right 0
                       (:repeat1
                        (:choice
                         "+"
                         "-"
                         "*"
                         "/"
                         "%"
                         "^"
                         "!"
                         "&"
                         "|"
                         "&&"
                         "||"
                         "<<"
                         ">>"
                         "+="
                         "-="
                         "*="
                         "/="
                         "%="
                         "^="
                         "&="
                         "|="
                         "<<="
                         ">>="
                         "="
                         "=="
                         "!="
                         ">"
                         "<"
                         ">="
                         "<="
                         "@"
                         "_"
                         "."
                         ".."
                         "..."
                         "..="
                         ","
                         ";"
                         ":"
                         "::"
                         "->"
                         "=>"
                         "#"
                         "?")))
                      "'"
                      "as"
                      "async"
                      "await"
                      "break"
                      "const"
                      "continue"
                      "default"
                      "enum"
                      "fn"
                      "for"
                      "gen"
                      "if"
                      "impl"
                      "let"
                      "loop"
                      "match"
                      "mod"
                      "pub"
                      "return"
                      "static"
                      "struct"
                      "trait"
                      "type"
                      "union"
                      "unsafe"
                      "use"
                      "where"
                      "while")
  attribute_item (:seq "#" "[" attribute "]")
  inner_attribute_item (:seq "#" "!" "[" attribute "]")
  attribute (:seq
             _path
             (:choice
              (:choice
               (:seq "=" (:field :value _expression))
               (:field :arguments (:alias delim_token_tree token_tree)))
              :blank))
  mod_item (:seq
            (:choice visibility_modifier :blank)
            "mod"
            (:field :name identifier)
            (:choice ";" (:field :body declaration_list)))
  foreign_mod_item (:seq
                    (:choice "unsafe" :blank)
                    extern_modifier
                    (:choice ";" (:field :body declaration_list)))
  declaration_list (:seq "{" (:repeat _declaration_statement) "}")
  struct_item (:seq
               (:choice visibility_modifier :blank)
               "struct"
               (:field :name _type_identifier)
               (:field :type_parameters (:choice type_parameters :blank))
               (:choice
                (:seq (:choice where_clause :blank) (:field :body field_declaration_list))
                (:seq
                 (:field :body ordered_field_declaration_list)
                 (:choice where_clause :blank)
                 ";")
                ";"))
  union_item (:seq
              (:choice visibility_modifier :blank)
              "union"
              (:field :name _type_identifier)
              (:field :type_parameters (:choice type_parameters :blank))
              (:choice where_clause :blank)
              (:field :body field_declaration_list))
  enum_item (:seq
             (:choice visibility_modifier :blank)
             "enum"
             (:field :name _type_identifier)
             (:field :type_parameters (:choice type_parameters :blank))
             (:choice where_clause :blank)
             (:field :body enum_variant_list))
  enum_variant_list (:seq
                     "{"
                     (:choice
                      (:seq
                       (:seq (:repeat attribute_item) enum_variant)
                       (:repeat (:seq "," (:seq (:repeat attribute_item) enum_variant))))
                      :blank)
                     (:choice "," :blank)
                     "}")
  enum_variant (:seq
                (:choice visibility_modifier :blank)
                (:field :name identifier)
                (:field :body
                 (:choice (:choice field_declaration_list ordered_field_declaration_list) :blank))
                (:choice (:seq "=" (:field :value _expression)) :blank))
  field_declaration_list (:seq
                          "{"
                          (:choice
                           (:seq
                            (:seq (:repeat attribute_item) field_declaration)
                            (:repeat (:seq "," (:seq (:repeat attribute_item) field_declaration))))
                           :blank)
                          (:choice "," :blank)
                          "}")
  field_declaration (:seq
                     (:choice visibility_modifier :blank)
                     (:field :name _field_identifier)
                     ":"
                     (:field :type _type))
  ordered_field_declaration_list (:seq
                                  "("
                                  (:choice
                                   (:seq
                                    (:seq
                                     (:repeat attribute_item)
                                     (:choice visibility_modifier :blank)
                                     (:field :type _type))
                                    (:repeat
                                     (:seq
                                      ","
                                      (:seq
                                       (:repeat attribute_item)
                                       (:choice visibility_modifier :blank)
                                       (:field :type _type)))))
                                   :blank)
                                  (:choice "," :blank)
                                  ")")
  extern_crate_declaration (:seq
                            (:choice visibility_modifier :blank)
                            "extern"
                            crate
                            (:field :name identifier)
                            (:choice (:seq "as" (:field :alias identifier)) :blank)
                            ";")
  const_item (:seq
              (:choice visibility_modifier :blank)
              "const"
              (:field :name identifier)
              ":"
              (:field :type _type)
              (:choice (:seq "=" (:field :value _expression)) :blank)
              ";")
  static_item (:seq
               (:choice visibility_modifier :blank)
               "static"
               (:choice "ref" :blank)
               (:choice mutable_specifier :blank)
               (:field :name identifier)
               ":"
               (:field :type _type)
               (:choice (:seq "=" (:field :value _expression)) :blank)
               ";")
  type_item (:seq
             (:choice visibility_modifier :blank)
             "type"
             (:field :name _type_identifier)
             (:field :type_parameters (:choice type_parameters :blank))
             (:choice where_clause :blank)
             "="
             (:field :type _type)
             (:choice where_clause :blank)
             ";")
  function_item (:seq
                 (:choice visibility_modifier :blank)
                 (:choice function_modifiers :blank)
                 "fn"
                 (:field :name (:choice identifier metavariable))
                 (:field :type_parameters (:choice type_parameters :blank))
                 (:field :parameters parameters)
                 (:choice (:seq "->" (:field :return_type _type)) :blank)
                 (:choice where_clause :blank)
                 (:field :body block))
  function_signature_item (:seq
                           (:choice visibility_modifier :blank)
                           (:choice function_modifiers :blank)
                           "fn"
                           (:field :name (:choice identifier metavariable))
                           (:field :type_parameters (:choice type_parameters :blank))
                           (:field :parameters parameters)
                           (:choice (:seq "->" (:field :return_type _type)) :blank)
                           (:choice where_clause :blank)
                           ";")
  function_modifiers (:repeat1 (:choice "async" "default" "const" "unsafe" extern_modifier))
  where_clause (:prec-right 0
                (:seq
                 "where"
                 (:choice
                  (:seq
                   (:seq where_predicate (:repeat (:seq "," where_predicate)))
                   (:choice "," :blank))
                  :blank)))
  where_predicate (:seq
                   (:field :left
                    (:choice
                     lifetime
                     _type_identifier
                     scoped_type_identifier
                     generic_type
                     reference_type
                     pointer_type
                     tuple_type
                     array_type
                     higher_ranked_trait_bound
                     (:alias
                      (:choice
                       "u8"
                       "i8"
                       "u16"
                       "i16"
                       "u32"
                       "i32"
                       "u64"
                       "i64"
                       "u128"
                       "i128"
                       "isize"
                       "usize"
                       "f32"
                       "f64"
                       "bool"
                       "str"
                       "char")
                      primitive_type)))
                   (:field :bounds trait_bounds))
  impl_item (:seq
             (:choice "unsafe" :blank)
             "impl"
             (:field :type_parameters (:choice type_parameters :blank))
             (:choice
              (:seq
               (:choice "!" :blank)
               (:field :trait (:choice _type_identifier scoped_type_identifier generic_type))
               "for")
              :blank)
             (:field :type _type)
             (:choice where_clause :blank)
             (:choice (:field :body declaration_list) ";"))
  trait_item (:seq
              (:choice visibility_modifier :blank)
              (:choice "unsafe" :blank)
              "trait"
              (:field :name _type_identifier)
              (:field :type_parameters (:choice type_parameters :blank))
              (:field :bounds (:choice trait_bounds :blank))
              (:choice where_clause :blank)
              (:field :body declaration_list))
  associated_type (:seq
                   "type"
                   (:field :name _type_identifier)
                   (:field :type_parameters (:choice type_parameters :blank))
                   (:field :bounds (:choice trait_bounds :blank))
                   (:choice where_clause :blank)
                   ";")
  trait_bounds (:seq
                ":"
                (:seq
                 (:choice _type lifetime higher_ranked_trait_bound)
                 (:repeat (:seq "+" (:choice _type lifetime higher_ranked_trait_bound)))))
  higher_ranked_trait_bound (:seq
                             "for"
                             (:field :type_parameters type_parameters)
                             (:field :type _type))
  removed_trait_bound (:seq "?" _type)
  type_parameters (:prec 1
                   (:seq
                    "<"
                    (:seq
                     (:seq
                      (:repeat attribute_item)
                      (:choice metavariable type_parameter lifetime_parameter const_parameter))
                     (:repeat
                      (:seq
                       ","
                       (:seq
                        (:repeat attribute_item)
                        (:choice metavariable type_parameter lifetime_parameter const_parameter)))))
                    (:choice "," :blank)
                    ">"))
  const_parameter (:seq
                   "const"
                   (:field :name identifier)
                   ":"
                   (:field :type _type)
                   (:choice
                    (:seq "=" (:field :value (:choice block identifier _literal negative_literal)))
                    :blank))
  type_parameter (:prec 1
                  (:seq
                   (:field :name _type_identifier)
                   (:choice (:field :bounds trait_bounds) :blank)
                   (:choice (:seq "=" (:field :default_type _type)) :blank)))
  lifetime_parameter (:prec 1
                      (:seq (:field :name lifetime) (:choice (:field :bounds trait_bounds) :blank)))
  let_declaration (:seq
                   "let"
                   (:choice mutable_specifier :blank)
                   (:field :pattern _pattern)
                   (:choice (:seq ":" (:field :type _type)) :blank)
                   (:choice (:seq "=" (:field :value _expression)) :blank)
                   (:choice (:seq "else" (:field :alternative block)) :blank)
                   ";")
  use_declaration (:seq
                   (:choice visibility_modifier :blank)
                   "use"
                   (:field :argument _use_clause)
                   ";")
  _use_clause (:choice _path use_as_clause use_list scoped_use_list use_wildcard)
  scoped_use_list (:seq (:field :path (:choice _path :blank)) "::" (:field :list use_list))
  use_list (:seq
            "{"
            (:choice (:seq (:choice _use_clause) (:repeat (:seq "," (:choice _use_clause)))) :blank)
            (:choice "," :blank)
            "}")
  use_as_clause (:seq (:field :path _path) "as" (:field :alias identifier))
  use_wildcard (:seq (:choice (:seq (:choice _path :blank) "::") :blank) "*")
  parameters (:seq
              "("
              (:choice
               (:seq
                (:seq
                 (:choice attribute_item :blank)
                 (:choice parameter self_parameter variadic_parameter "_" _type))
                (:repeat
                 (:seq
                  ","
                  (:seq
                   (:choice attribute_item :blank)
                   (:choice parameter self_parameter variadic_parameter "_" _type)))))
               :blank)
              (:choice "," :blank)
              ")")
  self_parameter (:seq
                  (:choice "&" :blank)
                  (:choice lifetime :blank)
                  (:choice mutable_specifier :blank)
                  self)
  variadic_parameter (:seq
                      (:choice mutable_specifier :blank)
                      (:choice (:seq (:field :pattern _pattern) ":") :blank)
                      "...")
  parameter (:seq
             (:choice mutable_specifier :blank)
             (:field :pattern (:choice _pattern self))
             ":"
             (:field :type _type))
  extern_modifier (:seq "extern" (:choice string_literal :blank))
  visibility_modifier (:choice
                       crate
                       (:seq
                        "pub"
                        (:choice (:seq "(" (:choice self super crate (:seq "in" _path)) ")") :blank)))
  _type (:choice
         abstract_type
         reference_type
         metavariable
         pointer_type
         generic_type
         scoped_type_identifier
         tuple_type
         unit_type
         array_type
         function_type
         _type_identifier
         macro_invocation
         never_type
         dynamic_type
         bounded_type
         removed_trait_bound
         (:alias
          (:choice
           "u8"
           "i8"
           "u16"
           "i16"
           "u32"
           "i32"
           "u64"
           "i64"
           "u128"
           "i128"
           "isize"
           "usize"
           "f32"
           "f64"
           "bool"
           "str"
           "char")
          primitive_type))
  bracketed_type (:seq "<" (:choice _type qualified_type) ">")
  qualified_type (:seq (:field :type _type) "as" (:field :alias _type))
  lifetime (:prec 1 (:seq "'" identifier))
  array_type (:seq
              "["
              (:field :element _type)
              (:choice (:seq ";" (:field :length _expression)) :blank)
              "]")
  for_lifetimes (:seq
                 "for"
                 "<"
                 (:seq lifetime (:repeat (:seq "," lifetime)))
                 (:choice "," :blank)
                 ">")
  function_type (:seq
                 (:choice for_lifetimes :blank)
                 (:prec 15
                  (:seq
                   (:choice
                    (:field :trait (:choice _type_identifier scoped_type_identifier))
                    (:seq (:choice function_modifiers :blank) "fn"))
                   (:field :parameters parameters)))
                 (:choice (:seq "->" (:field :return_type _type)) :blank))
  tuple_type (:seq "(" (:seq _type (:repeat (:seq "," _type))) (:choice "," :blank) ")")
  unit_type (:seq "(" ")")
  generic_function (:prec 1
                    (:seq
                     (:field :function (:choice identifier scoped_identifier field_expression))
                     "::"
                     (:field :type_arguments type_arguments)))
  generic_type (:prec 1
                (:seq
                 (:field :type
                  (:choice _type_identifier _reserved_identifier scoped_type_identifier))
                 (:field :type_arguments type_arguments)))
  generic_type_with_turbofish (:seq
                               (:field :type (:choice _type_identifier scoped_identifier))
                               "::"
                               (:field :type_arguments type_arguments))
  bounded_type (:prec-left -1
                (:seq (:choice lifetime _type use_bounds) "+" (:choice lifetime _type use_bounds)))
  use_bounds (:seq
              "use"
              (:token (:prec 1 "<"))
              (:choice
               (:seq
                (:choice lifetime _type_identifier)
                (:repeat (:seq "," (:choice lifetime _type_identifier))))
               :blank)
              (:choice "," :blank)
              ">")
  type_arguments (:seq
                  (:token (:prec 1 "<"))
                  (:seq
                   (:seq
                    (:choice _type type_binding lifetime _literal block)
                    (:choice trait_bounds :blank))
                   (:repeat
                    (:seq
                     ","
                     (:seq
                      (:choice _type type_binding lifetime _literal block)
                      (:choice trait_bounds :blank)))))
                  (:choice "," :blank)
                  ">")
  type_binding (:seq
                (:field :name _type_identifier)
                (:field :type_arguments (:choice type_arguments :blank))
                "="
                (:field :type _type))
  reference_type (:seq
                  "&"
                  (:choice lifetime :blank)
                  (:choice mutable_specifier :blank)
                  (:field :type _type))
  pointer_type (:seq "*" (:choice "const" mutable_specifier) (:field :type _type))
  never_type "!"
  abstract_type (:seq
                 "impl"
                 (:choice (:seq "for" type_parameters) :blank)
                 (:field :trait
                  (:prec 1
                   (:choice
                    _type_identifier
                    scoped_type_identifier
                    removed_trait_bound
                    generic_type
                    function_type
                    tuple_type
                    bounded_type))))
  dynamic_type (:seq
                "dyn"
                (:field :trait
                 (:choice
                  higher_ranked_trait_bound
                  _type_identifier
                  scoped_type_identifier
                  generic_type
                  function_type
                  tuple_type)))
  mutable_specifier "mut"
  _expression_except_range (:choice
                            unary_expression
                            reference_expression
                            try_expression
                            binary_expression
                            assignment_expression
                            compound_assignment_expr
                            type_cast_expression
                            call_expression
                            return_expression
                            yield_expression
                            _literal
                            (:prec-left 0 identifier)
                            (:alias
                             (:choice
                              "u8"
                              "i8"
                              "u16"
                              "i16"
                              "u32"
                              "i32"
                              "u64"
                              "i64"
                              "u128"
                              "i128"
                              "isize"
                              "usize"
                              "f32"
                              "f64"
                              "bool"
                              "str"
                              "char")
                             identifier)
                            (:prec-left 0 _reserved_identifier)
                            self
                            scoped_identifier
                            generic_function
                            await_expression
                            field_expression
                            array_expression
                            tuple_expression
                            (:prec 1 macro_invocation)
                            unit_expression
                            break_expression
                            continue_expression
                            index_expression
                            metavariable
                            closure_expression
                            parenthesized_expression
                            struct_expression
                            _expression_ending_with_block)
  _expression (:choice _expression_except_range range_expression)
  _expression_ending_with_block (:choice
                                 unsafe_block
                                 async_block
                                 gen_block
                                 try_block
                                 block
                                 if_expression
                                 match_expression
                                 while_expression
                                 loop_expression
                                 for_expression
                                 const_block)
  macro_invocation (:seq
                    (:field :macro (:choice scoped_identifier identifier _reserved_identifier))
                    "!"
                    (:alias delim_token_tree token_tree))
  delim_token_tree (:choice
                    (:seq "(" (:repeat _delim_tokens) ")")
                    (:seq "[" (:repeat _delim_tokens) "]")
                    (:seq "{" (:repeat _delim_tokens) "}"))
  _delim_tokens (:choice _non_delim_token (:alias delim_token_tree token_tree))
  _non_delim_token (:choice _non_special_token "$")
  scoped_identifier (:seq
                     (:field :path
                      (:choice
                       (:choice
                        _path
                        bracketed_type
                        (:alias generic_type_with_turbofish generic_type))
                       :blank))
                     "::"
                     (:field :name (:choice identifier super)))
  scoped_type_identifier_in_expression_position (:prec -2
                                                 (:seq
                                                  (:field :path
                                                   (:choice
                                                    (:choice
                                                     _path
                                                     (:alias
                                                      generic_type_with_turbofish
                                                      generic_type))
                                                    :blank))
                                                  "::"
                                                  (:field :name _type_identifier)))
  scoped_type_identifier (:seq
                          (:field :path
                           (:choice
                            (:choice
                             _path
                             (:alias generic_type_with_turbofish generic_type)
                             bracketed_type
                             generic_type)
                            :blank))
                          "::"
                          (:field :name _type_identifier))
  range_expression (:prec-left 1
                    (:choice
                     (:seq _expression (:choice ".." "..." "..=") _expression)
                     (:seq _expression "..")
                     (:seq ".." _expression)
                     ".."))
  unary_expression (:prec 12 (:seq (:choice "-" "*" "!") _expression))
  try_expression (:prec 13 (:seq _expression "?"))
  reference_expression (:prec 12
                        (:seq
                         "&"
                         (:choice
                          (:seq "raw" (:choice "const" mutable_specifier))
                          (:choice mutable_specifier :blank))
                         (:field :value _expression)))
  binary_expression (:choice
                     (:prec-left 3
                      (:seq
                       (:field :left _expression)
                       (:field :operator "&&")
                       (:field :right _expression)))
                     (:prec-left 2
                      (:seq
                       (:field :left _expression)
                       (:field :operator "||")
                       (:field :right _expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left _expression)
                       (:field :operator "&")
                       (:field :right _expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left _expression)
                       (:field :operator "|")
                       (:field :right _expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left _expression)
                       (:field :operator "^")
                       (:field :right _expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "==" "!=" "<" "<=" ">" ">="))
                       (:field :right _expression)))
                     (:prec-left 8
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "<<" ">>"))
                       (:field :right _expression)))
                     (:prec-left 9
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "+" "-"))
                       (:field :right _expression)))
                     (:prec-left 10
                      (:seq
                       (:field :left _expression)
                       (:field :operator (:choice "*" "/" "%"))
                       (:field :right _expression))))
  assignment_expression (:prec-left 0
                         (:seq (:field :left _expression) "=" (:field :right _expression)))
  compound_assignment_expr (:prec-left 0
                            (:seq
                             (:field :left _expression)
                             (:field :operator
                              (:choice "+=" "-=" "*=" "/=" "%=" "&=" "|=" "^=" "<<=" ">>="))
                             (:field :right _expression)))
  type_cast_expression (:prec-left 11 (:seq (:field :value _expression) "as" (:field :type _type)))
  return_expression (:choice (:prec-left 0 (:seq "return" _expression)) (:prec -1 "return"))
  yield_expression (:choice (:prec-left 0 (:seq "yield" _expression)) (:prec -1 "yield"))
  call_expression (:prec 15
                   (:seq (:field :function _expression_except_range) (:field :arguments arguments)))
  arguments (:seq
             "("
             (:choice
              (:seq
               (:seq (:repeat attribute_item) _expression)
               (:repeat (:seq "," (:seq (:repeat attribute_item) _expression))))
              :blank)
             (:choice "," :blank)
             ")")
  array_expression (:seq
                    "["
                    (:repeat attribute_item)
                    (:choice
                     (:seq _expression ";" (:field :length _expression))
                     (:seq
                      (:choice
                       (:seq
                        (:seq (:repeat attribute_item) _expression)
                        (:repeat (:seq "," (:seq (:repeat attribute_item) _expression))))
                       :blank)
                      (:choice "," :blank)))
                    "]")
  parenthesized_expression (:seq "(" _expression ")")
  tuple_expression (:seq
                    "("
                    (:repeat attribute_item)
                    (:seq _expression ",")
                    (:repeat (:seq _expression ","))
                    (:choice _expression :blank)
                    ")")
  unit_expression (:seq "(" ")")
  struct_expression (:seq
                     (:field :name
                      (:choice
                       _type_identifier
                       (:alias scoped_type_identifier_in_expression_position scoped_type_identifier)
                       generic_type_with_turbofish))
                     (:field :body field_initializer_list))
  field_initializer_list (:seq
                          "{"
                          (:choice
                           (:seq
                            (:choice
                             shorthand_field_initializer
                             field_initializer
                             base_field_initializer)
                            (:repeat
                             (:seq
                              ","
                              (:choice
                               shorthand_field_initializer
                               field_initializer
                               base_field_initializer))))
                           :blank)
                          (:choice "," :blank)
                          "}")
  shorthand_field_initializer (:seq (:repeat attribute_item) identifier)
  field_initializer (:seq
                     (:repeat attribute_item)
                     (:field :field (:choice _field_identifier integer_literal))
                     ":"
                     (:field :value _expression))
  base_field_initializer (:seq ".." _expression)
  if_expression (:prec-right 0
                 (:seq
                  "if"
                  (:field :condition _condition)
                  (:field :consequence block)
                  (:choice (:field :alternative else_clause) :blank)))
  let_condition (:seq
                 "let"
                 (:field :pattern _pattern)
                 "="
                 (:field :value (:prec-left 3 _expression)))
  _let_chain (:prec-left 3
              (:choice
               (:seq _let_chain "&&" let_condition)
               (:seq _let_chain "&&" _expression)
               (:seq let_condition "&&" _expression)
               (:seq let_condition "&&" let_condition)
               (:seq _expression "&&" let_condition)))
  _condition (:choice _expression let_condition (:alias _let_chain let_chain))
  else_clause (:seq "else" (:choice block if_expression))
  match_expression (:seq "match" (:field :value _expression) (:field :body match_block))
  match_block (:seq
               "{"
               (:choice (:seq (:repeat match_arm) (:alias last_match_arm match_arm)) :blank)
               "}")
  match_arm (:prec-right 0
             (:seq
              (:repeat (:choice attribute_item inner_attribute_item))
              (:field :pattern match_pattern)
              "=>"
              (:choice
               (:seq (:field :value _expression) ",")
               (:field :value (:prec 1 _expression_ending_with_block)))))
  last_match_arm (:seq
                  (:repeat (:choice attribute_item inner_attribute_item))
                  (:field :pattern match_pattern)
                  "=>"
                  (:field :value _expression)
                  (:choice "," :blank))
  match_pattern (:seq _pattern (:choice (:seq "if" (:field :condition _condition)) :blank))
  while_expression (:seq
                    (:choice (:seq label ":") :blank)
                    "while"
                    (:field :condition _condition)
                    (:field :body block))
  loop_expression (:seq (:choice (:seq label ":") :blank) "loop" (:field :body block))
  for_expression (:seq
                  (:choice (:seq label ":") :blank)
                  "for"
                  (:field :pattern _pattern)
                  "in"
                  (:field :value _expression)
                  (:field :body block))
  const_block (:seq "const" (:field :body block))
  closure_expression (:prec -1
                      (:seq
                       (:choice "static" :blank)
                       (:choice "async" :blank)
                       (:choice "move" :blank)
                       (:field :parameters closure_parameters)
                       (:choice
                        (:seq
                         (:choice (:seq "->" (:field :return_type _type)) :blank)
                         (:field :body block))
                        (:field :body (:choice _expression "_")))))
  closure_parameters (:seq
                      "|"
                      (:choice
                       (:seq
                        (:choice _pattern parameter)
                        (:repeat (:seq "," (:choice _pattern parameter))))
                       :blank)
                      "|")
  label (:seq "'" identifier)
  break_expression (:prec-left 0 (:seq "break" (:choice label :blank) (:choice _expression :blank)))
  continue_expression (:prec-left 0 (:seq "continue" (:choice label :blank)))
  index_expression (:prec 15 (:seq _expression "[" _expression "]"))
  await_expression (:prec 14 (:seq _expression "." "await"))
  field_expression (:prec 14
                    (:seq
                     (:field :value _expression)
                     "."
                     (:field :field (:choice _field_identifier integer_literal))))
  unsafe_block (:seq "unsafe" block)
  async_block (:seq "async" (:choice "move" :blank) block)
  gen_block (:seq "gen" (:choice "move" :blank) block)
  try_block (:seq "try" block)
  block (:seq
         (:choice (:seq label ":") :blank)
         "{"
         (:repeat _statement)
         (:choice _expression :blank)
         "}")
  _pattern (:choice
            _literal_pattern
            (:alias
             (:choice
              "u8"
              "i8"
              "u16"
              "i16"
              "u32"
              "i32"
              "u64"
              "i64"
              "u128"
              "i128"
              "isize"
              "usize"
              "f32"
              "f64"
              "bool"
              "str"
              "char")
             identifier)
            identifier
            scoped_identifier
            generic_pattern
            tuple_pattern
            tuple_struct_pattern
            struct_pattern
            _reserved_identifier
            ref_pattern
            slice_pattern
            captured_pattern
            reference_pattern
            remaining_field_pattern
            mut_pattern
            range_pattern
            or_pattern
            const_block
            macro_invocation
            "_")
  generic_pattern (:seq
                   (:choice identifier scoped_identifier)
                   "::"
                   (:field :type_arguments type_arguments))
  tuple_pattern (:seq
                 "("
                 (:choice
                  (:seq
                   (:choice _pattern closure_expression)
                   (:repeat (:seq "," (:choice _pattern closure_expression))))
                  :blank)
                 (:choice "," :blank)
                 ")")
  slice_pattern (:seq
                 "["
                 (:choice (:seq _pattern (:repeat (:seq "," _pattern))) :blank)
                 (:choice "," :blank)
                 "]")
  tuple_struct_pattern (:seq
                        (:field :type
                         (:choice
                          identifier
                          scoped_identifier
                          (:alias generic_type_with_turbofish generic_type)))
                        "("
                        (:choice (:seq _pattern (:repeat (:seq "," _pattern))) :blank)
                        (:choice "," :blank)
                        ")")
  struct_pattern (:seq
                  (:field :type (:choice _type_identifier scoped_type_identifier))
                  "{"
                  (:choice
                   (:seq
                    (:choice field_pattern remaining_field_pattern)
                    (:repeat (:seq "," (:choice field_pattern remaining_field_pattern))))
                   :blank)
                  (:choice "," :blank)
                  "}")
  field_pattern (:seq
                 (:choice "ref" :blank)
                 (:choice mutable_specifier :blank)
                 (:choice
                  (:field :name (:alias identifier shorthand_field_identifier))
                  (:seq (:field :name _field_identifier) ":" (:field :pattern _pattern))))
  remaining_field_pattern ".."
  mut_pattern (:prec -1 (:seq mutable_specifier _pattern))
  range_pattern (:choice
                 (:seq
                  (:field :left (:choice _literal_pattern _path))
                  (:choice
                   (:seq
                    (:choice "..." "..=" "..")
                    (:field :right (:choice _literal_pattern _path)))
                   ".."))
                 (:seq (:choice "..=" "..") (:field :right (:choice _literal_pattern _path))))
  ref_pattern (:seq "ref" _pattern)
  captured_pattern (:seq identifier "@" _pattern)
  reference_pattern (:seq "&" (:choice mutable_specifier :blank) _pattern)
  or_pattern (:prec-left -2 (:choice (:seq _pattern "|" _pattern) (:seq "|" _pattern)))
  _literal (:choice
            string_literal
            raw_string_literal
            char_literal
            boolean_literal
            integer_literal
            float_literal)
  _literal_pattern (:choice
                    string_literal
                    raw_string_literal
                    char_literal
                    boolean_literal
                    integer_literal
                    float_literal
                    negative_literal)
  negative_literal (:seq "-" (:choice integer_literal float_literal))
  integer_literal (:token
                   (:seq
                    (:choice
                     (:pattern "[0-9][0-9_]*")
                     (:pattern "0x[0-9a-fA-F_]+")
                     (:pattern "0b[01_]+")
                     (:pattern "0o[0-7_]+"))
                    (:choice
                     (:choice
                      "u8"
                      "i8"
                      "u16"
                      "i16"
                      "u32"
                      "i32"
                      "u64"
                      "i64"
                      "u128"
                      "i128"
                      "isize"
                      "usize"
                      "f32"
                      "f64")
                     :blank)))
  string_literal (:seq
                  (:alias (:pattern "[bc]?\"") "\"")
                  (:repeat (:choice escape_sequence string_content))
                  (:alias string_close "\""))
  raw_string_literal (:seq
                      _raw_string_literal_start
                      (:alias raw_string_literal_content string_content)
                      _raw_string_literal_end)
  char_literal (:token
                (:seq
                 (:choice "b" :blank)
                 "'"
                 (:choice
                  (:choice
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu]")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}")
                     (:pattern "x[0-9a-fA-F]{2}")))
                   (:pattern "[^\\\\']"))
                  :blank)
                 "'"))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu]")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}")
                     (:pattern "x[0-9a-fA-F]{2}"))))
  boolean_literal (:choice "true" "false")
  comment (:choice line_comment block_comment)
  line_comment (:seq
                "//"
                (:choice
                 (:seq (:token-immediate (:prec 2 (:pattern "\\/\\/"))) (:pattern ".*"))
                 (:seq
                  _line_doc_comment_marker
                  (:field :doc (:alias _line_doc_content doc_comment)))
                 (:token-immediate (:prec 1 (:pattern ".*")))))
  _line_doc_comment_marker (:choice
                            (:field :outer
                             (:alias _outer_line_doc_comment_marker outer_doc_comment_marker))
                            (:field :inner
                             (:alias _inner_line_doc_comment_marker inner_doc_comment_marker)))
  _inner_line_doc_comment_marker (:token-immediate (:prec 2 "!"))
  _outer_line_doc_comment_marker (:token-immediate (:prec 2 "/"))
  block_comment (:seq
                 "/*"
                 (:choice
                  (:choice
                   (:seq
                    _block_doc_comment_marker
                    (:choice (:field :doc (:alias _block_comment_content doc_comment)) :blank))
                   _block_comment_content)
                  :blank)
                 "*/")
  _block_doc_comment_marker (:choice
                             (:field :outer
                              (:alias _outer_block_doc_comment_marker outer_doc_comment_marker))
                             (:field :inner
                              (:alias _inner_block_doc_comment_marker inner_doc_comment_marker)))
  _path (:choice
         self
         (:alias
          (:choice
           "u8"
           "i8"
           "u16"
           "i16"
           "u32"
           "i32"
           "u64"
           "i64"
           "u128"
           "i128"
           "isize"
           "usize"
           "f32"
           "f64"
           "bool"
           "str"
           "char")
          identifier)
         metavariable
         super
         crate
         identifier
         scoped_identifier
         _reserved_identifier)
  identifier (:pattern "(r#)?[_\\p{XID_Start}][_\\p{XID_Continue}]*")
  shebang (:pattern "#![\\r\\f\\t\\v ]*([^\\[\\n].*)?\\n")
  _reserved_identifier (:alias (:choice "default" "union" "gen" "raw") identifier)
  _type_identifier (:alias identifier type_identifier)
  _field_identifier (:alias identifier field_identifier)
  self "self"
  super "super"
  crate "crate"
  metavariable (:pattern "\\$[a-zA-Z_]\\w*")}}
