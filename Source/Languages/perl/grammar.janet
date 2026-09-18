# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "perl"
 :word _identifier
 :extras [(:pattern "\\p{White_Space}") comment pod heredoc_content]
 :conflicts [[preinc_expression postinc_expression]
             [return_expression]
             [function bareword]
             [function function_call_expression]
             [_variables indirect_object]
             [_term indirect_object]
             [expression_statement _tricky_indirob_hashref]
             [autoquoted_bareword]
             [optional_parameter]
             [_loop_body]
             [_interp_arrow _interpolation_fallbacks]]
 :precedences []
 :externals [_single_quote
             _double_quote
             _backtick_quote
             _search_slash_quote
             _no_search_slash_plz
             _open_readline_bracket
             _open_fileglob_bracket
             _PERLY_SEMICOLON
             _PERLY_HEREDOC
             _ctrl_z_hack
             _quotelike_begin_quote
             _quotelike_middle_close_quote
             _quotelike_middle_skip
             _quotelike_end_zw
             _quotelike_end_quote
             _q_string_content
             _qq_string_content
             escape_sequence
             escaped_delimiter
             _dollar_in_regexp
             _regexp_open_bracket
             _regexp_open_brace
             pod
             _gobbled_content
             _attribute_value_begin
             attribute_value
             prototype
             _signature_start
             _heredoc_delimiter
             _command_heredoc_delimiter
             _heredoc_start
             _heredoc_middle
             heredoc_end
             _fat_comma_autoquoted
             _fat_comma_autoquoted_ahead
             _filetest
             _brace_autoquoted_token
             _brace_end_zw
             _dollar_ident_zw
             _no_interp_whitespace_zw
             _NONASSOC
             _RECOVER_PAREN_CLOSE
             _RECOVER_BRACKET_CLOSE
             _RECOVER_BRACE_CLOSE
             _RECOVER_ARROW
             _RECOVER_BLOCK_CLOSE
             format_content
             _x_op
             _KW_CLASS
             _KW_ROLE
             _KW_METHOD
             _KW_ASYNC
             _KW_TRY
             _ERROR]
 :inline [_var_indirob
          _semicolon
          _fullstmt
          _else
          _conditionals
          _quotelike_end
          _quotelike_begin
          _declared_vars
          _interpolations
          _nonvar_interpolation_fallbacks
          _apostrophe
          _brace_autoquoted
          _version
          _loops
          _func0op
          _func1op
          _map_grep
          _PERLY_COMMA
          _KW_USE
          _KW_FOR
          _LOOPEX
          _PHASE_NAME
          _HASH_PERCENT
          _bareword
          _unambiguous_function]
 :supertypes [primitive postfix_deref subscripted slices]
 :rules
 {source_file (:seq (:repeat _fullstmt) (:choice __DATA__ :blank))
  _PERLY_BRACE_OPEN "{"
  block (:seq _PERLY_BRACE_OPEN (:repeat _fullstmt) "}")
  _body_block (:seq
               _PERLY_BRACE_OPEN
               (:repeat _fullstmt)
               (:choice "}" (:alias _RECOVER_BLOCK_CLOSE "}")))
  _fullstmt (:choice _barestmt statement_label)
  statement_label (:seq
                   (:field :label (:choice identifier (:alias _PHASE_NAME identifier)))
                   ":"
                   (:field :statement _fullstmt))
  _semicolon (:choice ";" _PERLY_SEMICOLON)
  _barestmt (:choice
             package_statement
             class_statement
             role_statement
             class_phaser_statement
             use_version_statement
             use_statement
             subroutine_declaration_statement
             method_declaration_statement
             phaser_statement
             format_statement
             conditional_statement
             loop_statement
             cstyle_for_statement
             for_statement
             try_statement
             (:alias block block_statement)
             (:seq expression_statement (:choice _semicolon __DATA__))
             defer_statement
             ";")
  package_statement (:choice
                     (:seq
                      "package"
                      (:field :name package)
                      (:choice (:field :version _version) :blank)
                      _semicolon)
                     (:seq
                      "package"
                      (:field :name package)
                      (:choice (:field :version _version) :blank)
                      block))
  class_statement (:choice
                   (:seq
                    (:alias _KW_CLASS "class")
                    (:field :name package)
                    (:choice (:field :version _version) :blank)
                    (:choice (:seq ":" (:choice (:field :attributes attrlist) :blank)) :blank)
                    _semicolon)
                   (:seq
                    (:alias _KW_CLASS "class")
                    (:field :name package)
                    (:choice (:field :version _version) :blank)
                    (:choice (:seq ":" (:choice (:field :attributes attrlist) :blank)) :blank)
                    block))
  role_statement (:choice
                  (:seq
                   (:alias _KW_ROLE "role")
                   (:field :name package)
                   (:choice (:field :version _version) :blank)
                   (:choice (:seq ":" (:choice (:field :attributes attrlist) :blank)) :blank)
                   _semicolon)
                  (:seq
                   (:alias _KW_ROLE "role")
                   (:field :name package)
                   (:choice (:field :version _version) :blank)
                   (:choice (:seq ":" (:choice (:field :attributes attrlist) :blank)) :blank)
                   block))
  format_statement (:seq "format" (:choice (:field :name bareword) :blank) "=" format_content)
  class_phaser_statement (:seq
                          (:field :phase (:choice "BUILD" "ADJUST"))
                          (:choice (:seq ":" (:choice (:field :attributes attrlist) :blank)) :blank)
                          (:choice signature :blank)
                          block)
  use_version_statement (:seq _KW_USE (:field :version _version) _semicolon)
  use_statement (:seq
                 _KW_USE
                 (:field :module package)
                 (:choice (:field :version _version) :blank)
                 (:choice _listexpr :blank)
                 _semicolon)
  mandatory_parameter (:alias (:choice "$" _signature_scalar) scalar)
  optional_parameter (:choice
                      (:seq
                       (:alias _signature_scalar scalar)
                       (:choice "=" "||=" "//=")
                       (:field :default _term))
                      (:seq
                       (:alias "$" scalar)
                       (:choice "=" "||=" "//=")
                       (:field :default (:choice _term :blank))))
  named_parameter (:seq
                   ":"
                   (:alias _signature_scalar scalar)
                   (:choice (:seq (:choice "=" "||=" "//=") (:field :default _term)) :blank))
  slurpy_parameter (:choice
                    (:alias (:choice "@" _signature_array) array)
                    (:alias (:choice _HASH_PERCENT _signature_hash) hash))
  _signature_vars (:choice mandatory_parameter optional_parameter slurpy_parameter named_parameter)
  signature (:seq
             (:alias _signature_start "(")
             (:repeat
              (:seq _signature_vars (:choice (:seq "," (:choice _signature_vars :blank)) :blank)))
             ")")
  subroutine_declaration_statement (:seq
                                    (:choice
                                     (:choice (:field :lexical (:choice "my" "state")) "our")
                                     :blank)
                                    (:repeat (:choice "extended" (:alias _KW_ASYNC "async")))
                                    "sub"
                                    (:field :name bareword)
                                    _sub_decl_tail)
  method_declaration_statement (:seq
                                (:choice
                                 (:choice (:field :lexical (:choice "my" "state")) "our")
                                 :blank)
                                (:repeat (:choice "extended" (:alias _KW_ASYNC "async")))
                                (:alias _KW_METHOD "method")
                                (:field :name bareword)
                                _sub_decl_tail)
  _sub_decl_tail (:seq
                  (:choice (:seq ":" (:choice (:field :attributes attrlist) :blank)) :blank)
                  (:choice (:choice prototype signature) :blank)
                  (:choice (:field :body (:alias _body_block block)) _semicolon))
  phaser_statement (:seq (:field :phase _PHASE_NAME) (:alias _body_block block))
  conditional_statement (:seq
                         _conditionals
                         "("
                         (:field :condition _expr)
                         ")"
                         (:field :block (:alias _body_block block))
                         (:choice _else :blank))
  _loop_body (:seq
              (:field :block (:alias _body_block block))
              (:choice (:seq "continue" (:field :continue (:alias _body_block block))) :blank))
  loop_statement (:seq _loops "(" (:field :condition _expr) ")" _loop_body)
  cstyle_for_statement (:seq
                        _KW_FOR
                        "("
                        (:field :initialiser _barestmt)
                        (:field :condition _barestmt)
                        (:field :iterator (:choice _expr :blank))
                        ")"
                        _loop_body)
  _for_initializer (:choice
                    (:seq (:choice (:choice "my" "state" "our") :blank) (:field :variable scalar))
                    (:seq
                     (:choice (:choice "my" "state" "our") :blank)
                     (:field :variable refalias_variable))
                    (:seq
                     "my"
                     (:seq
                      "("
                      (:repeat (:seq (:choice (:field :variables scalar) :blank) ","))
                      (:choice (:field :variables scalar) :blank)
                      ")")))
  for_statement (:seq
                 _KW_FOR
                 (:choice _for_initializer :blank)
                 "("
                 (:field :list _expr)
                 ")"
                 _loop_body)
  try_statement (:seq
                 (:alias _KW_TRY "try")
                 (:field :try_block (:alias _body_block block))
                 (:choice
                  (:seq
                   "catch"
                   (:choice (:seq "(" (:field :catch_expr _expr) ")") :blank)
                   (:field :catch_block (:alias _body_block block)))
                  :blank)
                 (:choice
                  (:seq "finally" (:field :finally_block (:alias _body_block block)))
                  :blank))
  defer_statement (:seq "defer" (:field :block (:alias _body_block block)))
  expression_statement (:choice _expr _postfix_expressions yadayada)
  _postfix_expressions (:choice
                        postfix_conditional_expression
                        postfix_loop_expression
                        postfix_for_expression)
  postfix_conditional_expression (:seq _expr _conditionals (:field :condition _expr))
  postfix_loop_expression (:seq _expr _loops (:field :condition _expr))
  postfix_for_expression (:seq _expr _KW_FOR (:field :list _expr))
  yadayada "..."
  _else (:choice else elsif)
  else (:seq "else" (:field :block (:alias _body_block block)))
  elsif (:seq
         "elsif"
         "("
         (:field :condition _expr)
         ")"
         (:field :block (:alias _body_block block))
         (:choice _else :blank))
  _expr (:choice lowprec_logical_expression _listexpr)
  lowprec_logical_expression (:choice
                              (:prec-left 3
                               (:seq
                                (:field :left _expr)
                                (:field :operator "and")
                                (:field :right _expr)))
                              (:prec-left 2
                               (:seq
                                (:field :left _expr)
                                (:field :operator (:choice "or" "xor"))
                                (:field :right _expr))))
  _listexpr (:choice (:alias _term_rightward list_expression) (:prec-right 0 _term))
  _term_rightward (:prec-right 0
                   (:seq
                    _term
                    _PERLY_COMMA
                    (:repeat (:seq (:choice _term :blank) _PERLY_COMMA))
                    (:choice _term :blank)))
  subscripted (:choice
               glob_slot_expression
               array_element_expression
               hash_element_expression
               coderef_call_expression
               anonymous_slice_expression)
  container_variable (:prec 2 (:seq "$" _var_indirob))
  _glob_slot_subscript (:seq "{" _hash_key "}")
  glob_slot_expression (:choice
                        (:seq glob _glob_slot_subscript)
                        (:prec-left 25 (:seq _term "->" "*" _glob_slot_subscript)))
  _index_subscript (:seq
                    "["
                    (:field :index _expr)
                    (:choice "]" (:alias _RECOVER_BRACKET_CLOSE "]")))
  _key_subscript (:seq "{" (:field :key _hash_key) (:choice "}" (:alias _RECOVER_BRACE_CLOSE "}")))
  _args_subscript (:seq
                   "("
                   (:choice (:field :arguments _expr) :blank)
                   (:choice ")" (:alias _RECOVER_PAREN_CLOSE ")")))
  array_element_expression (:choice
                            (:seq (:field :array container_variable) _index_subscript)
                            (:prec-left 25 (:seq _term "->" _index_subscript))
                            (:seq subscripted _index_subscript))
  _hash_key (:choice _brace_autoquoted _expr)
  hash_element_expression (:choice
                           (:seq (:field :hash container_variable) _key_subscript)
                           (:prec-left 25 (:seq _term "->" _key_subscript))
                           (:seq subscripted _key_subscript))
  coderef_call_expression (:choice
                           (:prec-left 25 (:seq _term "->" _args_subscript))
                           (:seq subscripted _args_subscript))
  _anon_slice_subscript (:seq "[" _expr "]")
  anonymous_slice_expression (:choice
                              (:seq
                               "("
                               (:choice (:field :list _expr) :blank)
                               ")"
                               _anon_slice_subscript)
                              (:seq (:field :list quoted_word_list) _anon_slice_subscript))
  slices (:choice slice_expression keyval_expression)
  _slice_index_subscript (:seq "[" _expr (:choice "]" (:alias _RECOVER_BRACKET_CLOSE "]")))
  _slice_key_subscript (:seq "{" _hash_key (:choice "}" (:alias _RECOVER_BRACE_CLOSE "}")))
  slice_container_variable (:seq "@" _var_indirob)
  slice_expression (:choice
                    (:seq (:field :array slice_container_variable) _slice_index_subscript)
                    (:seq (:field :hash slice_container_variable) _slice_key_subscript)
                    (:prec-left 25 (:seq (:field :arrayref _term) "->" "@" _slice_index_subscript))
                    (:prec-left 25 (:seq (:field :hashref _term) "->" "@" _slice_key_subscript)))
  keyval_container_variable (:seq _HASH_PERCENT _var_indirob)
  keyval_expression (:choice
                     (:seq (:field :array keyval_container_variable) _slice_index_subscript)
                     (:seq (:field :hash keyval_container_variable) _slice_key_subscript)
                     (:prec-left 25 (:seq (:field :arrayref _term) "->" "%" _slice_index_subscript))
                     (:prec-left 25 (:seq (:field :hashref _term) "->" "%" _slice_key_subscript)))
  parenthesized_expression (:seq "(" _expr ")")
  _term (:choice
         readline_expression
         fileglob_expression
         assignment_expression
         binary_expression
         equality_expression
         relational_expression
         unary_expression
         preinc_expression
         postinc_expression
         anonymous_array_expression
         anonymous_hash_expression
         anonymous_subroutine_expression
         anonymous_method_expression
         async_block_expression
         do_expression
         eval_expression
         await_expression
         conditional_expression
         refgen_expression
         localization_expression
         parenthesized_expression
         quoted_word_list
         heredoc_token
         command_heredoc_token
         stub_expression
         _variables
         subscripted
         slices
         postfix_deref
         loopex_expression
         goto_expression
         return_expression
         undef_expression
         logical_not_expression
         require_expression
         require_version_expression
         func0op_call_expression
         func1op_call_expression
         map_grep_expression
         sort_expression
         (:alias _builtin_filehandle filehandle)
         bareword
         (:alias (:choice _listop_keyword _indirob_listop) function)
         autoquoted_bareword
         _listop
         variable_declaration
         primitive
         _literal)
  readline_expression (:choice
                       (:seq
                        (:field :operator (:alias _open_readline_bracket "<"))
                        (:choice (:alias (:choice scalar bareword) filehandle) :blank)
                        (:field :operator ">"))
                       (:field :operator (:seq "<<" (:token-immediate ">>"))))
  fileglob_expression (:seq
                       (:field :operator (:alias _open_fileglob_bracket "<"))
                       (:field :content (:alias _interpolated_string_content string_content))
                       (:field :operator (:alias _quotelike_end ">")))
  assignment_expression (:prec-right 7
                         (:seq
                          (:field :left _term)
                          (:field :operator
                           (:choice
                            "="
                            (:token (:prec 2 "**="))
                            "+="
                            "-="
                            ".="
                            (:token (:prec 2 "*="))
                            "/="
                            (:token (:prec 2 "%="))
                            "x="
                            (:token (:prec 2 "&="))
                            "|="
                            "^="
                            "<<="
                            ">>="
                            (:token (:prec 2 "&&="))
                            "||="
                            "//="))
                          (:field :right _term)))
  binary_expression (:choice
                     (:prec-right 9
                      (:seq
                       (:field :left _term)
                       (:field :operator (:choice ".." "..."))
                       (:field :right _term)
                       (:choice
                        (:seq (:field :operator (:choice ".." "...")) _NONASSOC _ERROR)
                        :blank)))
                     (:prec-right 23
                      (:seq
                       (:field :left _term)
                       (:field :operator (:token (:prec 2 "**")))
                       (:field :right _term)))
                     (:prec-left 10
                      (:seq
                       (:field :left _term)
                       (:field :operator (:choice "||" "//" "^^"))
                       (:field :right _term)))
                     (:prec-left 11
                      (:seq
                       (:field :left _term)
                       (:field :operator (:token (:prec 2 "&&")))
                       (:field :right _term)))
                     (:prec-left 12
                      (:seq
                       (:field :left _term)
                       (:field :operator (:choice "|" "^"))
                       (:field :right _term)))
                     (:prec-left 13
                      (:seq (:field :left _term) (:field :operator "&") (:field :right _term)))
                     (:prec-left 18
                      (:seq
                       (:field :left _term)
                       (:field :operator (:choice "<<" ">>"))
                       (:field :right _term)))
                     (:prec-left 19
                      (:seq
                       (:field :left _term)
                       (:field :operator (:choice "+" "-" "."))
                       (:field :right _term)))
                     (:prec-left 20
                      (:seq
                       (:field :left _term)
                       (:field :operator (:choice "*" "/" "%" "x" (:alias _x_op "x")))
                       (:field :right _term)))
                     (:prec-left 21
                      (:seq
                       (:field :left _term)
                       (:field :operator (:choice "=~" "!~"))
                       (:field :right _term))))
  equality_expression (:choice
                       (:prec-left 14
                        (:seq
                         (:field :left _term)
                         (:field :operator (:choice "==" "!=" "eq" "===" "equ" "eqr" "ne"))
                         (:field :right _term)))
                       (:prec-right 14
                        (:seq
                         (:field :left _term)
                         (:field :operator (:choice "<=>" "cmp" "~~"))
                         (:field :right _term)
                         (:choice
                          (:seq (:field :operator (:choice "<=>" "cmp" "~~")) _NONASSOC _ERROR)
                          :blank))))
  relational_expression (:choice
                         (:prec-left 15
                          (:seq
                           (:field :left _term)
                           (:field :operator (:choice "<" "<=" ">=" ">" "lt" "le" "ge" "gt"))
                           (:field :right _term)))
                         (:prec-right 15
                          (:seq
                           (:field :left _term)
                           (:field :operator "isa")
                           (:field :right _term)
                           (:choice (:seq (:field :operator "isa") _NONASSOC _ERROR) :blank))))
  unary_expression (:choice
                    (:prec 22 (:seq (:field :operator "-") (:field :operand _term)))
                    (:prec 22 (:seq (:field :operator "+") (:field :operand _term)))
                    (:prec 22 (:seq (:field :operator "~") (:field :operand _term)))
                    (:prec 22 (:seq (:field :operator "!") (:field :operand _term))))
  logical_not_expression (:prec-right 4 (:seq (:field :operator "not") (:field :operand _listexpr)))
  preinc_expression (:prec 24 (:seq (:field :operator (:choice "++" "--")) (:field :operand _term)))
  postinc_expression (:prec 24
                      (:seq (:field :operand _term) (:field :operator (:choice "++" "--"))))
  conditional_expression (:prec-right 8
                          (:seq
                           (:field :condition _term)
                           "?"
                           (:field :consequent _term)
                           ":"
                           (:field :alternative _term)))
  refgen_expression (:prec-left 22 (:seq "\\" (:choice (:alias amper_sub function) _term)))
  anonymous_array_expression (:seq
                              "["
                              (:choice _expr :blank)
                              (:choice "]" (:alias _RECOVER_BRACKET_CLOSE "]")))
  _tricky_list (:prec-right 1
                (:seq
                 (:choice
                  string_literal
                  interpolated_string_literal
                  command_string
                  autoquoted_bareword
                  number)
                 _PERLY_COMMA
                 (:repeat (:seq (:choice _term :blank) _PERLY_COMMA))
                 (:choice _term :blank)))
  anonymous_hash_expression (:choice
                             (:seq _PERLY_BRACE_OPEN _expr "}")
                             (:prec 1 (:seq _PERLY_BRACE_OPEN "}"))
                             (:seq _PERLY_BRACE_OPEN (:alias _tricky_list list_expression) "}"))
  _anon_sub_tail (:seq
                  (:choice (:seq ":" (:choice (:field :attributes attrlist) :blank)) :blank)
                  (:choice (:choice prototype signature) :blank)
                  (:field :body block))
  anonymous_subroutine_expression (:seq
                                   (:repeat (:choice "extended" (:alias _KW_ASYNC "async")))
                                   "sub"
                                   _anon_sub_tail)
  anonymous_method_expression (:seq
                               (:repeat (:choice "extended" (:alias _KW_ASYNC "async")))
                               (:alias _KW_METHOD "method")
                               _anon_sub_tail)
  async_block_expression (:seq (:alias _KW_ASYNC "async") block)
  do_expression (:choice (:seq "do" block))
  eval_expression (:prec-left 16
                   (:choice
                    (:seq "eval" (:choice (:choice block _term) :blank))
                    (:seq "do" (:alias _term filename))))
  await_expression (:seq "await" _term)
  _declared_vars (:choice
                  (:alias _declare_scalar scalar)
                  (:alias _declare_array array)
                  (:alias _declare_hash hash))
  refalias_variable (:seq "\\" _declared_vars)
  variable_declaration (:prec-left 9
                        (:seq
                         (:choice "my" "state" "our" "field")
                         (:choice
                          (:seq
                           (:choice (:field :type package) :blank)
                           (:field :variable _declared_vars))
                          (:field :variable refalias_variable)
                          _decl_variable_list)
                         (:choice (:seq ":" (:choice (:field :attributes attrlist) :blank)) :blank)))
  _decl_variable_list (:seq "(" (:choice _decl_variable_list_body :blank) ")")
  _decl_variable_list_body (:seq
                            (:field :variables _decl_variable_list_element)
                            (:repeat
                             (:seq
                              ","
                              (:choice (:field :variables _decl_variable_list_element) :blank))))
  _decl_variable_list_element (:choice
                               undef_expression
                               _declared_vars
                               refalias_variable
                               variable_group)
  variable_group (:seq "(" (:choice _decl_variable_list_body :blank) ")")
  localization_expression (:prec 16 (:seq (:choice "local" "dynamically") _term))
  stub_expression (:prec -1 (:seq "(" ")"))
  postfix_deref (:choice
                 scalar_deref_expression
                 array_deref_expression
                 arraylen_deref_expression
                 hash_deref_expression
                 amper_deref_expression
                 glob_deref_expression)
  scalar_deref_expression (:prec-left 25 (:seq _term "->" "$" "*"))
  array_deref_expression (:prec-left 25 (:seq _term "->" "@" "*"))
  arraylen_deref_expression (:prec-left 25 (:seq _term "->" "$#" "*"))
  hash_deref_expression (:prec-left 25 (:seq _term "->" "%" "*"))
  amper_deref_expression (:prec-left 25 (:seq _term "->" "&" "*"))
  glob_deref_expression (:prec-left 25 (:seq _term "->" "*" "*"))
  require_expression (:prec-left 17 (:seq "require" _term))
  require_version_expression (:prec-left 17 (:seq "require" (:field :version _version)))
  func0op_call_expression (:seq (:field :function _func0op) (:choice (:seq "(" ")") :blank))
  func1op_call_expression (:prec-left 16
                           (:seq
                            (:field :function _func1op)
                            (:choice (:choice (:seq "(" (:choice _expr :blank) ")") :blank) _term)))
  _map_grep (:choice "map" "grep")
  map_grep_expression (:prec-left 5
                       (:choice
                        (:seq _map_grep (:field :callback block) (:field :list _listexpr))
                        (:seq
                         _map_grep
                         (:field :callback _term)
                         _PERLY_COMMA
                         (:field :list _listexpr))
                        (:seq
                         _map_grep
                         "("
                         _NONASSOC
                         (:field :callback _term)
                         _PERLY_COMMA
                         (:field :list _listexpr)
                         ")")
                        (:seq
                         _map_grep
                         "("
                         _NONASSOC
                         (:field :callback block)
                         (:field :list _listexpr)
                         ")")))
  _sort_routine (:choice (:prec 1 (:alias _bareword function)) block (:prec 1 scalar))
  sort_expression (:prec-left 5
                   (:choice
                    (:seq
                     "sort"
                     (:choice (:field :callback _sort_routine) :blank)
                     (:field :list _listexpr))
                    (:seq
                     "sort"
                     "("
                     _NONASSOC
                     (:choice (:field :callback _sort_routine) :blank)
                     (:field :list _listexpr)
                     ")")))
  _label_arg (:choice (:alias identifier label) _term)
  loopex_expression (:prec-left 1 (:seq (:field :loopex _LOOPEX) (:choice _label_arg :blank)))
  goto_expression (:prec-left 1 (:seq "goto" _label_arg))
  return_expression (:prec 5 (:seq "return" (:choice _listexpr :blank)))
  undef_expression (:prec-left 16 (:seq "undef" (:choice _term :blank)))
  _listop (:choice
           method_call_expression
           function_call_expression
           ambiguous_function_call_expression)
  indirect_object (:choice
                   (:alias _builtin_filehandle filehandle)
                   block
                   (:seq scalar (:choice _no_search_slash_plz :blank)))
  _builtin_filehandle (:choice "STDIN" "STDOUT" "STDERR")
  _unambiguous_function (:alias
                         (:choice _bareword _listop_keyword _indirob_listop amper_sub)
                         function)
  function_call_expression (:choice
                            (:seq (:field :function (:alias amper_sub function)))
                            (:seq
                             (:field :function _unambiguous_function)
                             "("
                             _NONASSOC
                             (:choice (:field :arguments _expr) :blank)
                             (:choice ")" (:alias _RECOVER_PAREN_CLOSE ")")))
                            (:seq
                             (:field :function (:alias _indirob_listop function))
                             "("
                             _NONASSOC
                             indirect_object
                             (:field :arguments _expr)
                             (:choice ")" (:alias _RECOVER_PAREN_CLOSE ")")))
                            (:seq
                             (:field :function function)
                             "("
                             _NONASSOC
                             indirect_object
                             (:field :arguments _expr)
                             (:choice ")" (:alias _RECOVER_PAREN_CLOSE ")"))))
  _tricky_indirob_hashref (:seq _PERLY_BRACE_OPEN _expr _PERLY_SEMICOLON "}")
  ambiguous_function_call_expression (:prec-right 5
                                      (:choice
                                       (:seq
                                        (:field :function (:alias _listop_keyword function))
                                        (:field :arguments _listexpr))
                                       (:seq
                                        (:field :function (:alias _indirob_listop function))
                                        (:field :arguments _listexpr))
                                       (:seq
                                        (:field :function function)
                                        (:choice _no_search_slash_plz :blank)
                                        (:field :arguments _listexpr))
                                       (:seq
                                        (:field :function function)
                                        indirect_object
                                        (:field :arguments _listexpr))
                                       (:seq
                                        (:field :function (:alias _indirob_listop function))
                                        indirect_object
                                        (:field :arguments _listexpr))
                                       (:seq
                                        (:field :function function)
                                        (:alias block indirect_object))
                                       (:seq
                                        (:field :function function)
                                        (:field :arguments
                                         (:alias _tricky_indirob_hashref anonymous_hash_expression))
                                        (:choice
                                         (:seq _PERLY_COMMA (:field :arguments _listexpr))
                                         :blank))))
  _indirob_listop (:choice "print" "printf" "say" "exec" "system")
  _listop_keyword (:choice
                   "accept"
                   "atan2"
                   "bind"
                   "binmode"
                   "bless"
                   "crypt"
                   "chmod"
                   "chown"
                   "connect"
                   "die"
                   "dbmopen"
                   "fcntl"
                   "flock"
                   "getpriority"
                   "getprotobynumber"
                   "gethostbyaddr"
                   "getnetbyaddr"
                   "getservbyname"
                   "getservbyport"
                   "getsockopt"
                   "glob"
                   "index"
                   "ioctl"
                   "join"
                   "kill"
                   "link"
                   "listen"
                   "mkdir"
                   "msgctl"
                   "msgget"
                   "msgrcv"
                   "msgsend"
                   "opendir"
                   "push"
                   "pack"
                   "pipe"
                   "rename"
                   "rindex"
                   "read"
                   "recv"
                   "reverse"
                   "select"
                   "seek"
                   "semctl"
                   "semget"
                   "semop"
                   "send"
                   "setpgrp"
                   "setpriority"
                   "seekdir"
                   "setsockopt"
                   "shmctl"
                   "shmread"
                   "shmwrite"
                   "shutdown"
                   "socket"
                   "socketpair"
                   "split"
                   "sprintf"
                   "splice"
                   "substr"
                   "symlink"
                   "syscall"
                   "sysopen"
                   "sysseek"
                   "sysread"
                   "syswrite"
                   "tie"
                   "truncate"
                   "unlink"
                   "unpack"
                   "utime"
                   "unshift"
                   "vec"
                   "warn"
                   "waitpid"
                   "formline"
                   "open")
  function _bareword
  method_call_expression (:prec-left 25
                          (:seq
                           (:field :invocant _term)
                           "->"
                           (:choice "&" :blank)
                           (:field :method method)
                           (:choice _args_subscript :blank)))
  method (:choice _bareword scalar _RECOVER_ARROW)
  _variables (:choice scalar array hash arraylen glob)
  _signature_varname (:alias _identifier varname)
  scalar (:seq "$" _var_indirob)
  _declare_scalar (:seq "$" (:choice varname _var_indirob_autoquote))
  _signature_scalar (:seq "$" _signature_varname)
  array (:seq "@" _var_indirob)
  _declare_array (:seq "@" (:choice varname _var_indirob_autoquote))
  _signature_array (:seq "@" _signature_varname)
  _HASH_PERCENT (:alias (:token (:prec 2 "%")) "%")
  _SUB_AMPER (:alias (:token (:prec 2 "&")) "&")
  _GLOB_STAR (:alias (:token (:prec 2 "*")) "*")
  hash (:seq _HASH_PERCENT _var_indirob)
  _declare_hash (:seq _HASH_PERCENT (:choice varname _var_indirob_autoquote))
  _signature_hash (:seq _HASH_PERCENT _signature_varname)
  arraylen (:seq "$#" _var_indirob)
  glob (:seq
        _GLOB_STAR
        (:choice
         (:alias _amper_indirob varname)
         _var_indirob_autoquote
         (:alias _code_deref glob_deref_expression)))
  amper_sub (:seq
             _SUB_AMPER
             (:choice
              (:alias _amper_indirob varname)
              _var_indirob_autoquote
              (:alias _code_deref code_deref_expression)))
  _amper_indirob (:choice _bareword _ident_special scalar)
  _code_deref block
  _indirob (:choice _bareword _ident_special scalar block)
  varname (:choice _identifier _ident_special)
  _var_indirob_autoquote (:seq
                          _PERLY_BRACE_OPEN
                          (:alias
                           (:choice
                            _brace_autoquoted_token
                            _bareword
                            _special_var_name
                            (:pattern "\\^\\w+"))
                           varname)
                          _brace_end_zw
                          "}")
  _var_indirob (:choice (:alias _indirob varname) _var_indirob_autoquote)
  attrlist (:prec-left 0 (:seq attribute (:repeat (:seq (:choice ":" :blank) attribute))))
  attribute (:seq
             (:field :name attribute_name)
             (:choice (:seq _attribute_value_begin "(" (:field :value attribute_value) ")") :blank))
  attribute_name _bareword
  _PERLY_COMMA (:choice "," "=>")
  _KW_USE (:choice "use" "no")
  _KW_FOR (:choice "for" "foreach")
  _LOOPEX (:choice "last" "next" "redo")
  _PHASE_NAME (:choice "BEGIN" "INIT" "CHECK" "UNITCHECK" "END")
  _func0op (:choice
            "__FILE__"
            "__LINE__"
            "__PACKAGE__"
            "__SUB__"
            "break"
            "fork"
            "getppid"
            "time"
            "times"
            "wait"
            "wantarray"
            "continue")
  _func1op (:choice
            "abs"
            "alarm"
            "chop"
            "chdir"
            "close"
            "closedir"
            "caller"
            "chomp"
            "chr"
            "cos"
            "chroot"
            "defined"
            "delete"
            "dbmclose"
            "exists"
            "exit"
            "eof"
            "exp"
            "each"
            "fc"
            "fileno"
            "gmtime"
            "getc"
            "getpgrp"
            "getprotobyname"
            "getpwname"
            "getpwuid"
            "getpeername"
            "getnetbyname"
            "getsockname"
            "getgrnam"
            "getgrgid"
            "hex"
            "int"
            "keys"
            "lc"
            "lcfirst"
            "length"
            "localtime"
            "log"
            "lock"
            "lstat"
            "oct"
            "ord"
            "prototype"
            "pop"
            "pos"
            "quotemeta"
            "reset"
            "rand"
            "rmdir"
            "readdir"
            "readline"
            "readpipe"
            "rewinddir"
            "readlink"
            "ref"
            "scalar"
            "shift"
            "sin"
            "sleep"
            "sqrt"
            "srand"
            "stat"
            "study"
            "tell"
            "telldir"
            "tied"
            "uc"
            "ucfirst"
            "untie"
            "umask"
            "values"
            "write"
            (:alias _filetest "-x"))
  comment (:pattern "#.*")
  __DATA__ (:seq
            (:choice
             (:seq
              (:alias (:choice "__DATA__" "__END__") eof_marker)
              (:pattern ".*")
              (:alias _gobbled_content data_section))
             (:seq (:alias (:choice "\x04" _ctrl_z_hack) eof_marker) _gobbled_content)))
  _literal (:choice
            string_literal
            interpolated_string_literal
            command_string
            quoted_regexp
            match_regexp
            substitution_regexp
            transliteration_expression
            (:alias (:token (:prec 1 (:pattern "v[0-9]+(?:\\.[0-9]+)+"))) version))
  _apostrophe (:alias _single_quote "'")
  _quotation_mark (:alias _double_quote "'")
  _backtick (:alias _backtick_quote "'")
  _search_slash (:alias _search_slash_quote "'")
  _quotelike_begin (:alias _quotelike_begin_quote "'")
  _quotelike_middle_close (:alias _quotelike_middle_close_quote "'")
  _quotelike_end (:alias _quotelike_end_quote "'")
  _quotelike_middle (:seq _quotelike_middle_close (:choice _quotelike_middle_skip _quotelike_begin))
  string_literal (:seq
                  (:choice (:seq "q" _quotelike_begin) _apostrophe)
                  (:choice
                   (:field :content (:alias _noninterpolated_string_content string_content))
                   :blank)
                  _quotelike_end)
  interpolated_string_literal (:seq
                               (:choice (:seq "qq" _quotelike_begin) _quotation_mark)
                               (:choice
                                (:field :content
                                 (:alias _interpolated_string_content string_content))
                                :blank)
                               _quotelike_end)
  _subscripted_interpolations (:choice
                               (:alias _array_element_interpolation array_element_expression)
                               (:alias _hash_element_interpolation hash_element_expression))
  _braced_scalar (:seq "$" (:choice block _var_indirob_autoquote) _NONASSOC)
  _braced_array (:seq "@" (:choice block _var_indirob_autoquote) _NONASSOC)
  _interp_arrow (:token-immediate "->")
  _array_element_interpolation (:choice
                                (:seq
                                 (:field :array (:alias scalar container_variable))
                                 (:token-immediate "[")
                                 (:field :index _expr)
                                 "]")
                                (:prec-left 25
                                 (:seq scalar _interp_arrow "[" (:field :index _expr) "]"))
                                (:seq
                                 _subscripted_interpolations
                                 (:choice _interp_arrow :blank)
                                 (:token-immediate "[")
                                 (:field :index _expr)
                                 "]"))
  _hash_element_interpolation (:choice
                               (:seq
                                (:field :hash (:alias scalar container_variable))
                                (:token-immediate "{")
                                (:field :key _hash_key)
                                "}")
                               (:prec-left 25
                                (:seq scalar _interp_arrow "{" (:field :key _hash_key) "}"))
                               (:seq
                                _subscripted_interpolations
                                (:choice _interp_arrow :blank)
                                (:token-immediate "{")
                                (:field :key _hash_key)
                                "}"))
  _slice_expression_interpolation (:choice
                                   (:seq
                                    (:field :array (:alias array slice_container_variable))
                                    (:token-immediate "[")
                                    _expr
                                    "]")
                                   (:seq
                                    (:field :hash (:alias array slice_container_variable))
                                    (:token-immediate "{")
                                    _hash_key
                                    "}")
                                   (:prec-left 25
                                    (:seq (:field :arrayref scalar) _interp_arrow "@" "[" _expr "]"))
                                   (:prec-left 25
                                    (:seq
                                     (:field :hashref scalar)
                                     _interp_arrow
                                     "@"
                                     "{"
                                     _hash_key
                                     "}")))
  _scalar_deref_interpolation (:prec-left 25 (:seq scalar _interp_arrow (:token-immediate "$*")))
  _array_deref_interpolation (:prec-left 25
                              (:seq (:field :arrayref scalar) _interp_arrow (:token-immediate "@*")))
  _interpolations (:choice
                   scalar
                   arraylen
                   array
                   (:alias _scalar_deref_interpolation scalar_deref_expression)
                   (:alias _array_deref_interpolation array_deref_expression)
                   (:alias _braced_scalar scalar)
                   (:alias _braced_array array)
                   _subscripted_interpolations
                   (:alias _slice_expression_interpolation slice_expression))
  _noninterpolated_string_content (:repeat1
                                   (:choice _q_string_content escape_sequence escaped_delimiter))
  _interpolation_fallbacks (:choice
                            (:seq "@" _no_interp_whitespace_zw)
                            (:seq
                             "@"
                             (:choice
                              (:pattern "[^A-Za-z0-9_\\$'+:-]")
                              _quotelike_end_zw
                              _quotelike_end))
                            (:seq
                             scalar
                             (:alias (:token-immediate "->") "not-interpolated")
                             (:choice
                              (:seq (:choice (:choice "@" "$") :blank) _no_interp_whitespace_zw)
                              scalar
                              (:pattern "[^@${\\[]")))
                            _nonvar_interpolation_fallbacks)
  _nonvar_interpolation_fallbacks (:choice
                                   (:alias "-" "not-interpolated")
                                   (:alias "{" "not-interpolated")
                                   (:alias "[" "not-interpolated"))
  _interpolated_string_content (:repeat1
                                (:choice
                                 _qq_string_content
                                 _interpolation_fallbacks
                                 escape_sequence
                                 escaped_delimiter
                                 _interpolations))
  quoted_word_list (:seq
                    "qw"
                    _quotelike_begin
                    (:choice
                     (:field :content (:alias _noninterpolated_string_content string_content))
                     :blank)
                    _quotelike_end)
  command_string (:choice
                  (:seq
                   (:choice (:seq "qx" _quotelike_begin) _backtick)
                   (:choice
                    (:field :content (:alias _interpolated_string_content string_content))
                    :blank)
                   _quotelike_end)
                  (:seq
                   "qx"
                   _apostrophe
                   (:choice
                    (:field :content (:alias _noninterpolated_string_content string_content))
                    :blank)
                   _quotelike_end))
  quoted_regexp (:seq
                 "qr"
                 (:choice
                  (:seq
                   _quotelike_begin
                   (:choice
                    (:field :content (:alias _interpolated_regexp_content regexp_content))
                    :blank))
                  (:seq
                   _apostrophe
                   (:choice
                    (:field :content (:alias _noninterpolated_string_content regexp_content))
                    :blank)))
                 _quotelike_end
                 (:choice (:field :modifiers quoted_regexp_modifiers) :blank))
  match_regexp (:seq
                (:choice
                 (:seq
                  (:choice
                   (:seq
                    (:choice _search_slash (:seq (:field :operator "m") _quotelike_begin))
                    (:choice
                     (:field :content (:alias _interpolated_regexp_content regexp_content))
                     :blank))
                   (:seq
                    (:field :operator "m")
                    _apostrophe
                    (:choice
                     (:field :content (:alias _noninterpolated_string_content regexp_content))
                     :blank)))
                  _quotelike_end)
                 "//")
                (:choice (:field :modifiers match_regexp_modifiers) :blank))
  substitution_regexp (:seq
                       (:field :operator "s")
                       (:choice
                        (:seq
                         _quotelike_begin
                         (:choice
                          (:field :content (:alias _interpolated_regexp_content regexp_content))
                          :blank)
                         _quotelike_middle
                         (:choice (:alias _interpolated_string_content replacement) :blank))
                        (:seq
                         _apostrophe
                         (:choice
                          (:field :content (:alias _noninterpolated_string_content regexp_content))
                          :blank)
                         _quotelike_middle
                         (:choice (:alias _noninterpolated_string_content replacement) :blank)))
                       _quotelike_end
                       (:choice (:field :modifiers substitution_regexp_modifiers) :blank))
  _interpolated_regexp_content (:repeat1
                                (:choice
                                 _qq_string_content
                                 escape_sequence
                                 escaped_delimiter
                                 _dollar_in_regexp
                                 (:alias _regexp_open_bracket "[")
                                 (:alias _regexp_open_brace "{")
                                 _interpolation_fallbacks
                                 _interpolations
                                 (:seq "$" _no_interp_whitespace_zw)))
  quoted_regexp_modifiers (:token-immediate (:prec 2 (:pattern "[msixpodualn]+")))
  match_regexp_modifiers (:token-immediate (:prec 2 (:pattern "[msixpogcdualn]+")))
  substitution_regexp_modifiers (:token-immediate (:prec 2 (:pattern "[msixpogcedualr]+")))
  transliteration_modifiers (:token-immediate (:prec 2 (:pattern "[cdsr]+")))
  _interpolated_transliteration_content (:repeat1
                                         (:choice
                                          _qq_string_content
                                          _nonvar_interpolation_fallbacks
                                          (:seq (:choice "$" "@") (:pattern "."))
                                          escape_sequence
                                          escaped_delimiter))
  transliteration_expression (:seq
                              (:field :operator (:choice "tr" "y"))
                              (:choice
                               (:seq
                                _quotelike_begin
                                (:choice
                                 (:field :content
                                  (:alias
                                   _interpolated_transliteration_content
                                   transliteration_content))
                                 :blank)
                                _quotelike_middle
                                (:choice
                                 (:alias _interpolated_transliteration_content replacement)
                                 :blank))
                               (:seq
                                _apostrophe
                                (:choice
                                 (:field :content
                                  (:alias _noninterpolated_string_content transliteration_content))
                                 :blank)
                                _quotelike_middle
                                (:choice
                                 (:alias _noninterpolated_string_content replacement)
                                 :blank)))
                              _quotelike_end
                              (:choice (:field :modifiers transliteration_modifiers) :blank))
  heredoc_token (:seq _PERLY_HEREDOC _heredoc_delimiter)
  command_heredoc_token (:seq _PERLY_HEREDOC _command_heredoc_delimiter)
  heredoc_content (:seq
                   _heredoc_start
                   (:repeat
                    (:choice
                     _heredoc_middle
                     escape_sequence
                     _interpolations
                     _interpolation_fallbacks))
                   heredoc_end)
  package _bareword
  _version (:prec 1 (:choice number version))
  version (:token (:prec 2 (:pattern "v[0-9]+(?:\\.[0-9]+)*|[0-9]+(?:\\.[0-9]+){2,}")))
  _conditionals (:choice "if" "unless")
  _loops (:choice "while" "until")
  autoquoted_bareword (:choice
                       (:prec-dynamic 20 (:prec 26 (:seq "-" _bareword)))
                       (:seq
                        (:choice _fat_comma_autoquoted_ahead :blank)
                        (:choice "-" :blank)
                        _fat_comma_autoquoted))
  _brace_autoquoted (:alias _brace_autoquoted_token autoquoted_bareword)
  identifier (:prec 2 _identifier)
  _identifier (:pattern "[_\\p{XID_Start}][\\p{XID_Continue}]*" "v")
  _special_var_name (:pattern "[0-9]+|\\^([A-Z[?\\^_]|])|\\S")
  _ident_special (:choice _special_var_name (:seq "$" _dollar_ident_zw))
  bareword (:prec-dynamic 1 _bareword)
  _bareword (:choice
             _identifier
             (:pattern "(::)*[_\\p{XID_Start}][\\p{XID_Continue}]*((::)+[_\\p{XID_Start}][\\p{XID_Continue}]*)*(::)*|(::)+" "v"))
  primitive (:choice number boolean)
  number (:token
          (:choice
           (:choice
            (:seq
             (:choice
              "0"
              (:seq
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank)))
             "."
             (:choice (:pattern "\\d(_?\\d)*") :blank)
             (:choice
              (:seq (:choice "e" "E") (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*"))
              :blank))
            (:seq
             "."
             (:pattern "\\d(_?\\d)*")
             (:choice
              (:seq (:choice "e" "E") (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*"))
              :blank))
            (:seq
             (:choice
              "0"
              (:seq
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank)))
             (:seq (:choice "e" "E") (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*")))
            (:pattern "\\d(_?\\d)*"))
           (:seq (:choice "0x" "0X") (:pattern "[\\da-fA-F](_?[\\da-fA-F])*"))
           (:seq (:choice "0b" "0B") (:pattern "[0-1](_?[0-1])*"))
           (:seq "0" (:pattern "[0-7](_?[0-7])*"))))
  boolean (:choice "true" "false")}}
