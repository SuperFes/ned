# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "crystal"
 :word identifier
 :extras [(:pattern "\\s")
          _line_continuation
          loc_pragma_push
          loc_pragma_pop
          loc_pragma_location
          comment
          heredoc_body]
 :conflicts [[_bare_type proc_type]
             [_bare_type proc_type parenthesized_proc_type]
             [proc_type]
             [no_args_proc_type]
             [parenthesized_proc_type]
             [fun_def]
             [call type_declaration]]
 :precedences [["index_operator"
                "dot_operator"
                "unary_operator"
                "exponential_operator"
                "multiplicative_operator"
                "additive_operator"
                "shift_operator"
                "binary_and_operator"
                "binary_or_operator"
                "equality_operator"
                "comparison_operator"
                "logical_and_operator"
                "logical_or_operator"
                "range_operator"
                "ternary_operator"
                "block_ampersand"
                "assignment_operator"
                "splat_operator"
                named_expr
                "comma"]
               ["atomic_type" "union_type" "splat_type"]
               ["atomic_type" "union_type" "proc_type"]
               ["atomic_type" "union_type" _splattable_type]
               [lhs_splat "assignment_operator"]
               ["brace_block_call" _expression]
               [_expression "do_end_block_call"]
               ["brace_block_call" "no_block_call"]
               ["no_block_call" "do_end_block_call"]
               ["ampersand_block_call" _expression]
               ["ampersand_block_call" "no_block_call"]
               [union_type array]
               [proc_type array]
               [union_type hash]
               [_expression type_declaration]
               [top_level_fun_def empty_parens]
               [implicit_object_call_chainable implicit_object_call_unchainable "block_ampersand"]
               [implicit_object_call_chainable _implicit_object_call]
               [pseudo_call_argument_list _parenthesized_type]]
 :externals [_line_break
             _line_continuation
             _start_of_brace_block
             _start_of_hash_or_tuple
             _start_of_named_tuple
             _start_of_tuple_type
             _start_of_named_tuple_type
             _start_of_index_operator
             _end_of_with_expression
             unary_plus
             unary_minus
             binary_plus
             binary_minus
             unary_wrapping_plus
             unary_wrapping_minus
             binary_wrapping_plus
             binary_wrapping_minus
             _pointer_star
             _unary_star
             _binary_star
             _unary_double_star
             _binary_double_star
             _block_ampersand
             binary_ampersand
             _beginless_range_operator
             _regex_start
             _binary_slash
             _binary_double_slash
             _regular_if_keyword
             _modifier_if_keyword
             _regular_unless_keyword
             _modifier_unless_keyword
             _regular_rescue_keyword
             _modifier_rescue_keyword
             _regular_ensure_keyword
             _modifier_ensure_keyword
             _modulo_operator
             _start_of_symbol
             unquoted_symbol_content
             _type_field_colon
             _string_literal_start
             _delimited_string_contents
             _string_literal_end
             _command_literal_start
             _command_literal_end
             _string_percent_literal_start
             _command_percent_literal_start
             _string_array_percent_literal_start
             _symbol_array_percent_literal_start
             _regex_percent_literal_start
             _percent_literal_end
             _delimited_array_element_start
             _delimited_array_element_end
             heredoc_start
             _heredoc_body_start
             heredoc_content
             heredoc_end
             regex_modifier
             _macro_start
             _macro_delimiter_end
             _macro_delimiter_else
             _macro_delimiter_elsif
             macro_content
             macro_content_nesting
             _start_of_parenless_args
             _end_of_range
             _start_of_macro_var_exps
             _error_recovery]
 :inline []
 :supertypes []
 :rules
 {expressions (:seq (:choice _statements :blank))
  _macro_def_literal_content (:choice
                              (:alias string macro_content)
                              (:alias macro_content_nesting macro_content)
                              (:seq
                               (:alias
                                (:choice
                                 (:pattern "abstract\\sclass")
                                 (:pattern "abstract\\sstruct")
                                 "annotation"
                                 "begin"
                                 "case"
                                 "class"
                                 "def"
                                 "do"
                                 "enum"
                                 "fun"
                                 "if"
                                 "lib"
                                 "macro"
                                 "module"
                                 "select"
                                 "struct"
                                 "union"
                                 "unless"
                                 "until"
                                 "while")
                                macro_content)
                               (:repeat _macro_def_content)
                               (:alias "end" macro_content)))
  _macro_literal_content (:choice (:alias string macro_content) macro_content)
  _terminator (:choice _line_break ";")
  _statements (:choice
               (:seq
                (:repeat1 (:choice (:seq _statement _terminator) (:prec -1 ";")))
                (:choice _statement :blank))
               _statement)
  _lib_statements (:choice
                   (:seq
                    (:repeat1 (:choice (:seq _lib_statement _terminator) (:prec -1 ";")))
                    (:choice _lib_statement :blank))
                   _lib_statement)
  _enum_statements (:choice
                    (:seq
                     (:repeat1 (:choice (:seq _enum_statement _terminator) (:prec -1 ";")))
                     (:choice _enum_statement :blank))
                    _enum_statement)
  _inline_statement (:choice
                     modifier_if
                     modifier_unless
                     modifier_rescue
                     modifier_ensure
                     return
                     next
                     break)
  _statement (:choice
              _expression
              _inline_statement
              const_assign
              (:alias multi_assign assign)
              annotation
              annotation_def
              module_def
              class_def
              struct_def
              enum_def
              lib_def
              alias
              method_def
              abstract_method_def
              macro_def
              (:alias top_level_fun_def fun_def)
              visibility_modifier
              require
              include
              extend)
  _lib_statement (:choice
                  _macro_node
                  alias
                  fun_def
                  type_def
                  c_struct_def
                  union_def
                  enum_def
                  global_var
                  const_assign
                  annotation
                  visibility_modifier)
  _enum_statement (:choice
                   _macro_node
                   constant
                   const_assign
                   method_def
                   macro_def
                   class_var
                   (:alias class_var_assign assign)
                   annotation
                   visibility_modifier)
  parenthesized_expressions (:seq "(" _statements ")")
  _expression (:choice
               _macro_node
               (:ref "nil")
               (:ref "true")
               (:ref "false")
               integer
               float
               char
               array
               hash
               string
               chained_string
               (:alias string_percent_literal string)
               (:alias string_array_percent_literal array)
               (:alias operator_symbol symbol)
               (:alias unquoted_symbol symbol)
               (:alias quoted_symbol symbol)
               (:alias symbol_array_percent_literal array)
               heredoc_start
               range
               (:alias beginless_range range)
               tuple
               named_tuple
               proc
               method_proc
               command
               (:alias command_percent_literal command)
               regex
               (:alias regex_percent_literal regex)
               (:alias empty_parens (:ref "nil"))
               (:alias parenthesized_expressions expressions)
               begin
               self
               constant
               generic_instance_type
               nilable_constant
               pseudo_constant
               special_variable
               (:alias global_match_data_index special_variable)
               identifier
               instance_var
               class_var
               macro_var
               type_declaration
               while
               until
               if
               unless
               conditional
               case
               (:alias exhaustive_case case)
               select
               call
               (:alias additive_operator call)
               (:alias unary_additive_operator call)
               (:alias multiplicative_operator call)
               (:alias exponential_operator call)
               (:alias shift_operator call)
               (:alias complement_operator call)
               (:alias binary_and_operator call)
               (:alias binary_or_operator call)
               (:alias equality_operator call)
               (:alias comparison_operator call)
               (:alias index_operator index_call)
               (:alias pseudo_call call)
               index_call
               array_like
               hash_like
               assign
               (:alias uninitialized_assign assign)
               (:alias operator_assign op_assign)
               not
               and
               or
               asm
               yield
               typeof
               pointerof
               sizeof
               instance_sizeof
               alignof
               instance_alignof
               offsetof)
  comment (:pattern "#.*")
  empty_parens (:seq "(" ")")
  (:ref "nil") "nil"
  (:ref "true") "true"
  (:ref "false") "false"
  integer (:choice
           (:seq
            (:choice (:alias unary_minus "-") (:alias unary_plus "+"))
            (:token-immediate
             (:seq
              (:choice
               (:seq "0b" (:repeat (:pattern "[01_]")))
               (:seq "0o" (:repeat (:pattern "[0-7_]")))
               (:seq "0x" (:repeat (:pattern "[0-9a-fA-F_]")))
               (:choice "0" (:seq "0_" (:repeat (:pattern "[0-9_]"))))
               (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9_]"))))
              (:choice (:choice "u8" "u16" "u32" "u64" "u128" "i8" "i16" "i32" "i64" "i128") :blank))))
           (:token
            (:seq
             (:choice
              (:seq "0b" (:repeat (:pattern "[01_]")))
              (:seq "0o" (:repeat (:pattern "[0-7_]")))
              (:seq "0x" (:repeat (:pattern "[0-9a-fA-F_]")))
              (:choice "0" (:seq "0_" (:repeat (:pattern "[0-9_]"))))
              (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9_]"))))
             (:choice (:choice "u8" "u16" "u32" "u64" "u128" "i8" "i16" "i32" "i64" "i128") :blank))))
  float (:choice
         (:seq
          (:choice (:alias unary_minus "-") (:alias unary_plus "+"))
          (:token-immediate
           (:choice
            (:seq
             (:choice
              (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9_]")))
              (:choice "0" (:seq "0_" (:repeat (:pattern "[0-9_]")))))
             (:seq (:pattern "\\.[0-9]") (:repeat (:pattern "[0-9_]")))
             (:choice
              (:seq
               (:pattern "[eE]")
               (:choice
                (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9_]")))
                :blank))
              :blank)
             (:choice (:choice "f32" "f64") :blank))
            (:seq
             (:choice
              (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9_]")))
              (:choice "0" (:seq "0_" (:repeat (:pattern "[0-9_]")))))
             (:choice (:seq (:pattern "\\.[0-9]") (:repeat (:pattern "[0-9_]"))) :blank)
             (:seq
              (:pattern "[eE]")
              (:choice
               (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9_]")))
               :blank))
             (:choice (:choice "f32" "f64") :blank))
            (:seq
             (:choice
              (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9_]")))
              (:choice "0" (:seq "0_" (:repeat (:pattern "[0-9_]")))))
             (:choice (:seq (:pattern "\\.[0-9]") (:repeat (:pattern "[0-9_]"))) :blank)
             (:choice
              (:seq
               (:pattern "[eE]")
               (:choice
                (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9_]")))
                :blank))
              :blank)
             (:choice "f32" "f64")))))
         (:token
          (:choice
           (:seq
            (:choice
             (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9_]")))
             (:choice "0" (:seq "0_" (:repeat (:pattern "[0-9_]")))))
            (:seq (:pattern "\\.[0-9]") (:repeat (:pattern "[0-9_]")))
            (:choice
             (:seq
              (:pattern "[eE]")
              (:choice
               (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9_]")))
               :blank))
             :blank)
            (:choice (:choice "f32" "f64") :blank))
           (:seq
            (:choice
             (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9_]")))
             (:choice "0" (:seq "0_" (:repeat (:pattern "[0-9_]")))))
            (:choice (:seq (:pattern "\\.[0-9]") (:repeat (:pattern "[0-9_]"))) :blank)
            (:seq
             (:pattern "[eE]")
             (:choice
              (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9_]")))
              :blank))
            (:choice (:choice "f32" "f64") :blank))
           (:seq
            (:choice
             (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9_]")))
             (:choice "0" (:seq "0_" (:repeat (:pattern "[0-9_]")))))
            (:choice (:seq (:pattern "\\.[0-9]") (:repeat (:pattern "[0-9_]"))) :blank)
            (:choice
             (:seq
              (:pattern "[eE]")
              (:choice
               (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9_]")))
               :blank))
             :blank)
            (:choice "f32" "f64")))))
  char (:seq
        "'"
        (:choice
         (:alias (:token-immediate (:prec 1 (:pattern "[^\\\\]"))) literal_content)
         (:alias char_escape_sequence escape_sequence))
        (:token-immediate "'"))
  char_escape_sequence (:token-immediate
                        (:seq
                         "\\"
                         (:choice
                          "0"
                          "\\"
                          "'"
                          "a"
                          "b"
                          "e"
                          "f"
                          "n"
                          "r"
                          "t"
                          "v"
                          (:seq
                           "u"
                           (:choice (:pattern "[0-9a-fA-F]{4}") (:pattern "\\{[0-9a-fA-F]{1,6}\\}"))))))
  string (:seq
          (:alias _string_literal_start "\"")
          (:choice _string_literal_content :blank)
          (:alias _string_literal_end "\""))
  _string_literal_content (:repeat1
                           (:choice
                            _line_continuation
                            (:alias _delimited_string_contents literal_content)
                            (:alias string_escape_sequence escape_sequence)
                            (:alias ignored_backslash escape_sequence)
                            interpolation))
  chained_string (:seq string (:repeat1 string))
  ignored_backslash (:token-immediate (:seq "\\" (:pattern "[^\\n\\\\\"abefnrtuvx0-7]")))
  string_escape_sequence (:token-immediate
                          (:seq
                           "\\"
                           (:choice
                            (:choice "\\" "\"" "a" "b" "e" "f" "n" "r" "t" "v")
                            (:pattern "[0-7]{1,3}")
                            (:seq "x" (:pattern "[0-9a-fA-F]{2}"))
                            (:seq
                             "u"
                             (:choice
                              (:pattern "[0-9a-fA-F]{4}")
                              (:seq
                               "{"
                               (:pattern "[0-9a-fA-F]{1,6}")
                               (:repeat (:seq " " (:pattern "[0-9a-fA-F]{1,6}")))
                               "}"))))))
  interpolation (:seq (:token (:prec 1 "#{")) (:choice _expression _inline_statement) "}")
  string_percent_literal (:seq
                          (:alias _string_percent_literal_start "\"")
                          (:choice _string_percent_literal_content :blank)
                          (:alias _percent_literal_end "\""))
  _string_percent_literal_content (:repeat1
                                   (:choice
                                    (:alias _delimited_string_contents literal_content)
                                    interpolation
                                    (:alias string_escape_sequence escape_sequence)
                                    (:alias ignored_backslash escape_sequence)))
  string_array_percent_literal (:seq
                                (:alias _string_array_percent_literal_start "[")
                                (:repeat (:alias percent_literal_array_word string))
                                (:alias _percent_literal_end "]"))
  symbol_array_percent_literal (:seq
                                (:alias _symbol_array_percent_literal_start "[")
                                (:repeat (:alias percent_literal_array_word symbol))
                                (:alias _percent_literal_end "]"))
  percent_literal_array_word (:seq
                              _delimited_array_element_start
                              (:repeat
                               (:choice
                                (:alias _delimited_string_contents literal_content)
                                (:alias percent_array_escape_sequence escape_sequence)))
                              _delimited_array_element_end)
  percent_array_escape_sequence (:token-immediate (:pattern "\\\\[\\s})\\]>|]"))
  heredoc_body (:seq
                _heredoc_body_start
                (:repeat
                 (:choice
                  (:alias heredoc_content literal_content)
                  interpolation
                  (:seq (:pattern "\\s*") (:alias string_escape_sequence escape_sequence))
                  (:seq (:pattern "\\s*") (:alias ignored_backslash escape_sequence))
                  _line_continuation))
                heredoc_end)
  operator_symbol (:seq
                   (:alias _start_of_symbol ":")
                   (:alias
                    (:token-immediate
                     (:choice
                      "+"
                      "-"
                      "*"
                      "/"
                      "//"
                      "%"
                      "&"
                      "|"
                      "^"
                      "**"
                      ">>"
                      "<<"
                      "=="
                      "!="
                      "<"
                      "<="
                      ">"
                      ">="
                      "<=>"
                      "==="
                      "[]"
                      "[]?"
                      "[]="
                      "!"
                      "~"
                      "!~"
                      "=~"
                      "&+"
                      "&-"
                      "&*"
                      "&**"))
                    literal_content))
  unquoted_symbol (:seq
                   (:alias _start_of_symbol ":")
                   (:alias unquoted_symbol_content literal_content))
  quoted_symbol (:seq
                 ":\""
                 (:seq
                  (:repeat
                   (:choice
                    (:alias (:token-immediate (:prec 1 (:pattern "[^\\\\\"]+"))) literal_content)
                    (:alias string_escape_sequence escape_sequence)
                    (:alias ignored_backslash escape_sequence)))
                  (:token-immediate "\"")))
  command (:seq
           (:alias _command_literal_start "`")
           (:choice _string_literal_content :blank)
           (:alias _command_literal_end "`"))
  command_percent_literal (:seq
                           (:alias _command_percent_literal_start "`")
                           (:choice _string_percent_literal_content :blank)
                           (:alias _percent_literal_end "`"))
  regex (:seq
         (:alias _regex_start "/")
         (:choice _regex_literal_content :blank)
         (:token-immediate "/")
         (:choice regex_modifier :blank))
  _regex_literal_content (:repeat1
                          (:choice interpolation (:alias regex_literal_content literal_content)))
  regex_literal_content (:prec-right 0
                         (:repeat1
                          (:choice
                           (:token-immediate (:prec 1 (:pattern "[^\\\\/]")))
                           (:token-immediate (:pattern "\\\\."))
                           _line_continuation)))
  regex_percent_literal (:seq
                         (:alias _regex_percent_literal_start "/")
                         (:choice _regex_percent_literal_content :blank)
                         (:alias _percent_literal_end "/")
                         (:choice regex_modifier :blank))
  _regex_percent_literal_content (:repeat1
                                  (:choice
                                   interpolation
                                   (:alias _delimited_string_contents literal_content)))
  array (:choice
         (:seq
          "["
          _expression
          (:repeat (:seq "," _expression))
          (:choice "," :blank)
          "]"
          (:choice (:field :of (:seq "of" _bare_type)) :blank))
         (:seq "[" "]" (:field :of (:seq "of" _bare_type))))
  hash (:choice
        (:seq
         (:alias _start_of_hash_or_tuple "{")
         hash_entry
         (:repeat (:seq "," hash_entry))
         (:choice "," :blank)
         "}"
         (:choice (:seq "of" (:field :of_key _bare_type) "=>" (:field :of_value _bare_type)) :blank))
        (:seq
         (:alias _start_of_hash_or_tuple "{")
         "}"
         (:seq "of" (:field :of_key _bare_type) "=>" (:field :of_value _bare_type))))
  hash_entry (:seq _expression "=>" _expression)
  tuple (:seq
         (:alias _start_of_hash_or_tuple "{")
         _expression
         (:repeat (:seq "," _expression))
         (:choice "," :blank)
         "}")
  named_tuple (:seq
               (:alias _start_of_named_tuple "{")
               named_expr
               (:repeat (:seq "," named_expr))
               (:choice "," :blank)
               "}")
  range (:prec-left "range_operator"
         (:seq
          (:field :begin _expression)
          (:field :operator (:alias (:choice ".." "...") operator))
          (:choice _end_of_range :blank)
          (:choice (:field :end _expression) :blank)))
  beginless_range (:prec-left "range_operator"
                   (:seq
                    (:field :operator (:alias _beginless_range_operator operator))
                    (:choice _end_of_range :blank)
                    (:choice (:field :end _expression) :blank)))
  proc (:seq
        "->"
        (:choice
         (:seq "(" (:field :params (:choice (:alias proc_param_list param_list) :blank)) ")")
         :blank)
        (:choice (:field :type (:seq (:pattern ":\\s") _bare_type)) :blank)
        (:field :block (:choice (:alias do_end_block block) (:alias brace_block block))))
  proc_param_list (:seq param (:repeat (:seq "," param)) (:choice "," :blank))
  method_proc (:prec-right 0
               (:seq
                "->"
                (:choice
                 (:seq
                  (:field :receiver (:choice identifier instance_var class_var self constant))
                  "."
                  (:field :method
                   (:choice
                    identifier
                    (:alias identifier_method_call identifier)
                    (:alias identifier_assign identifier)
                    (:alias _operator_token operator))))
                 (:choice
                  _global_method
                  (:field :method
                   (:choice
                    identifier
                    (:alias identifier_method_call identifier)
                    (:alias identifier_assign identifier)))))
                (:choice
                 (:seq "(" (:field :params (:alias type_instance_param_list param_list)) ")")
                 :blank)))
  annotation_def (:seq "annotation" (:field :name constant) (:repeat _terminator) "end")
  annotation (:seq
              "@["
              constant
              (:choice (:field :arguments (:alias annotation_argument_list argument_list)) :blank)
              "]")
  annotation_argument_list (:seq
                            "("
                            (:choice
                             (:seq
                              (:choice _expression splat double_splat named_expr out)
                              (:repeat
                               (:seq "," (:choice _expression splat double_splat named_expr out)))
                              (:choice "," :blank))
                             :blank)
                            ")")
  module_def (:seq
              "module"
              (:field :name (:choice constant generic_type))
              (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
              "end")
  class_def (:seq
             (:choice "abstract" :blank)
             "class"
             (:field :name (:choice constant generic_type))
             (:choice
              (:seq "<" (:field :superclass (:choice constant generic_instance_type)))
              :blank)
             (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
             "end")
  struct_def (:seq
              (:choice "abstract" :blank)
              "struct"
              (:field :name (:choice constant generic_type))
              (:choice
               (:seq "<" (:field :superclass (:choice constant generic_instance_type)))
               :blank)
              (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
              "end")
  enum_def (:seq
            "enum"
            (:field :name constant)
            (:choice (:field :type (:seq (:pattern ":\\s") _bare_type)) :blank)
            (:field :body (:seq (:choice (:alias _enum_statements expressions) :blank)))
            "end")
  lib_def (:seq
           "lib"
           (:field :name (:choice constant generic_type))
           (:field :body (:seq (:choice (:alias _lib_statements expressions) :blank)))
           "end")
  _base_fun_def (:prec-right 0
                 (:seq
                  "fun"
                  (:field :name (:choice identifier (:alias identifier_method_call identifier)))
                  (:choice
                   (:seq "=" (:field :real_name (:choice identifier constant string)))
                   :blank)
                  (:choice
                   (:seq
                    "("
                    (:choice (:field :params (:alias fun_param_list param_list)) :blank)
                    ")")
                   :blank)
                  (:choice (:field :type (:seq _type_field_separator _bare_type)) :blank)))
  top_level_fun_def (:seq
                     _base_fun_def
                     (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
                     "end")
  fun_def (:seq
           "fun"
           (:field :name (:choice identifier (:alias identifier_method_call identifier) constant))
           (:choice (:seq "=" (:field :real_name (:choice identifier constant string))) :blank)
           (:choice
            (:seq
             (:choice _line_break :blank)
             "("
             (:choice (:field :params (:alias fun_type_param_list param_list)) :blank)
             ")")
            :blank)
           (:choice (:field :type (:seq _type_field_separator _bare_type)) :blank))
  fun_param_list (:seq
                  fun_param
                  (:repeat (:seq "," fun_param))
                  (:choice (:seq "," (:choice "..." :blank)) :blank))
  fun_type_param_list (:seq
                       (:choice fun_param _type)
                       (:repeat (:seq "," (:choice fun_param _type)))
                       (:choice (:seq "," (:choice "..." :blank)) :blank))
  fun_param (:seq
             (:field :name (:choice identifier (:alias identifier_method_call identifier) constant))
             (:field :type (:seq _type_field_separator _bare_type)))
  _type_field_separator (:alias _type_field_colon ":")
  type_def (:seq "type" (:field :name constant) "=" (:field :type _bare_type))
  c_struct_def (:seq
                "struct"
                (:field :name constant)
                (:field :body (:alias _c_struct_expressions expressions))
                "end")
  _c_struct_expressions (:choice
                         (:seq
                          (:repeat1
                           (:choice (:seq _c_struct_expression _terminator) (:prec -1 ";")))
                          (:choice _c_struct_expression :blank))
                         _c_struct_expression)
  _c_struct_expression (:choice _macro_node include c_struct_fields)
  c_struct_fields (:seq
                   (:field :name (:seq identifier (:repeat (:seq "," identifier))))
                   _type_field_separator
                   (:field :type _bare_type))
  union_def (:seq
             "union"
             (:field :name constant)
             (:field :body (:alias _union_expressions expressions))
             "end")
  _union_expressions (:choice
                      (:seq
                       (:repeat1 (:choice (:seq _union_expression _terminator) (:prec -1 ";")))
                       (:choice _union_expression :blank))
                      _union_expression)
  _union_expression (:choice include union_fields)
  union_fields (:seq
                (:field :name (:seq identifier (:repeat (:seq "," identifier))))
                _type_field_separator
                (:field :type _bare_type))
  global_var (:seq
              (:field :name (:seq "$" identifier))
              (:choice (:seq "=" (:field :real_name (:choice identifier constant))) :blank)
              (:field :type (:seq _type_field_separator _bare_type)))
  _operator_token (:choice
                   "+"
                   "-"
                   "*"
                   "/"
                   "//"
                   "%"
                   "&"
                   "|"
                   "^"
                   "**"
                   ">>"
                   "<<"
                   "=="
                   "!="
                   "<"
                   "<="
                   ">"
                   ">="
                   "<=>"
                   "==="
                   "[]"
                   "[]?"
                   "[]="
                   "!"
                   "~"
                   "!~"
                   "=~"
                   "&+"
                   "&-"
                   "&*"
                   "&**")
  _base_method_def (:prec-right 0
                    (:seq
                     "def"
                     (:choice (:field :class (:seq (:choice constant self) ".")) :blank)
                     (:field :name
                      (:choice
                       identifier
                       (:alias identifier_method_call identifier)
                       (:alias identifier_assign identifier)
                       (:alias _operator_token operator)
                       (:alias "`" operator)))
                     (:choice (:seq "(" (:field :params (:choice param_list :blank)) ")") :blank)
                     (:choice (:field :type (:seq _type_field_separator _bare_type)) :blank)
                     (:choice (:field :forall forall) :blank)))
  method_def (:seq
              _base_method_def
              (:choice _terminator :blank)
              (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
              (:choice _rescue_else_ensure :blank)
              "end")
  abstract_method_def (:prec-left 0 (:seq "abstract" _base_method_def (:choice _terminator :blank)))
  _macro_signature (:seq
                    "macro"
                    (:field :name
                     (:choice
                      identifier
                      (:alias identifier_method_call identifier)
                      (:alias _operator_token operator)
                      (:alias "`" operator)))
                    (:choice
                     (:seq "(" (:field :params (:choice param_list :blank)) ")")
                     _terminator))
  macro_def (:seq
             _macro_signature
             _macro_start
             (:field :body (:choice (:alias (:repeat _macro_def_content) expressions) :blank))
             "end")
  include (:seq "include" (:choice constant self generic_instance_type))
  extend (:seq "extend" (:choice constant self generic_instance_type))
  forall (:seq
          "forall"
          (:alias _constant_segment constant)
          (:repeat (:seq "," (:alias _constant_segment constant))))
  param_list (:choice
              (:seq
               (:choice param splat_param double_splat_param)
               (:repeat (:seq "," (:choice param splat_param double_splat_param)))
               (:choice (:seq "," (:choice block_param :blank)) :blank))
              block_param)
  param (:seq
         (:repeat annotation)
         (:choice (:field :extern_name identifier) :blank)
         (:field :name
          (:choice
           identifier
           (:alias identifier_method_call identifier)
           instance_var
           class_var
           macro_var))
         (:choice (:field :type (:seq _type_field_separator _bare_type)) :blank)
         (:choice (:field :default (:seq "=" _expression)) :blank))
  splat_param (:seq
               (:repeat annotation)
               "*"
               (:choice (:field :name (:choice identifier instance_var class_var macro_var)) :blank)
               (:choice (:field :type (:seq _type_field_separator _bare_type)) :blank))
  double_splat_param (:seq
                      (:repeat annotation)
                      "**"
                      (:field :name (:choice identifier instance_var class_var macro_var))
                      (:choice (:field :type (:seq _type_field_separator _bare_type)) :blank))
  block_param (:seq
               (:repeat annotation)
               "&"
               (:choice (:field :name (:choice identifier instance_var class_var macro_var)) :blank)
               (:choice (:field :type (:seq (:pattern ":\\s") _bare_type)) :blank))
  _control_expressions (:choice
                        (:alias argument_list_with_parens argument_list)
                        (:alias argument_list_no_parens argument_list))
  return (:seq "return" (:choice _control_expressions :blank))
  next (:seq "next" (:choice _control_expressions :blank))
  break (:seq "break" (:choice _control_expressions :blank))
  yield (:seq
         (:choice (:seq "with" (:field :with _expression) _end_of_with_expression) :blank)
         "yield"
         (:choice _control_expressions :blank))
  typeof (:seq "typeof" "(" _expression (:repeat (:seq "," _expression)) (:choice "," :blank) ")")
  pointerof (:seq "pointerof" "(" _expression ")")
  sizeof (:seq "sizeof" "(" _bare_type ")")
  instance_sizeof (:seq "instance_sizeof" "(" _bare_type ")")
  alignof (:seq "alignof" "(" _bare_type ")")
  instance_alignof (:seq "instance_alignof" "(" _bare_type ")")
  offsetof (:seq "offsetof" "(" _bare_type "," (:choice instance_var integer) ")")
  _constant_segment (:token
                     (:seq
                      (:pattern "[\\p{Uppercase_Letter}\\p{Titlecase_Letter}]" "u")
                      (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))))
  constant (:prec-right 0
            (:seq
             (:choice "::" :blank)
             _constant_segment
             (:repeat (:seq "::" (:choice _constant_segment)))))
  nilable_constant (:seq
                    (:choice constant nilable_constant generic_instance_type)
                    (:token-immediate "?"))
  pseudo_constant (:choice "__LINE__" "__END_LINE__" "__FILE__" "__DIR__")
  special_variable (:token (:choice "$?" "$~"))
  global_match_data_index (:token (:choice (:pattern "\\$[0-9]+") (:pattern "\\$[0-9]+\\?")))
  identifier (:token
              (:seq
               (:pattern "[a-z_\\u{00a0}-\\u{10ffff}]" "u")
               (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))))
  identifier_method_call (:token
                          (:seq
                           (:pattern "[a-z_\\u{00a0}-\\u{10ffff}]" "u")
                           (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))
                           (:pattern "[?!]")))
  identifier_assign (:token
                     (:seq
                      (:pattern "[a-z_\\u{00a0}-\\u{10ffff}]" "u")
                      (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))
                      (:pattern "[=]")))
  instance_var (:token
                (:seq
                 "@"
                 (:pattern "[a-z_\\u{00a0}-\\u{10ffff}]" "u")
                 (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))))
  class_var (:token
             (:seq
              "@@"
              (:pattern "[a-z_\\u{00a0}-\\u{10ffff}]" "u")
              (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))))
  macro_var (:seq
             (:field :name
              (:alias
               (:token
                (:seq
                 "%"
                 (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u")
                 (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))))
               identifier))
             (:choice
              (:seq
               (:choice _start_of_macro_var_exps :blank)
               (:token-immediate "{")
               _expression
               (:repeat (:seq "," _expression))
               (:choice "," :blank)
               "}")
              :blank))
  self "self"
  _bare_type (:choice proc_type _splattable_type)
  _splattable_type (:choice double_splat_type splat_type _type)
  _type (:choice
         _parenthesized_type
         constant
         generic_instance_type
         union_type
         tuple_type
         named_tuple_type
         (:alias no_args_proc_type proc_type)
         (:alias parenthesized_proc_type proc_type)
         class_type
         underscore
         nilable_type
         pointer_type
         self
         typeof
         static_array_type)
  _numeric_type (:choice integer float sizeof instance_sizeof alignof instance_alignof offsetof)
  class_type (:prec "atomic_type" (:seq _type "." "class"))
  union_type (:prec-right "union_type"
              (:seq _type (:repeat1 (:prec-left "union_type" (:seq "|" _type)))))
  _parenthesized_type (:seq "(" _bare_type ")")
  proc_type (:prec "proc_type"
             (:seq
              _splattable_type
              (:repeat (:seq "," _splattable_type))
              "->"
              (:choice (:prec-dynamic 10 (:field :return _type)) :blank)))
  no_args_proc_type (:prec "proc_type"
                     (:seq "->" (:choice (:prec-dynamic 10 (:field :return _type)) :blank)))
  parenthesized_proc_type (:prec "proc_type"
                           (:seq
                            "("
                            (:choice
                             (:seq
                              _splattable_type
                              (:repeat1 (:seq "," _splattable_type))
                              (:choice "," :blank))
                             (:seq _bare_type ","))
                            ")"
                            "->"
                            (:choice (:prec-dynamic 10 (:field :return _type)) :blank)))
  splat_type (:prec "splat_type" (:seq "*" _type))
  double_splat_type (:prec "splat_type" (:seq (:alias _unary_double_star "**") _type))
  tuple_type (:seq
              (:alias _start_of_tuple_type "{")
              _splattable_type
              (:repeat (:seq "," _splattable_type))
              (:choice "," :blank)
              "}")
  named_type (:seq
              (:field :name
               (:choice
                identifier
                (:alias _constant_segment identifier)
                (:alias identifier_method_call identifier)
                string
                (:alias string_percent_literal string)))
              (:token-immediate ":")
              _bare_type)
  named_tuple_type (:seq
                    (:alias _start_of_named_tuple_type "{")
                    named_type
                    (:repeat (:seq "," named_type))
                    (:choice "," :blank)
                    "}")
  generic_type (:seq
                constant
                (:token-immediate "(")
                (:field :params (:choice (:alias type_param_list param_list) :blank))
                ")")
  type_param_splat (:seq "*" constant)
  type_param_list (:seq
                   (:choice constant _numeric_type (:alias type_param_splat splat))
                   (:repeat
                    (:seq "," (:choice constant _numeric_type (:alias type_param_splat splat))))
                   (:choice "," :blank))
  generic_instance_type (:seq
                         constant
                         (:token-immediate "(")
                         (:field :params
                          (:choice (:alias type_instance_param_list param_list) :blank))
                         ")")
  type_instance_param_list (:seq
                            (:choice
                             (:seq
                              (:choice _bare_type _numeric_type)
                              (:repeat (:seq "," (:choice _bare_type _numeric_type))))
                             (:seq named_type (:repeat (:seq "," named_type))))
                            (:choice "," :blank))
  underscore "_"
  nilable_type (:prec "atomic_type" (:seq _type "?"))
  pointer_type (:prec "atomic_type" (:seq _type (:alias _pointer_star "*")))
  static_array_type (:prec "atomic_type"
                     (:seq
                      _type
                      (:alias _start_of_index_operator "[")
                      (:choice constant _numeric_type)
                      "]"))
  _dot_call (:prec "dot_operator"
             (:seq
              (:field :receiver _expression)
              "."
              (:field :method
               (:choice
                identifier
                constant
                (:alias identifier_method_call identifier)
                (:alias _operator_token operator)
                instance_var))))
  bracket_argument_list (:seq
                         (:choice _expression splat double_splat named_expr out)
                         (:repeat
                          (:seq "," (:choice _expression splat double_splat named_expr out)))
                         (:choice "," :blank))
  _macro_node (:choice
               macro_expression
               macro_statement
               macro_begin
               macro_if
               macro_unless
               macro_for
               macro_verbatim)
  macro_expression (:seq "{{" (:choice splat double_splat _expression _inline_statement) "}}")
  macro_statement (:seq "{%" (:choice (:alias _statements expressions) :blank) "%}")
  _macro_begin_keyword (:seq "{%" "begin" "%}")
  _macro_end_keyword (:seq (:alias _macro_delimiter_end "{%") "end" "%}")
  _macro_else_keyword (:seq (:alias _macro_delimiter_else "{%") "else" "%}")
  _macro_verbatim_keyword (:seq "{%" "verbatim" "do" "%}")
  _macro_if_cond (:seq "{%" (:alias _regular_if_keyword "if") (:field :cond _expression) "%}")
  _macro_elsif_cond (:seq
                     (:alias _macro_delimiter_elsif "{%")
                     "elsif"
                     (:field :cond _expression)
                     "%}")
  _macro_unless_cond (:seq
                      "{%"
                      (:alias _regular_unless_keyword "unless")
                      (:field :cond _expression)
                      "%}")
  _macro_for_expr (:seq
                   "{%"
                   "for"
                   (:field :var (:choice underscore identifier))
                   (:repeat (:seq "," (:field :var (:choice underscore identifier))))
                   "in"
                   (:field :cond (:choice _expression splat double_splat))
                   "%}")
  macro_begin (:seq
               _macro_begin_keyword
               (:field :body (:alias (:repeat _macro_content) expressions))
               _macro_end_keyword)
  macro_if (:seq
            _macro_if_cond
            (:field :then (:alias (:repeat _macro_content) expressions))
            (:choice (:field :else (:choice macro_elsif macro_else)) :blank)
            _macro_end_keyword)
  macro_elsif (:seq
               _macro_elsif_cond
               (:field :then (:alias (:repeat _macro_content) expressions))
               (:choice (:field :else (:choice macro_elsif macro_else)) :blank))
  macro_else (:seq _macro_else_keyword (:field :body (:alias (:repeat _macro_content) expressions)))
  macro_unless (:seq
                _macro_unless_cond
                (:field :then (:alias (:repeat _macro_content) expressions))
                (:choice (:field :else macro_else) :blank)
                _macro_end_keyword)
  macro_for (:seq
             _macro_for_expr
             (:field :body (:alias (:repeat _macro_content) expressions))
             _macro_end_keyword)
  macro_verbatim (:seq
                  _macro_verbatim_keyword
                  (:field :body (:alias (:repeat _macro_content) expressions))
                  _macro_end_keyword)
  _macro_def_content (:choice _macro_node macro_var _macro_def_literal_content _terminator)
  _macro_content (:choice _macro_node macro_var _macro_literal_content _terminator)
  private "private"
  protected "protected"
  visibility_modifier (:seq
                       (:field :visibility (:choice private protected))
                       (:choice
                        call
                        module_def
                        class_def
                        struct_def
                        enum_def
                        lib_def
                        method_def
                        abstract_method_def
                        macro_def
                        const_assign
                        alias))
  array_like (:seq (:field :name (:choice constant generic_instance_type)) (:field :values tuple))
  hash_like (:seq (:field :name (:choice constant generic_instance_type)) (:field :values hash))
  call (:choice
        (:prec "no_block_call"
         (:seq
          (:choice
           _dot_call
           (:field :method (:alias identifier_method_call identifier))
           _global_method)
          (:choice
           (:field :arguments
            (:choice
             (:alias argument_list_with_parens argument_list)
             (:alias argument_list_no_parens argument_list)))
           :blank)))
        (:prec "no_block_call"
         (:seq
          (:field :method identifier)
          (:field :arguments
           (:choice
            (:alias argument_list_with_parens argument_list)
            (:alias argument_list_no_parens argument_list)))))
        (:prec "brace_block_call"
         (:seq
          (:choice
           _dot_call
           (:field :method (:alias identifier_method_call identifier))
           _global_method)
          (:choice
           (:field :arguments
            (:choice
             (:alias argument_list_with_parens argument_list)
             (:alias argument_list_no_parens argument_list)))
           :blank)
          (:field :block (:alias brace_block block))))
        (:prec "brace_block_call"
         (:seq
          (:field :method identifier)
          (:choice
           (:field :arguments
            (:choice
             (:alias argument_list_with_parens argument_list)
             (:alias argument_list_no_parens argument_list)))
           :blank)
          (:field :block (:alias brace_block block))))
        (:prec "do_end_block_call"
         (:seq
          (:choice
           _dot_call
           (:field :method (:alias identifier_method_call identifier))
           _global_method)
          (:choice
           (:field :arguments
            (:choice
             (:alias argument_list_with_parens argument_list)
             (:alias argument_list_no_parens argument_list)))
           :blank)
          (:field :block (:alias do_end_block block))))
        (:prec "do_end_block_call"
         (:seq
          (:field :method identifier)
          (:choice
           (:field :arguments
            (:choice
             (:alias argument_list_with_parens argument_list)
             (:alias argument_list_no_parens argument_list)))
           :blank)
          (:field :block (:alias do_end_block block))))
        (:prec "ampersand_block_call"
         (:seq
          (:choice
           _dot_call
           (:field :method (:alias identifier_method_call identifier))
           _global_method)
          (:field :arguments
           (:choice
            (:alias argument_list_with_parens_and_block argument_list)
            (:alias argument_list_no_parens_with_block argument_list)))))
        (:prec "ampersand_block_call"
         (:seq
          (:field :method identifier)
          (:field :arguments
           (:choice
            (:alias argument_list_with_parens_and_block argument_list)
            (:alias argument_list_no_parens_with_block argument_list))))))
  _global_method (:seq
                  "::"
                  (:field :method
                   (:choice
                    identifier
                    (:alias identifier_method_call identifier)
                    (:alias identifier_assign identifier))))
  implicit_object_method_identifier (:token
                                     (:seq
                                      "."
                                      (:repeat (:pattern "\\s"))
                                      (:pattern "[a-z_\\u{00a0}-\\u{10ffff}]" "u")
                                      (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))
                                      (:choice (:pattern "[?!]") :blank)))
  implicit_object_method_operator (:token
                                   (:seq
                                    "."
                                    (:repeat (:pattern "\\s"))
                                    (:choice
                                     "+"
                                     "-"
                                     "*"
                                     "/"
                                     "//"
                                     "%"
                                     "&"
                                     "|"
                                     "^"
                                     "**"
                                     ">>"
                                     "<<"
                                     "=="
                                     "!="
                                     "<"
                                     "<="
                                     ">"
                                     ">="
                                     "<=>"
                                     "==="
                                     "[]"
                                     "[]?"
                                     "[]="
                                     "!"
                                     "~"
                                     "!~"
                                     "=~"
                                     "&+"
                                     "&-"
                                     "&*"
                                     "&**")))
  implicit_object_ivar (:token
                        (:seq
                         "."
                         (:repeat (:pattern "\\s"))
                         "@"
                         (:pattern "[a-z_\\u{00a0}-\\u{10ffff}]" "u")
                         (:repeat (:pattern "[0-9A-Za-z_\\u{00a0}-\\u{10ffff}]" "u"))))
  implicit_object_index_operator (:seq
                                  "."
                                  (:field :method (:alias _start_of_index_operator operator))
                                  (:field :arguments (:alias bracket_argument_list argument_list))
                                  (:choice "]" "]?"))
  _implicit_object_call (:alias
                         (:choice implicit_object_call_chainable implicit_object_call_unchainable)
                         implicit_object_call)
  implicit_object_call_unchainable (:seq
                                    (:choice
                                     (:field :receiver
                                      (:alias implicit_object_call_chainable implicit_object_call))
                                     :blank)
                                    (:choice
                                     (:seq
                                      (:field :method
                                       (:choice
                                        (:alias implicit_object_method_identifier identifier)
                                        (:alias implicit_object_method_operator operator)))
                                      (:field :arguments
                                       (:alias argument_list_no_parens argument_list)))
                                     (:seq
                                      (:field :method
                                       (:choice
                                        (:alias implicit_object_method_identifier identifier)
                                        (:alias implicit_object_method_operator operator)))
                                      (:field :arguments
                                       (:alias argument_list_no_parens_with_block argument_list)))))
  implicit_object_call_chainable (:seq
                                  (:choice
                                   (:field :receiver
                                    (:alias implicit_object_call_chainable implicit_object_call))
                                   :blank)
                                  (:choice
                                   (:prec-right 0
                                    (:seq
                                     (:field :method
                                      (:choice
                                       (:alias implicit_object_method_identifier identifier)
                                       (:alias implicit_object_method_operator operator)))
                                     (:choice
                                      (:field :arguments
                                       (:alias argument_list_with_parens argument_list))
                                      :blank)))
                                   (:seq
                                    (:field :method
                                     (:choice
                                      (:alias implicit_object_method_identifier identifier)
                                      (:alias implicit_object_method_operator operator)))
                                    (:choice
                                     (:field :arguments
                                      (:choice
                                       (:alias argument_list_with_parens argument_list)
                                       (:alias argument_list_no_parens argument_list)))
                                     :blank)
                                    (:field :block (:alias brace_block block)))
                                   (:seq
                                    (:field :method
                                     (:choice
                                      (:alias implicit_object_method_identifier identifier)
                                      (:alias implicit_object_method_operator operator)))
                                    (:choice
                                     (:field :arguments
                                      (:choice
                                       (:alias argument_list_with_parens argument_list)
                                       (:alias argument_list_no_parens argument_list)))
                                     :blank)
                                    (:field :block (:alias do_end_block block)))
                                   (:seq
                                    (:field :method
                                     (:choice
                                      (:alias implicit_object_method_identifier identifier)
                                      (:alias implicit_object_method_operator operator)))
                                    (:field :arguments
                                     (:alias argument_list_with_parens_and_block argument_list)))
                                   (:alias implicit_object_ivar instance_var)
                                   (:alias implicit_object_index_operator index_call)))
  implicit_object_tuple (:seq
                         (:alias _start_of_hash_or_tuple "{")
                         (:choice (:seq _expression (:repeat (:seq "," _expression)) ",") :blank)
                         (:choice _implicit_object_call underscore)
                         (:repeat (:seq "," (:choice _expression _implicit_object_call underscore)))
                         (:choice "," :blank)
                         "}")
  assign_call (:prec "dot_operator"
               (:seq (:field :receiver _expression) "." (:field :method identifier)))
  index_operator (:prec "index_operator"
                  (:seq
                   (:field :receiver _expression)
                   (:field :method (:alias _start_of_index_operator operator))
                   (:choice (:field :arguments (:alias bracket_argument_list argument_list)) :blank)
                   (:choice "]" "]?")))
  index_call (:seq
              (:field :receiver _expression)
              "."
              (:field :method (:alias "[" operator))
              (:field :arguments (:alias bracket_argument_list argument_list))
              (:choice "]" "]?"))
  not (:prec "unary_operator" (:seq (:alias "!" operator) _expression))
  and (:prec-left "logical_and_operator" (:seq _expression (:alias "&&" operator) _expression))
  or (:prec-left "logical_or_operator" (:seq _expression (:alias "||" operator) _expression))
  additive_operator (:prec-left "additive_operator"
                     (:seq
                      (:field :receiver _expression)
                      (:field :method
                       (:alias
                        (:choice
                         binary_plus
                         binary_minus
                         binary_wrapping_plus
                         binary_wrapping_minus)
                        operator))
                      (:field :arguments (:alias _expression argument_list))))
  unary_additive_operator (:prec "unary_operator"
                           (:seq
                            (:field :method
                             (:alias
                              (:choice
                               unary_plus
                               unary_minus
                               unary_wrapping_plus
                               unary_wrapping_minus)
                              operator))
                            (:field :receiver _expression)))
  multiplicative_operator (:prec-left "multiplicative_operator"
                           (:seq
                            (:field :receiver _expression)
                            (:field :method
                             (:alias
                              (:choice
                               _binary_star
                               "&*"
                               _binary_slash
                               _binary_double_slash
                               _modulo_operator)
                              operator))
                            (:field :arguments (:alias _expression argument_list))))
  exponential_operator (:prec-right "exponential_operator"
                        (:seq
                         (:field :receiver _expression)
                         (:field :method (:alias (:choice _binary_double_star "&**") operator))
                         (:field :arguments (:alias _expression argument_list))))
  shift_operator (:prec-left "shift_operator"
                  (:seq
                   (:field :receiver _expression)
                   (:field :method (:alias (:choice "<<" ">>") operator))
                   (:field :arguments (:alias _expression argument_list))))
  complement_operator (:prec "unary_operator"
                       (:seq (:field :method (:alias "~" operator)) (:field :receiver _expression)))
  binary_and_operator (:prec-left "binary_and_operator"
                       (:seq
                        (:field :receiver _expression)
                        (:field :method (:alias binary_ampersand operator))
                        (:field :arguments (:alias _expression argument_list))))
  binary_or_operator (:prec-left "binary_or_operator"
                      (:seq
                       (:field :receiver _expression)
                       (:field :method (:alias (:choice "|" "^") operator))
                       (:field :arguments (:alias _expression argument_list))))
  equality_operator (:prec-left "equality_operator"
                     (:seq
                      (:field :receiver _expression)
                      (:field :method (:alias (:choice "==" "!=" "=~" "!~" "===") operator))
                      (:field :arguments (:alias _expression argument_list))))
  comparison_operator (:prec-left "comparison_operator"
                       (:seq
                        (:field :receiver _expression)
                        (:field :method (:alias (:choice "<" "<=" ">" ">=" "<=>") operator))
                        (:field :arguments (:alias _expression argument_list))))
  pseudo_call (:choice
               (:seq
                (:choice (:seq (:field :receiver _expression) ".") :blank)
                (:field :method (:alias (:choice "as" "as?" "is_a?") identifier))
                (:field :arguments (:alias pseudo_call_argument_list argument_list)))
               (:seq
                (:choice (:seq (:field :receiver _expression) ".") :blank)
                (:field :method (:alias "nil?" identifier))
                (:choice (:seq "(" ")") :blank))
               (:seq
                (:choice (:seq (:field :receiver _expression) ".") :blank)
                (:field :method (:alias "responds_to?" identifier))
                (:field :arguments (:alias pseudo_responds_to_argument_list argument_list))))
  pseudo_call_argument_list (:choice (:seq "(" _bare_type ")") _bare_type)
  pseudo_responds_to_argument_list (:choice
                                    (:seq
                                     "("
                                     (:alias
                                      (:choice quoted_symbol unquoted_symbol operator_symbol)
                                      symbol)
                                     ")")
                                    (:alias
                                     (:choice quoted_symbol unquoted_symbol operator_symbol)
                                     symbol))
  splat (:prec "splat_operator" (:seq (:alias _unary_star "*") _expression))
  double_splat (:prec "splat_operator" (:seq (:alias _unary_double_star "**") _expression))
  named_expr (:seq
              (:field :name
               (:choice
                identifier
                (:alias _constant_segment identifier)
                (:alias identifier_method_call identifier)
                string
                (:alias string_percent_literal string)))
              (:token-immediate ":")
              (:choice _expression out))
  argument_list_no_parens (:prec-right 0
                           (:seq
                            (:choice _start_of_parenless_args :blank)
                            (:choice _expression splat double_splat named_expr out)
                            (:repeat
                             (:prec "comma"
                              (:seq "," (:choice _expression splat double_splat named_expr out))))))
  argument_list_no_parens_with_block (:prec-right 0
                                      (:seq
                                       (:choice _start_of_parenless_args :blank)
                                       (:choice
                                        (:seq
                                         (:choice _expression splat double_splat named_expr out)
                                         (:repeat
                                          (:prec "comma"
                                           (:seq
                                            ","
                                            (:choice _expression splat double_splat named_expr out))))
                                         ",")
                                        :blank)
                                       block_argument))
  argument_list_with_parens (:prec-right 0
                             (:seq
                              (:token-immediate "(")
                              (:choice
                               (:seq
                                (:choice _expression splat double_splat named_expr out)
                                (:repeat
                                 (:seq "," (:choice _expression splat double_splat named_expr out)))
                                (:choice "," :blank))
                               :blank)
                              ")"))
  argument_list_with_parens_and_block (:prec-right 0
                                       (:seq
                                        (:token-immediate "(")
                                        (:choice
                                         (:seq
                                          (:choice _expression splat double_splat named_expr out)
                                          (:repeat
                                           (:seq
                                            ","
                                            (:choice _expression splat double_splat named_expr out)))
                                          ",")
                                         :blank)
                                        block_argument
                                        ")"))
  out (:seq "out" (:choice identifier instance_var underscore macro_var))
  assign (:prec "assignment_operator"
          (:seq
           (:field :lhs
            (:choice
             underscore
             identifier
             instance_var
             class_var
             macro_var
             assign_call
             index_call
             (:alias index_operator index_call)
             special_variable))
           "="
           (:field :rhs _expression)))
  const_assign (:prec-right "assignment_operator"
                (:seq (:field :lhs constant) "=" (:field :rhs _statement)))
  class_var_assign (:prec-right "assignment_operator"
                    (:seq (:field :lhs class_var) "=" (:field :rhs _statement)))
  operator_assign (:prec "assignment_operator"
                   (:seq
                    (:field :lhs
                     (:choice
                      identifier
                      instance_var
                      class_var
                      macro_var
                      assign_call
                      index_call
                      (:alias index_operator index_call)))
                    (:alias
                     (:choice
                      "+="
                      "&+="
                      "-="
                      "&-="
                      "*="
                      "&*="
                      "/="
                      "//="
                      "%="
                      "|="
                      "&="
                      "^="
                      "**="
                      "<<="
                      ">>="
                      "||="
                      "&&=")
                     operator)
                    (:field :rhs _expression)))
  lhs_splat (:seq
             "*"
             (:choice
              underscore
              identifier
              instance_var
              class_var
              macro_var
              assign_call
              index_call
              (:alias index_operator index_call)))
  multi_assign (:choice
                (:seq (:field :lhs (:alias lhs_splat splat)) "=" (:field :rhs _expression))
                (:seq
                 (:seq
                  (:repeat1
                   (:seq
                    (:field :lhs
                     (:choice
                      (:choice
                       underscore
                       identifier
                       instance_var
                       class_var
                       macro_var
                       assign_call
                       index_call
                       (:alias index_operator index_call))
                      (:alias lhs_splat splat)))
                    ","))
                  (:field :lhs
                   (:choice
                    (:choice
                     underscore
                     identifier
                     instance_var
                     class_var
                     macro_var
                     assign_call
                     index_call
                     (:alias index_operator index_call))
                    (:alias lhs_splat splat))))
                 "="
                 (:field :rhs _expression))
                (:seq
                 (:field :lhs (:alias lhs_splat splat))
                 "="
                 (:seq (:repeat1 (:seq (:field :rhs _expression) ",")) (:field :rhs _expression)))
                (:seq
                 (:seq
                  (:repeat1
                   (:seq
                    (:field :lhs
                     (:choice
                      (:choice
                       underscore
                       identifier
                       instance_var
                       class_var
                       macro_var
                       assign_call
                       index_call
                       (:alias index_operator index_call))
                      (:alias lhs_splat splat)))
                    ","))
                  (:field :lhs
                   (:choice
                    (:choice
                     underscore
                     identifier
                     instance_var
                     class_var
                     macro_var
                     assign_call
                     index_call
                     (:alias index_operator index_call))
                    (:alias lhs_splat splat))))
                 "="
                 (:seq (:repeat1 (:seq (:field :rhs _expression) ",")) (:field :rhs _expression))))
  uninitialized_assign (:seq
                        (:field :lhs
                         (:choice identifier instance_var class_var global_var macro_var))
                        "="
                        (:field :rhs uninitialized_var))
  uninitialized_var (:seq "uninitialized" _bare_type)
  type_declaration (:prec "assignment_operator"
                    (:seq
                     (:field :var
                      (:choice
                       identifier
                       (:alias identifier_method_call identifier)
                       instance_var
                       class_var
                       macro_var))
                     ":"
                     (:token-immediate (:pattern "\\s"))
                     (:field :type _bare_type)
                     (:choice (:seq "=" (:field :value _expression)) :blank)))
  alias (:seq "alias" (:field :name constant) "=" (:field :type _bare_type))
  block_body_param (:field :name (:choice identifier underscore))
  block_body_splat_param (:seq "*" (:field :name (:choice identifier underscore)))
  _block_body_nested_param (:seq
                            "("
                            (:choice
                             (:alias block_body_param param)
                             (:alias block_body_splat_param splat_param)
                             _block_body_nested_param)
                            (:repeat
                             (:seq
                              ","
                              (:choice
                               (:alias block_body_param param)
                               (:alias block_body_splat_param splat_param)
                               _block_body_nested_param)))
                            (:choice "," :blank)
                            ")")
  block_param_list (:seq
                    (:choice
                     (:alias block_body_param param)
                     (:alias block_body_splat_param splat_param)
                     _block_body_nested_param)
                    (:repeat
                     (:seq
                      ","
                      (:choice
                       (:alias block_body_param param)
                       (:alias block_body_splat_param splat_param)
                       _block_body_nested_param)))
                    (:choice "," :blank))
  do_end_block (:seq
                "do"
                (:choice
                 (:seq "|" (:field :params (:alias block_param_list param_list)) "|")
                 :blank)
                (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
                (:choice _rescue_else_ensure :blank)
                "end")
  brace_block (:seq
               (:alias _start_of_brace_block "{")
               (:choice (:seq "|" (:field :params (:alias block_param_list param_list)) "|") :blank)
               (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
               "}")
  block_argument (:prec "block_ampersand"
                  (:seq (:alias _block_ampersand "&") (:choice _expression _implicit_object_call)))
  begin (:seq
         "begin"
         (:choice _terminator :blank)
         (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
         (:choice _rescue_else_ensure :blank)
         "end")
  rescue (:seq
          (:alias _regular_rescue_keyword "rescue")
          (:choice
           (:choice
            (:seq
             (:field :variable identifier)
             (:alias (:pattern ":\\s") ":")
             (:field :type _bare_type))
            (:field :variable identifier)
            (:field :type _bare_type))
           :blank)
          _terminator
          (:field :body (:seq (:choice (:alias _statements expressions) :blank))))
  ensure (:seq
          (:alias _regular_ensure_keyword "ensure")
          (:field :body (:choice (:alias _statements expressions) :blank)))
  modifier_rescue (:seq
                   _statement
                   (:alias _modifier_rescue_keyword "rescue")
                   (:field :rescue _expression))
  modifier_ensure (:seq
                   _statement
                   (:alias _modifier_ensure_keyword "ensure")
                   (:field :ensure _expression))
  _rescue_else_ensure (:choice
                       (:seq
                        (:field :rescue (:repeat1 rescue))
                        (:field :else (:choice else :blank))
                        (:field :ensure (:choice ensure :blank)))
                       (:seq
                        (:field :rescue (:repeat rescue))
                        (:field :else else)
                        (:field :ensure (:choice ensure :blank)))
                       (:seq
                        (:field :rescue (:repeat rescue))
                        (:field :else (:choice else :blank))
                        (:field :ensure ensure)))
  while (:seq
         "while"
         (:field :cond _expression)
         _terminator
         (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
         "end")
  until (:seq
         "until"
         (:field :cond _expression)
         _terminator
         (:field :body (:seq (:choice (:alias _statements expressions) :blank)))
         "end")
  if (:seq
      (:alias _regular_if_keyword "if")
      (:field :cond _expression)
      _terminator
      (:choice (:field :then then) :blank)
      (:choice (:field :else (:choice elsif else)) :blank)
      "end")
  unless (:seq
          (:alias _regular_unless_keyword "unless")
          (:field :cond _expression)
          _terminator
          (:choice (:field :then then) :blank)
          (:choice (:field :else else) :blank)
          "end")
  then (:seq _statements)
  elsif (:seq
         "elsif"
         (:field :cond _expression)
         _terminator
         (:choice (:field :then then) :blank)
         (:choice (:field :else (:choice elsif else)) :blank))
  else (:seq "else" (:field :body (:seq (:choice (:alias _statements expressions) :blank))))
  conditional (:prec-right "ternary_operator"
               (:seq
                (:field :cond _expression)
                "?"
                (:field :then _expression)
                ":"
                (:field :else _expression)))
  modifier_if (:seq
               (:field :then _statement)
               (:alias _modifier_if_keyword "if")
               (:field :cond _expression))
  modifier_unless (:seq
                   (:field :then _statement)
                   (:alias _modifier_unless_keyword "unless")
                   (:field :cond _expression))
  require (:seq "require" string)
  when (:seq
        "when"
        (:field :cond
         (:choice _expression _implicit_object_call (:alias implicit_object_tuple tuple)))
        (:repeat
         (:seq
          ","
          (:field :cond
           (:choice _expression _implicit_object_call (:alias implicit_object_tuple tuple)))))
        (:choice "then" _terminator)
        (:field :body (:seq (:choice (:alias _statements expressions) :blank))))
  case (:seq
        "case"
        (:choice (:field :cond _expression) :blank)
        (:repeat when)
        (:choice else :blank)
        "end")
  select (:seq "select" (:repeat when) (:choice else :blank) "end")
  in (:seq
      "in"
      (:field :cond
       (:choice _expression _implicit_object_call (:alias implicit_object_tuple tuple)))
      (:repeat
       (:seq
        ","
        (:field :cond
         (:choice _expression _implicit_object_call (:alias implicit_object_tuple tuple)))))
      (:choice "then" _terminator)
      (:field :body (:seq (:choice (:alias _statements expressions) :blank))))
  exhaustive_case (:seq "case" (:field :cond _expression) (:repeat1 in) "end")
  asm (:seq "asm" "(" (:field :text string) (:choice _asm_outputs :blank) ")")
  _asm_outputs (:seq
                ":"
                (:choice (:field :outputs asm_operands) :blank)
                (:choice _asm_inputs :blank))
  _asm_inputs (:seq
               ":"
               (:choice (:field :inputs asm_operands) :blank)
               (:choice _asm_clobbers :blank))
  _asm_clobbers (:seq
                 ":"
                 (:choice (:field :clobbers asm_clobbers) :blank)
                 (:choice _asm_options :blank))
  _asm_options (:seq ":" (:choice (:field :options asm_options) :blank))
  asm_operands (:seq asm_operand (:repeat (:seq "," asm_operand)))
  asm_operand (:seq (:field :constraint string) "(" (:field :expression _expression) ")")
  asm_clobbers (:seq string (:repeat (:seq "," string)))
  asm_options (:seq string (:repeat (:seq "," string)))
  loc_pragma_push (:token (:prec 1 "#<loc:push>"))
  loc_pragma_pop (:token (:prec 1 "#<loc:pop>"))
  loc_pragma_location (:token
                       (:prec 1
                        (:seq
                         "#<loc:\""
                         (:choice (:pattern "[^\"]+") :blank)
                         "\","
                         (:choice (:pattern "[0-9]+") :blank)
                         (:seq "," (:choice (:pattern "[0-9]+") :blank))
                         ">")))}}
