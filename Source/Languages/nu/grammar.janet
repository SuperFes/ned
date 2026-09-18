# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "nu"
 :word identifier
 :extras [(:pattern "[ \\t]") comment]
 :conflicts [[_binary_predicate_parenthesized]
             [_block_body record_body val_closure]
             [_block_body shebang]
             [_block_body val_closure]
             [_block_body]
             [_expression_parenthesized _expr_binary_expression_parenthesized]
             [_match_pattern_list val_list]
             [_match_pattern_list_body _table_head]
             [_match_pattern_list_body list_body _table_head]
             [_match_pattern_list_body list_body]
             [_match_pattern_list_body val_entry]
             [_match_pattern_list_body]
             [_match_pattern_record val_record val_closure]
             [_match_pattern_record val_record]
             [_match_pattern_record_body record_body]
             [_match_pattern_value _value]
             [_parenthesized_body]
             [block val_closure]
             [block val_record val_closure]
             [command record_entry]
             [ctrl_if_parenthesized]
             [ctrl_try_parenthesized]
             [expr_binary_parenthesized]
             [list_body _table_head]
             [list_body]
             [parameter param_type param_value]
             [pipeline]
             [pipeline_parenthesized]
             [val_record val_closure]]
 :precedences []
 :externals [raw_string_begin raw_string_content raw_string_end]
 :inline [_flag_value
          _item_expression
          _match_expression
          _separator
          _spread_listish
          _spread_recordish
          _stringish
          _terminator]
 :supertypes []
 :rules
 {nu_script (:seq (:choice shebang :blank) (:choice _block_body :blank))
  shebang (:seq (:choice _repeat_newline :blank) "#!" (:pattern ".*\\r?\\n?"))
  _block_body_statement (:choice _declaration _statement)
  _declaration (:choice decl_alias decl_def decl_export decl_extern decl_module decl_use)
  decl_alias (:seq (:choice "export" :blank) "alias" _command_name "=" (:field :value pipeline))
  stmt_let (:prec-right 1 (:seq "let" _assignment_pattern))
  stmt_mut (:prec-right 1 (:seq "mut" _assignment_pattern))
  stmt_const (:prec-right 1 (:seq (:choice "export" :blank) "const" _assignment_pattern))
  assignment (:prec-right 1 _mutable_assignment_pattern)
  _assignment_pattern (:seq
                       (:field :name _variable_name)
                       (:field :type (:choice param_type :blank))
                       "="
                       (:field :value pipeline))
  _mutable_assignment_pattern (:seq
                               (:field :lhs val_variable)
                               (:field :opr (:choice "=" "+=" "-=" "*=" "/=" "++="))
                               (:field :rhs pipeline))
  _statement (:choice _ctrl_statement stmt_let stmt_mut stmt_const pipeline)
  pipeline (:seq
            (:repeat (:seq pipe_element _pipe_separator (:choice _newline :blank)))
            pipe_element)
  _block_body_statement_parenthesized (:choice _declaration_parenthesized _statement_parenthesized)
  _declaration_parenthesized (:choice
                              (:alias decl_alias_parenthesized decl_alias)
                              decl_def
                              decl_export
                              decl_extern
                              decl_module
                              decl_use)
  decl_alias_parenthesized (:seq
                            (:choice "export" :blank)
                            "alias"
                            _command_name
                            "="
                            (:field :value (:alias pipeline_parenthesized pipeline)))
  stmt_let_parenthesized (:prec-right 1 (:seq "let" _assignment_pattern_parenthesized))
  stmt_mut_parenthesized (:prec-right 1 (:seq "mut" _assignment_pattern_parenthesized))
  stmt_const_parenthesized (:prec-right 1
                            (:seq
                             (:choice "export" :blank)
                             "const"
                             _assignment_pattern_parenthesized))
  assignment_parenthesized (:prec-right 1 _mutable_assignment_pattern_parenthesized)
  _assignment_pattern_parenthesized (:seq
                                     (:field :name _variable_name)
                                     (:field :type (:choice param_type :blank))
                                     "="
                                     (:field :value (:alias pipeline_parenthesized pipeline)))
  _mutable_assignment_pattern_parenthesized (:seq
                                             (:field :lhs val_variable)
                                             (:field :opr (:choice "=" "+=" "-=" "*=" "/=" "++="))
                                             (:field :rhs (:alias pipeline_parenthesized pipeline)))
  _statement_parenthesized (:choice
                            _ctrl_statement
                            (:alias stmt_let_parenthesized stmt_let)
                            (:alias stmt_mut_parenthesized stmt_mut)
                            (:alias stmt_const_parenthesized stmt_const)
                            (:alias pipeline_parenthesized pipeline))
  pipeline_parenthesized (:seq
                          (:repeat
                           (:seq
                            (:alias pipe_element_parenthesized pipe_element)
                            _pipe_separator
                            (:choice _repeat_newline :blank)))
                          (:alias pipe_element_parenthesized pipe_element))
  _block_body (:choice
               (:repeat1 _terminator)
               (:prec 20
                (:seq
                 (:repeat _terminator)
                 (:repeat (:seq _block_body_statement (:repeat1 _terminator)))
                 _block_body_statement
                 (:repeat _terminator))))
  cmd_identifier (:choice
                  (:token
                   (:prec -1
                    (:seq
                     (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,^@#$\\-]")
                     (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]")))))
                  (:seq
                   (:alias "def" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "alias" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "use" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "export-env" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "extern" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "module" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "let" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "mut" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "const" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "for" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "loop" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "while" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "if" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "else" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "try" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "catch" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "finally" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "match" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "in" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "export" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "true" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "false" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias "null" "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias (:pattern "[iI][nN][fF]([iI][nN][iI][tT][yY])?") "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias (:pattern "-[iI][nN][fF]([iI][nN][iI][tT][yY])?") "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   (:alias (:pattern "[nN][aA][nN]") "_prefix")
                   (:token-immediate
                    (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))
                  (:seq
                   _val_number_decimal
                   (:choice
                    (:choice (:alias duration_unit "_unit") (:alias filesize_unit "_unit"))
                    :blank)
                   (:choice
                    (:choice
                     (:token-immediate "**")
                     (:token-immediate "++")
                     (:token-immediate "*")
                     (:token-immediate "/")
                     (:token-immediate "mod")
                     (:token-immediate "//")
                     (:token-immediate "+")
                     (:token-immediate "-")
                     (:token-immediate "bit-shl")
                     (:token-immediate "bit-shr")
                     (:token-immediate "=~")
                     (:token-immediate "!~")
                     (:token-immediate "like")
                     (:token-immediate "not-like")
                     (:token-immediate "bit-and")
                     (:token-immediate "bit-xor")
                     (:token-immediate "bit-or")
                     (:token-immediate "and")
                     (:token-immediate "xor")
                     (:token-immediate "or")
                     (:token-immediate "in")
                     (:token-immediate "not-in")
                     (:token-immediate "has")
                     (:token-immediate "not-has")
                     (:token-immediate "starts-with")
                     (:token-immediate "not-starts-with")
                     (:token-immediate "ends-with")
                     (:token-immediate "not-ends-with")
                     (:token-immediate "==")
                     (:token-immediate "!=")
                     (:token-immediate "<")
                     (:token-immediate "<=")
                     (:token-immediate ">")
                     (:token-immediate ">=")
                     (:token-immediate "=~")
                     (:token-immediate "!~")
                     (:token-immediate "like")
                     (:token-immediate "not-like"))
                    :blank)
                   (:token-immediate
                    (:prec -1 (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\{}<>=\"`':,]"))))))
  identifier (:token
              (:seq
               (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\-{}<>=\"`'@?,:.&*!^+#$]")
               (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\-{}<>=\"`'@?,:.]"))))
  long_flag_identifier (:token-immediate
                        (:pattern "[0-9\\p{XID_Start}_][\\p{XID_Continue}?._-]*" "u"))
  _command_name (:choice (:field :unquoted_name cmd_identifier) (:field :quoted_name val_string))
  _variable_name (:choice (:field :var_name identifier) (:field :dollar_name val_variable))
  _newline (:pattern "\\r?\\n")
  _repeat_newline (:repeat1 _newline)
  _space (:pattern "[ \\t]+")
  _separator (:choice _space _newline)
  _terminator (:choice ";" _newline)
  _pipe_separator (:repeat1
                   (:seq
                    (:choice _repeat_newline :blank)
                    (:choice
                     "|"
                     "err>|"
                     "out>|"
                     "e>|"
                     "o>|"
                     "err+out>|"
                     "out+err>|"
                     "o+e>|"
                     "e+o>|")))
  attribute_list (:repeat1 (:seq attribute (:choice ";" _newline)))
  attribute_identifier (:token-immediate
                        (:pattern "[0-9\\p{XID_Start}][0-9\\p{XID_Continue}_-]*" "u"))
  attribute (:seq
             "@"
             (:field :type attribute_identifier)
             (:repeat (:seq _space (:choice _cmd_arg :blank))))
  decl_def (:seq
            (:choice attribute_list :blank)
            (:choice "export" :blank)
            "def"
            (:repeat long_flag)
            _command_name
            (:repeat long_flag)
            (:field :parameters (:choice parameter_parens parameter_bracks))
            (:field :return_type (:choice returns :blank))
            (:field :body block))
  decl_export (:seq "export-env" (:field :body block))
  decl_extern (:seq
               (:choice attribute_list :blank)
               (:choice "export" :blank)
               "extern"
               _command_name
               (:field :signature (:choice parameter_parens parameter_bracks))
               (:field :body (:choice block :blank)))
  decl_module (:seq
               (:choice "export" :blank)
               "module"
               _command_name
               (:choice (:field :body block) :blank))
  decl_use (:seq
            (:choice "export" :blank)
            "use"
            (:field :module (:choice unquoted _stringish))
            (:choice (:field :import_pattern scope_pattern) :blank))
  returns (:seq (:choice ":" :blank) (:choice _multiple_types _one_type))
  _one_type (:seq _type_annotation "->" _type_annotation)
  _types_body (:prec 20
               (:seq
                (:repeat _newline)
                (:repeat (:seq _one_type (:repeat1 _entry_separator)))
                _one_type
                (:repeat _entry_separator)))
  _multiple_types (:seq "[" (:choice _types_body :blank) "]")
  parameter_parens (:seq "(" (:choice _repeat_newline :blank) (:repeat parameter) ")")
  parameter_bracks (:seq "[" (:choice _repeat_newline :blank) (:repeat parameter) "]")
  parameter_pipes (:seq "|" (:choice _repeat_newline :blank) (:repeat parameter) "|")
  parameter (:seq
             (:choice
              _param_name
              (:seq
               (:field :param_long_flag param_long_flag)
               (:field :flag_capsule (:choice flag_capsule :blank))))
             (:repeat (:choice param_type param_value))
             (:repeat (:choice _newline ",")))
  _param_name (:choice
               (:field :param_rest param_rest)
               (:field :param_optional param_opt)
               (:field :param_name (:seq (:choice "$" :blank) identifier))
               (:field :param_short_flag param_short_flag))
  param_type (:seq
              (:choice _repeat_newline :blank)
              ":"
              (:choice _repeat_newline :blank)
              _type_annotation
              (:field :completion (:choice param_completer :blank)))
  param_value (:seq
               (:choice _repeat_newline :blank)
               "="
               (:choice _repeat_newline :blank)
               (:field :param_value
                (:choice
                 _item_expression
                 (:alias _unquoted_in_record val_string)
                 (:alias _unquoted_in_record_with_expr val_string))))
  _type_annotation (:field :type (:choice list_type collection_type flat_type composite_type))
  _all_type (:field :type (:choice list_type collection_type flat_type composite_type))
  flat_type (:field :flat_type
             (:choice
              "any"
              "binary"
              "block"
              "bool"
              "cell-path"
              "closure"
              "cond"
              "datetime"
              "directory"
              "duration"
              "directory"
              "duration"
              "error"
              "expr"
              "float"
              "decimal"
              "filesize"
              "full-cell-path"
              "glob"
              "int"
              "import-pattern"
              "keyword"
              "math"
              "nothing"
              "number"
              "one-of"
              "operator"
              "path"
              "range"
              "signature"
              "string"
              "table"
              "variable"
              "var-with-opt-type"
              "record"
              "list"))
  _collection_annotation (:seq ":" _all_type (:field :completion (:choice param_completer :blank)))
  _collection_entry (:seq
                     (:field :key
                      (:choice
                       (:alias _unquoted_in_record identifier)
                       (:alias val_string identifier)))
                     (:choice _collection_annotation :blank))
  _collection_body (:prec 20
                    (:seq
                     (:repeat _newline)
                     (:repeat (:seq _collection_entry (:repeat1 _entry_separator)))
                     _collection_entry
                     (:repeat _entry_separator)))
  collection_type (:seq
                   (:choice "record" "table")
                   (:token-immediate "<")
                   (:choice _collection_body :blank)
                   ">")
  list_type (:seq
             "list"
             (:token-immediate "<")
             (:field :inner (:choice _all_type :blank))
             (:field :completion (:choice param_completer :blank))
             ">")
  _composite_argument_body (:prec 20
                            (:seq
                             (:repeat _newline)
                             (:repeat (:seq _all_type (:repeat1 _entry_separator)))
                             _all_type
                             (:repeat _entry_separator)))
  composite_type (:seq "oneof" (:token-immediate "<") _composite_argument_body ">")
  param_completer (:seq
                   (:token-immediate "@")
                   (:choice
                    (:field :command cmd_identifier)
                    (:field :command val_string)
                    (:field :constant val_list)
                    (:field :constant val_record)
                    (:field :constant val_variable)))
  param_rest (:seq "..." (:choice "$" :blank) (:field :name identifier))
  param_opt (:seq (:field :name identifier) (:token-immediate "?"))
  param_long_flag (:seq "--" long_flag_identifier)
  flag_capsule (:seq "(" param_short_flag ")")
  param_short_flag (:seq "-" (:field :name param_short_flag_identifier))
  param_short_flag_identifier (:token-immediate
                               (:pattern "[\\p{Punctuation}\\p{Symbol}\\p{XID_Continue}]" "u"))
  _ctrl_statement (:choice ctrl_for ctrl_loop ctrl_while)
  _ctrl_expression (:choice ctrl_if ctrl_try ctrl_match)
  _ctrl_expression_parenthesized (:choice
                                  (:alias ctrl_if_parenthesized ctrl_if)
                                  (:alias ctrl_try_parenthesized ctrl_try)
                                  ctrl_match)
  ctrl_for (:seq
            "for"
            (:field :looping_var _variable_name)
            "in"
            (:field :iterable _expression)
            (:field :body block))
  ctrl_loop (:seq "loop" (:field :body block))
  ctrl_while (:seq "while" (:field :condition _expression) (:field :body block))
  ctrl_if (:seq
           "if"
           (:field :condition _expression)
           (:field :then_branch block)
           (:choice
            (:seq
             "else"
             (:choice
              (:field :else_block (:choice block _expression command))
              (:field :else_branch ctrl_if)))
            :blank))
  ctrl_if_parenthesized (:seq
                         "if"
                         (:choice _repeat_newline :blank)
                         (:field :condition _expression_parenthesized)
                         (:choice _repeat_newline :blank)
                         (:field :then_branch block)
                         (:choice _repeat_newline :blank)
                         (:choice
                          (:seq
                           (:choice _repeat_newline :blank)
                           "else"
                           (:choice _repeat_newline :blank)
                           (:choice
                            (:field :else_block (:choice block _expression_parenthesized command))
                            (:field :else_branch (:alias ctrl_if_parenthesized ctrl_if))))
                          :blank))
  _ctrl_match_body (:prec 20
                    (:seq
                     (:repeat _newline)
                     (:repeat (:seq (:choice match_arm default_arm) (:repeat1 _entry_separator)))
                     (:choice match_arm default_arm)
                     (:repeat _entry_separator)))
  ctrl_match (:seq
              "match"
              (:field :scrutinee (:choice _item_expression (:alias unquoted val_string)))
              "{"
              (:choice _ctrl_match_body :blank)
              "}")
  match_arm (:seq (:field :pattern match_pattern) "=>" (:field :expression _match_expression))
  default_arm (:seq (:field :default_pattern "_") "=>" (:field :expression _match_expression))
  _match_expression (:choice _item_expression (:prec-dynamic 10 block))
  match_pattern (:choice
                 (:seq "_" match_guard)
                 (:seq _match_pattern (:choice match_guard :blank))
                 (:seq _match_pattern (:repeat (:seq (:choice _newline :blank) "|" _match_pattern))))
  _match_pattern (:choice _match_pattern_expression (:alias unquoted val_string))
  match_guard (:seq "if" _expression)
  _match_pattern_expression (:choice _match_pattern_value val_range expr_parenthesized)
  _match_pattern_value (:choice
                        val_variable
                        val_nothing
                        val_bool
                        val_number
                        val_duration
                        val_filesize
                        val_binary
                        val_string
                        val_date
                        (:alias _match_pattern_list val_list)
                        (:alias _match_pattern_record val_record)
                        val_table)
  _match_pattern_list_body (:choice
                            (:repeat1 (:choice _newline ","))
                            (:prec 20
                             (:seq
                              (:repeat _newline)
                              (:repeat
                               (:seq
                                (:field :entry
                                 (:choice
                                  _match_pattern_expression
                                  (:alias _unquoted_in_list val_string)))
                                (:repeat1 _entry_separator)))
                              (:field :entry
                               (:choice
                                _match_pattern_expression
                                (:alias _unquoted_in_list val_string)))
                              (:repeat _entry_separator))))
  _match_pattern_list (:seq
                       "["
                       (:choice (:alias _match_pattern_list_body list_body) :blank)
                       (:choice
                        (:field :rest (:choice (:alias _match_pattern_rest val_variable) ".."))
                        :blank)
                       "]")
  _match_pattern_rest (:seq ".." (:seq (:token-immediate "$") identifier))
  _match_pattern_record_body (:prec 20
                              (:seq
                               (:repeat _newline)
                               (:repeat
                                (:seq
                                 (:field :entry (:choice record_entry val_variable))
                                 (:repeat1 _entry_separator)))
                               (:field :entry (:choice record_entry val_variable))
                               (:repeat _entry_separator)))
  _match_pattern_record (:seq
                         "{"
                         (:choice (:alias _match_pattern_record_body record_body) :blank)
                         "}"
                         (:choice cell_path :blank))
  ctrl_try (:seq
            "try"
            (:field :try_branch block)
            (:choice (:seq "catch" (:field :catch_branch _blosure)) :blank)
            (:choice (:seq "finally" (:field :finally_branch _blosure)) :blank))
  ctrl_try_parenthesized (:seq
                          "try"
                          (:choice _repeat_newline :blank)
                          (:field :try_branch block)
                          (:choice _repeat_newline :blank)
                          (:choice
                           (:seq
                            (:choice _repeat_newline :blank)
                            "catch"
                            (:choice _repeat_newline :blank)
                            (:field :catch_branch _blosure))
                           :blank)
                          (:choice _repeat_newline :blank)
                          (:choice
                           (:seq
                            (:choice _repeat_newline :blank)
                            "finally"
                            (:choice _repeat_newline :blank)
                            (:field :finally_branch _blosure))
                           :blank))
  _stmt_let_shortcut (:prec-left 0
                      (:seq
                       "let"
                       (:field :name _variable_name)
                       (:field :type (:choice param_type :blank))))
  pipe_element (:choice
                (:seq
                 (:repeat (:seq env_var (:repeat1 _space)))
                 _expression
                 (:choice redirection :blank))
                (:seq (:repeat (:seq env_var (:repeat1 _space))) command)
                _ctrl_expression
                where_command
                assignment
                (:alias _stmt_let_shortcut stmt_let))
  pipe_element_parenthesized (:choice
                              (:seq
                               (:repeat (:seq env_var (:repeat1 _separator)))
                               _expression_parenthesized
                               (:choice redirection :blank))
                              (:seq
                               (:repeat (:seq env_var (:repeat1 _separator)))
                               (:alias _command_parenthesized command))
                              _ctrl_expression_parenthesized
                              (:alias assignment_parenthesized assignment)
                              (:alias where_command_parenthesized where_command)
                              (:alias _stmt_let_shortcut stmt_let))
  scope_pattern (:choice
                 (:field :wildcard wild_card)
                 _command_name
                 (:field :command_list command_list))
  wild_card (:token "*")
  _command_list_body (:prec 20
                      (:seq
                       (:repeat _newline)
                       (:repeat (:seq (:field :cmd _command_name) (:repeat1 _entry_separator)))
                       (:field :cmd _command_name)
                       (:repeat _entry_separator)))
  command_list (:seq "[" (:choice _command_list_body :blank) "]")
  block (:seq "{" (:choice _block_body :blank) "}")
  _blosure (:choice block val_closure)
  _where_predicate_lhs_path_head (:seq
                                  (:choice
                                   (:token
                                    (:prec -1
                                     (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}.,:?!]"))))
                                   val_string)
                                  (:choice _path_suffix :blank))
  _where_predicate_lhs (:seq (:alias _where_predicate_lhs_path_head path) (:repeat path))
  where_command (:seq
                 "where"
                 (:field :predicate
                  (:choice where_predicate val_closure (:alias _binary_predicate where_predicate))))
  where_command_parenthesized (:prec-left 0
                               (:seq
                                "where"
                                (:choice _repeat_newline :blank)
                                (:field :predicate
                                 (:choice
                                  where_predicate
                                  val_closure
                                  (:alias _binary_predicate_parenthesized where_predicate)))
                                (:choice _repeat_newline :blank)))
  _binary_predicate (:choice
                     (:prec-left 4
                      (:seq
                       (:field :lhs (:choice where_predicate _binary_predicate))
                       (:field :opr "and")
                       (:field :rhs (:choice where_predicate _binary_predicate))))
                     (:prec-left 3
                      (:seq
                       (:field :lhs (:choice where_predicate _binary_predicate))
                       (:field :opr "xor")
                       (:field :rhs (:choice where_predicate _binary_predicate))))
                     (:prec-left 2
                      (:seq
                       (:field :lhs (:choice where_predicate _binary_predicate))
                       (:field :opr "or")
                       (:field :rhs (:choice where_predicate _binary_predicate)))))
  _binary_predicate_parenthesized (:choice
                                   (:prec-left 4
                                    (:seq
                                     (:field :lhs
                                      (:choice where_predicate _binary_predicate_parenthesized))
                                     (:choice _repeat_newline :blank)
                                     (:field :opr "and")
                                     (:choice _repeat_newline :blank)
                                     (:field :rhs
                                      (:choice where_predicate _binary_predicate_parenthesized))
                                     (:choice _repeat_newline :blank)))
                                   (:prec-left 3
                                    (:seq
                                     (:field :lhs
                                      (:choice where_predicate _binary_predicate_parenthesized))
                                     (:choice _repeat_newline :blank)
                                     (:field :opr "xor")
                                     (:choice _repeat_newline :blank)
                                     (:field :rhs
                                      (:choice where_predicate _binary_predicate_parenthesized))
                                     (:choice _repeat_newline :blank)))
                                   (:prec-left 2
                                    (:seq
                                     (:field :lhs
                                      (:choice where_predicate _binary_predicate_parenthesized))
                                     (:choice _repeat_newline :blank)
                                     (:field :opr "or")
                                     (:choice _repeat_newline :blank)
                                     (:field :rhs
                                      (:choice where_predicate _binary_predicate_parenthesized))
                                     (:choice _repeat_newline :blank))))
  where_predicate (:choice
                   val_bool
                   val_variable
                   expr_unary
                   expr_parenthesized
                   (:prec-left 9
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "in")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 9
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "not-in")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 9
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "has")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 9
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "not-has")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 9
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "starts-with")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 9
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "not-starts-with")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 9
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "ends-with")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 9
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "not-ends-with")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 10
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "==")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 10
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "!=")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 10
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "<")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 10
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "<=")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 10
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr ">")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 10
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr ">=")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 8
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "=~")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 8
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "!~")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 8
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "like")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string)))))
                   (:prec-left 8
                    (:seq
                     (:field :lhs (:choice _where_predicate_lhs val_variable expr_parenthesized))
                     (:field :opr "not-like")
                     (:field :rhs
                      (:choice
                       _value
                       val_range
                       expr_unary
                       expr_parenthesized
                       (:alias unquoted val_string)
                       (:alias _unquoted_with_expr val_string))))))
  _expression (:choice _value expr_binary expr_unary val_range expr_parenthesized)
  _expression_parenthesized (:choice
                             _value
                             expr_unary
                             val_range
                             expr_parenthesized
                             (:alias expr_binary_parenthesized expr_binary))
  expr_unary (:choice
              (:seq
               (:alias (:token (:seq "not" (:pattern "\\s"))) "not")
               (:choice val_bool expr_parenthesized val_variable expr_unary))
              _expr_unary_minus)
  _expr_unary_minus (:seq (:token "-") (:seq (:token-immediate "(") _block_body ")"))
  expr_binary (:choice
               (:prec-left 14
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "**")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 14
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "++")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 13
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "*")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 13
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "/")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 13
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "mod")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 13
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "//")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 12
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "+")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 12
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "-")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 11
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "bit-shl")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 11
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "bit-shr")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 8
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "=~")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 8
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "!~")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 8
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "like")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 8
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "not-like")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 7
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "bit-and")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 6
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "bit-xor")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 5
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "bit-or")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 4
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "and")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 3
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "xor")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 2
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "or")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 9
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "in")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 9
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "not-in")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 9
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "has")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 9
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "not-has")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 9
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "starts-with")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 9
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "not-starts-with")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 9
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "ends-with")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 9
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "not-ends-with")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 10
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "==")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 10
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "!=")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 10
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "<")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 10
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "<=")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 10
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr ">")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 10
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr ">=")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 8
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "=~")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 8
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "!~")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 8
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "like")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string)))))
               (:prec-left 8
                (:seq
                 (:field :lhs _expr_binary_expression)
                 (:field :opr "not-like")
                 (:field :rhs
                  (:choice
                   _expr_binary_expression
                   (:alias unquoted val_string)
                   (:alias _unquoted_with_expr val_string))))))
  expr_binary_parenthesized (:choice
                             (:prec-left 14
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "**")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 14
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "++")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 13
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "*")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 13
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "/")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 13
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "mod")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 13
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "//")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 12
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "+")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 12
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "-")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 11
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "bit-shl")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 11
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "bit-shr")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 8
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "=~")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 8
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "!~")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 8
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "like")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 8
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "not-like")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 7
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "bit-and")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 6
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "bit-xor")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 5
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "bit-or")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 4
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "and")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 3
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "xor")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 2
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "or")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 9
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "in")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 9
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "not-in")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 9
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "has")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 9
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "not-has")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 9
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "starts-with")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 9
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "not-starts-with")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 9
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "ends-with")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 9
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "not-ends-with")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 10
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "==")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 10
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "!=")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 10
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "<")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 10
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "<=")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 10
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr ">")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 10
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr ">=")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 8
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "=~")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 8
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "!~")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 8
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "like")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank)))
                             (:prec-left 8
                              (:seq
                               (:field :lhs _expr_binary_expression_parenthesized)
                               (:choice _repeat_newline :blank)
                               (:field :opr "not-like")
                               (:choice _repeat_newline :blank)
                               (:field :rhs
                                (:choice
                                 _expr_binary_expression_parenthesized
                                 (:alias unquoted val_string)
                                 (:alias _unquoted_with_expr val_string)))
                               (:choice _repeat_newline :blank))))
  _expr_binary_expression (:choice _value val_range expr_binary expr_unary expr_parenthesized)
  _expr_binary_expression_parenthesized (:choice
                                         _value
                                         val_range
                                         (:alias expr_binary_parenthesized expr_binary)
                                         expr_unary
                                         expr_parenthesized)
  expr_parenthesized (:seq "(" (:choice _parenthesized_body :blank) ")" (:choice cell_path :blank))
  _spread_parenthesized (:seq
                         "...("
                         (:choice _parenthesized_body :blank)
                         ")"
                         (:choice cell_path :blank))
  _expr_parenthesized_immediate (:seq
                                 (:token-immediate "(")
                                 (:choice _parenthesized_body :blank)
                                 ")")
  _parenthesized_body (:choice
                       (:repeat1 _terminator)
                       (:prec 20
                        (:seq
                         (:repeat _terminator)
                         (:repeat
                          (:seq
                           _block_body_statement_parenthesized
                           (:repeat1 (:seq (:choice _repeat_newline :blank) ";"))
                           (:choice _repeat_newline :blank)))
                         _block_body_statement_parenthesized
                         (:repeat _terminator))))
  val_range (:prec-right 15
             (:choice
              (:seq
               (:field :start
                (:choice expr_parenthesized (:alias _val_number_decimal val_number) val_variable))
               (:token-immediate "..")
               (:token-immediate "."))
              (:seq
               (:choice (:token "..") (:token "..=") (:token "..<"))
               (:field :end
                (:choice
                 (:alias _expr_parenthesized_immediate expr_parenthesized)
                 (:alias _immediate_decimal val_number)
                 val_variable)))
              (:seq
               (:token "..")
               (:field :step
                (:choice
                 (:alias _expr_parenthesized_immediate expr_parenthesized)
                 (:alias _immediate_decimal val_number)
                 val_variable))
               (:choice (:token-immediate "..") (:token-immediate "..=") (:token-immediate "..<"))
               (:field :end
                (:choice
                 (:alias _expr_parenthesized_immediate expr_parenthesized)
                 (:alias _immediate_decimal val_number)
                 val_variable)))
              (:seq
               (:field :start
                (:choice expr_parenthesized (:alias _val_number_decimal val_number) val_variable))
               (:choice
                (:seq
                 (:token-immediate "..")
                 (:field :step
                  (:choice
                   (:alias _expr_parenthesized_immediate expr_parenthesized)
                   (:alias _immediate_decimal val_number)
                   val_variable)))
                :blank)
               (:choice (:token-immediate "..") (:token-immediate "..=") (:token-immediate "..<"))
               (:choice
                (:field :end
                 (:choice
                  (:alias _expr_parenthesized_immediate expr_parenthesized)
                  (:alias _immediate_decimal val_number)
                  val_variable))
                :blank))))
  _val_range (:prec-right 14
              (:choice
               (:seq _val_number_decimal (:token-immediate "..") (:token-immediate "."))
               (:seq (:choice (:token "..") (:token "..=") (:token "..<")) _immediate_decimal)
               (:seq
                (:token "..")
                _immediate_decimal
                (:choice (:token-immediate "..") (:token-immediate "..=") (:token-immediate "..<"))
                _immediate_decimal)
               (:seq
                _val_number_decimal
                (:choice (:seq (:token-immediate "..") _immediate_decimal) :blank)
                (:choice (:token-immediate "..") (:token-immediate "..=") (:token-immediate "..<"))
                (:choice _immediate_decimal :blank))))
  _immediate_decimal (:seq
                      (:choice
                       (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*"))
                       (:token
                        (:seq
                         (:choice (:token-immediate "-") (:token-immediate "+"))
                         (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*"))))
                       (:seq
                        (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*"))
                        (:token-immediate ".")
                        (:choice (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*")) :blank))
                       (:seq
                        (:token
                         (:seq
                          (:choice (:token-immediate "-") (:token-immediate "+"))
                          (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*"))))
                        (:token-immediate ".")
                        (:choice (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*")) :blank))
                       (:token
                        (:seq
                         (:token-immediate ".")
                         (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*"))))
                       (:token
                        (:seq
                         (:choice (:token-immediate "-") (:token-immediate "+"))
                         (:choice (:token-immediate (:pattern "_+")) :blank)
                         (:token-immediate ".")
                         (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*")))))
                      (:choice (:token-immediate (:pattern "[eE][-+]?[\\d_]*\\d[\\d_]*")) :blank))
  _value (:choice
          val_variable
          val_cellpath
          val_nothing
          val_bool
          val_number
          val_duration
          val_filesize
          val_binary
          val_string
          val_interpolated
          val_date
          val_list
          val_record
          val_table
          val_closure)
  val_nothing (:choice "null" (:seq (:token "(") (:token-immediate ")")))
  val_bool (:choice "true" "false")
  _spread_variable (:seq "...$" (:field :name identifier) (:choice cell_path :blank))
  val_variable (:seq
                "$"
                (:field :name (:choice "nu" "in" "env" identifier))
                (:choice cell_path :blank))
  val_cellpath (:seq "$" cell_path)
  val_number _val_number
  _val_number_decimal (:seq
                       (:choice
                        (:token (:pattern "[\\d_]*\\d[\\d_]*"))
                        (:token
                         (:seq
                          (:choice (:token "-") (:token "+"))
                          (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*"))))
                        (:seq
                         (:token (:pattern "[\\d_]*\\d[\\d_]*"))
                         (:token-immediate ".")
                         (:choice (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*")) :blank))
                        (:seq
                         (:token
                          (:seq
                           (:choice (:token "-") (:token "+"))
                           (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*"))))
                         (:token-immediate ".")
                         (:choice (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*")) :blank))
                        (:token
                         (:seq (:token ".") (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*"))))
                        (:token
                         (:seq
                          (:choice (:token "-") (:token "+"))
                          (:choice (:token-immediate (:pattern "_+")) :blank)
                          (:token-immediate ".")
                          (:token-immediate (:pattern "[\\d_]*\\d[\\d_]*")))))
                       (:choice (:token-immediate (:pattern "[eE][-+]?[\\d_]*\\d[\\d_]*")) :blank))
  _val_number (:choice
               _val_number_decimal
               (:pattern "0x[0-9a-fA-F_]+")
               (:pattern "0b[01_]+")
               (:pattern "0o[0-7_]+")
               (:pattern "[iI][nN][fF]([iI][nN][iI][tT][yY])?")
               (:pattern "-[iI][nN][fF]([iI][nN][iI][tT][yY])?")
               (:pattern "[nN][aA][nN]"))
  val_duration (:seq
                (:field :value (:alias _val_number_decimal val_number))
                (:field :unit duration_unit))
  val_filesize (:choice
                "0b"
                (:seq
                 (:field :value (:alias _val_number_decimal val_number))
                 (:field :unit filesize_unit)))
  filesize_unit (:token-immediate
                 (:choice
                  "b"
                  "B"
                  "kb"
                  "kB"
                  "Kb"
                  "KB"
                  "mb"
                  "mB"
                  "Mb"
                  "MB"
                  "gb"
                  "gB"
                  "Gb"
                  "GB"
                  "tb"
                  "tB"
                  "Tb"
                  "TB"
                  "pb"
                  "pB"
                  "Pb"
                  "PB"
                  "eb"
                  "eB"
                  "Eb"
                  "EB"
                  "kib"
                  "kiB"
                  "kIB"
                  "kIb"
                  "Kib"
                  "KIb"
                  "KIB"
                  "mib"
                  "miB"
                  "mIB"
                  "mIb"
                  "Mib"
                  "MIb"
                  "MIB"
                  "gib"
                  "giB"
                  "gIB"
                  "gIb"
                  "Gib"
                  "GIb"
                  "GIB"
                  "tib"
                  "tiB"
                  "tIB"
                  "tIb"
                  "Tib"
                  "TIb"
                  "TIB"
                  "pib"
                  "piB"
                  "pIB"
                  "pIb"
                  "Pib"
                  "PIb"
                  "PIB"
                  "eib"
                  "eiB"
                  "eIB"
                  "eIb"
                  "Eib"
                  "EIb"
                  "EIB"))
  duration_unit (:token-immediate (:choice "ns" "µs" "us" "ms" "sec" "min" "hr" "day" "wk"))
  val_binary (:seq
              (:choice "0b" "0o" "0x")
              (:token-immediate "[")
              (:repeat (:field :digit (:seq hex_digit (:choice "," :blank))))
              "]")
  hex_digit (:token (:pattern "[0-9a-fA-F]+"))
  val_date (:token
            (:choice
             (:pattern "[0-9]{4}-[0-9]{2}-[0-9]{2}" "i")
             (:pattern "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-9]+)?([Zz]|([\\+-])([01]\\d|2[0-3]):?([0-5]\\d)?)?")))
  _stringish (:choice val_string val_interpolated expr_parenthesized val_variable)
  val_string (:choice _str_double_quotes _str_single_quotes _str_back_ticks _raw_str)
  _raw_str (:seq raw_string_begin (:alias raw_string_content string_content) raw_string_end)
  string_content (:repeat1 (:choice _escaped_str_content escape_sequence))
  _str_double_quotes (:seq "\"" (:choice string_content :blank) "\"")
  _escaped_str_content (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+")))
  _str_single_quotes (:seq
                      "'"
                      (:alias (:token-immediate (:prec 1 (:pattern "[^']*"))) string_content)
                      (:token-immediate "'"))
  _str_back_ticks (:seq
                   "`"
                   (:alias (:token-immediate (:prec 1 (:pattern "[^`]*"))) string_content)
                   (:token-immediate "`"))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu]")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F]+\\}")
                     (:pattern "x[0-9a-fA-F]{2}"))))
  val_interpolated (:choice _inter_single_quotes _inter_double_quotes)
  escaped_interpolated_content (:token-immediate (:prec 1 (:pattern "[^\"\\\\(]+")))
  unescaped_interpolated_content (:token-immediate (:prec 1 (:pattern "[^'(]+")))
  _inter_single_quotes (:seq
                        "$'"
                        (:repeat
                         (:choice (:field :expr expr_interpolated) unescaped_interpolated_content))
                        (:token-immediate "'"))
  _inter_double_quotes (:seq
                        "$\""
                        (:repeat
                         (:choice
                          (:field :expr expr_interpolated)
                          inter_escape_sequence
                          escaped_interpolated_content))
                        (:token-immediate "\""))
  inter_escape_sequence (:token-immediate
                         (:seq
                          "\\"
                          (:choice
                           (:pattern "[^xu]")
                           (:pattern "u[0-9a-fA-F]{4}")
                           (:pattern "u\\{[0-9a-fA-F]+\\}")
                           (:pattern "x[0-9a-fA-F]{2}")
                           "(")))
  expr_interpolated (:seq "(" _parenthesized_body ")")
  val_list (:seq "[" (:choice list_body :blank) "]" (:choice cell_path :blank))
  _spread_list (:seq "...[" (:choice list_body :blank) "]" (:choice cell_path :blank))
  _spread_listish (:choice
                   (:alias _spread_list val_list)
                   (:alias _spread_variable val_variable)
                   (:alias _spread_parenthesized expr_parenthesized))
  list_body (:choice
             (:repeat1 (:choice _newline ","))
             (:prec 20
              (:seq
               (:repeat _newline)
               (:repeat (:seq (:field :entry val_entry) (:repeat1 _entry_separator)))
               (:field :entry val_entry)
               (:repeat _entry_separator))))
  val_entry (:prec 10
             (:field :item
              (:choice
               _item_expression
               (:field :spread _spread_listish)
               (:alias _unquoted_in_list val_string)
               (:alias _unquoted_in_list_with_expr val_string))))
  _item_expression (:choice _value val_range expr_parenthesized)
  val_record (:seq "{" (:choice record_body :blank) "}" (:choice cell_path :blank))
  _spread_record (:seq "...{" (:choice record_body :blank) "}" (:choice cell_path :blank))
  _spread_recordish (:choice
                     (:alias _spread_record val_record)
                     (:alias _spread_variable val_variable)
                     (:alias _spread_parenthesized expr_parenthesized))
  record_body (:prec 20
               (:seq
                (:repeat _newline)
                (:repeat (:seq (:field :entry record_entry) (:repeat1 _entry_separator)))
                (:field :entry record_entry)
                (:repeat _entry_separator)))
  _entry_separator (:token (:prec 20 (:choice "," (:pattern "\\s"))))
  record_entry (:choice
                (:field :spread _spread_recordish)
                (:seq
                 (:field :key
                  (:choice
                   (:seq (:alias cmd_identifier identifier) (:repeat _separator))
                   (:alias _record_key identifier)
                   val_string
                   val_interpolated
                   val_number
                   val_variable
                   expr_parenthesized
                   (:alias "def" identifier)
                   (:alias "alias" identifier)
                   (:alias "use" identifier)
                   (:alias "export-env" identifier)
                   (:alias "extern" identifier)
                   (:alias "module" identifier)
                   (:alias "let" identifier)
                   (:alias "mut" identifier)
                   (:alias "const" identifier)
                   (:alias "for" identifier)
                   (:alias "loop" identifier)
                   (:alias "while" identifier)
                   (:alias "if" identifier)
                   (:alias "else" identifier)
                   (:alias "try" identifier)
                   (:alias "catch" identifier)
                   (:alias "finally" identifier)
                   (:alias "match" identifier)
                   (:alias "in" identifier)
                   (:alias "export" identifier)))
                 (:token (:prec 20 ":"))
                 (:field :value
                  (:choice
                   _item_expression
                   (:alias _unquoted_in_record val_string)
                   (:alias _unquoted_in_record_with_expr val_string)))))
  _record_key (:seq
               (:choice (:token "-") (:token "+"))
               (:token-immediate (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`':,]"))))
  _table_head_separator (:token (:prec 20 (:seq (:pattern "\\s*") ";")))
  _table_head (:seq
               (:choice _repeat_newline :blank)
               (:field :head val_list)
               (:alias _table_head_separator ";"))
  _table_body (:prec 20
               (:seq
                (:repeat _newline)
                (:repeat (:seq (:field :row val_list) (:repeat1 _entry_separator)))
                (:field :row val_list)
                (:repeat _entry_separator)))
  val_table (:seq "[" _table_head (:choice _table_body :blank) "]" (:choice cell_path :blank))
  val_closure (:seq
               "{"
               (:choice _repeat_newline :blank)
               (:choice (:field :parameters parameter_pipes) :blank)
               (:choice _block_body :blank)
               "}")
  cell_path (:repeat1 path)
  _path_suffix (:choice "?" "!" (:seq "?" "!") (:seq "!" "?"))
  path (:seq
        "."
        (:choice
         (:token-immediate (:prec -1 (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}.,:?!]"))))
         val_string)
        (:choice _path_suffix :blank))
  env_var (:seq
           (:field :variable (:alias cmd_identifier identifier))
           (:token-immediate "=")
           (:field :value
            (:choice
             (:alias
              (:token-immediate
               (:seq
                (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\-{}<>=\"`'@?,:.&*!^+#$]")
                (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]\\-{}<>=\"`'@?,:.]"))))
              val_string)
             val_string
             val_interpolated)))
  command (:prec-right 0
           (:seq
            (:choice
             (:field :head (:seq (:choice (:choice "^" "%") :blank) cmd_identifier))
             (:field :head (:seq "^" _stringish)))
            (:repeat (:seq _space (:choice _cmd_arg :blank)))))
  _command_parenthesized (:prec-right 0
                          (:seq
                           (:choice
                            (:field :head (:seq (:choice (:choice "^" "%") :blank) cmd_identifier))
                            (:field :head (:seq "^" _stringish)))
                           (:repeat (:seq _separator (:choice _cmd_arg :blank)))))
  _cmd_arg (:choice
            (:field :redir (:prec-right 10 redirection))
            (:field :flag (:prec-right 9 _flag))
            (:field :arg (:prec-right 8 _value))
            (:field :arg (:prec-right 8 val_range))
            (:field :arg (:prec-right 7 expr_parenthesized))
            (:field :arg_spread _spread_listish)
            (:field :arg_str (:alias unquoted val_string))
            (:field :arg_str (:alias _unquoted_with_expr val_string)))
  flag_value (:choice _value val_string)
  redirection (:seq
               (:choice
                "err>"
                "out>"
                "e>"
                "o>"
                "err+out>"
                "out+err>"
                "o+e>"
                "e+o>"
                "err>>"
                "out>>"
                "e>>"
                "o>>"
                "err+out>>"
                "out+err>>"
                "o+e>>"
                "e+o>>")
               _space
               (:field :file_path (:choice (:alias _unquoted_naive val_string) _stringish)))
  _flag (:choice short_flag long_flag)
  _flags_parenthesized (:seq (:repeat1 _separator) _flag)
  _flag_value (:choice
               _value
               (:alias unquoted val_string)
               (:alias _expr_parenthesized_immediate expr_parenthesized))
  _flag_equals_value (:seq (:token-immediate "=") (:field :value _flag_value))
  short_flag (:seq
              "-"
              (:choice (:field :name short_flag_identifier) :blank)
              (:choice _flag_equals_value :blank))
  short_flag_identifier (:token-immediate (:pattern "[\\p{XID_Continue}:?@!%_-]+" "u"))
  long_flag (:seq
             "--"
             (:choice (:field :name long_flag_identifier) :blank)
             (:choice _flag_equals_value :blank))
  _unquoted_pattern (:token-immediate
                     (:prec -69 (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`']"))))
  _unquoted_pattern_in_list (:token-immediate
                             (:prec -69 (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`',]"))))
  _unquoted_pattern_in_record (:token-immediate
                               (:prec -69
                                (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`':,>]"))))
  _unquoted_naive (:token (:prec -1 (:repeat1 (:pattern "[^\\s\\r\\n\\t\\|();{}]"))))
  unquoted (:prec-left -69
            (:choice
             (:token
              (:prec -69
               (:token
                (:token
                 (:seq
                  (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`'$-]")
                  (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`']")))))))
             (:seq
              (:choice
               _val_range
               _val_number_decimal
               (:pattern "[iI][nN][fF]([iI][nN][iI][tT][yY])?")
               (:pattern "-[iI][nN][fF]([iI][nN][iI][tT][yY])?")
               (:pattern "[nN][aA][nN]"))
              _unquoted_pattern)
             (:seq
              (:choice ".." "..=" "..<")
              (:token-immediate
               (:prec -69 (:token (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`']"))))))
             (:seq _unquoted_anonymous_prefix _unquoted_pattern)))
  _unquoted_in_list (:prec-left -69
                     (:choice
                      (:token
                       (:prec -69
                        (:token
                         (:token
                          (:seq
                           (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`'$,]")
                           (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`',]")))))))
                      (:seq
                       (:choice
                        _val_range
                        _val_number_decimal
                        (:pattern "[iI][nN][fF]([iI][nN][iI][tT][yY])?")
                        (:pattern "-[iI][nN][fF]([iI][nN][iI][tT][yY])?")
                        (:pattern "[nN][aA][nN]"))
                       _unquoted_pattern_in_list)
                      (:seq
                       (:choice ".." "..=" "..<")
                       (:token-immediate
                        (:prec -69
                         (:token (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`',]"))))))
                      (:seq _unquoted_anonymous_prefix _unquoted_pattern_in_list)))
  _unquoted_in_record (:prec-left -69
                       (:choice
                        (:token
                         (:prec -69
                          (:token
                           (:token
                            (:seq
                             (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`'$:,>]")
                             (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`':,>]")))))))
                        (:seq
                         (:choice
                          _val_range
                          _val_number_decimal
                          (:pattern "[iI][nN][fF]([iI][nN][iI][tT][yY])?")
                          (:pattern "-[iI][nN][fF]([iI][nN][iI][tT][yY])?")
                          (:pattern "[nN][aA][nN]"))
                         _unquoted_pattern_in_record)
                        (:seq
                         (:choice ".." "..=" "..<")
                         (:token-immediate
                          (:prec -69
                           (:token (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\]{}\"`':,>]"))))))
                        (:seq _unquoted_anonymous_prefix _unquoted_pattern_in_record)))
  _unquoted_with_expr (:prec -1
                       (:seq
                        (:choice
                         _unquoted_anonymous_prefix
                         _val_number_decimal
                         _val_range
                         (:alias unquoted "_head"))
                        (:alias _expr_parenthesized_immediate expr_parenthesized)
                        (:repeat
                         (:seq
                          (:token-immediate (:repeat (:pattern "[^\\s\\r\\n\\t\\|();]")))
                          (:alias _expr_parenthesized_immediate expr_parenthesized)))
                        (:token-immediate (:repeat (:pattern "[^\\s\\r\\n\\t\\|();]")))))
  _unquoted_in_list_with_expr (:prec -1
                               (:seq
                                (:choice
                                 _unquoted_anonymous_prefix
                                 _val_number_decimal
                                 _val_range
                                 (:alias _unquoted_in_list "_head"))
                                (:alias _expr_parenthesized_immediate expr_parenthesized)
                                (:repeat
                                 (:seq
                                  (:token-immediate
                                   (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\],]")))
                                  (:alias _expr_parenthesized_immediate expr_parenthesized)))
                                (:token-immediate
                                 (:repeat (:pattern "[^\\s\\r\\n\\t\\|();\\[\\],]")))))
  _unquoted_in_record_with_expr (:prec -1
                                 (:seq
                                  (:choice
                                   _unquoted_anonymous_prefix
                                   _val_number_decimal
                                   _val_range
                                   (:alias _unquoted_in_record "_head"))
                                  (:alias _expr_parenthesized_immediate expr_parenthesized)
                                  (:repeat
                                   (:seq
                                    (:token-immediate
                                     (:repeat (:pattern "[^\\s\\r\\n\\t\\|();{}:,]")))
                                    (:alias _expr_parenthesized_immediate expr_parenthesized)))
                                  (:token-immediate
                                   (:repeat (:pattern "[^\\s\\r\\n\\t\\|();{}:,]")))))
  _unquoted_anonymous_prefix (:choice
                              "null"
                              (:alias val_bool "_prefix")
                              (:alias val_date "_prefix")
                              (:seq
                               _val_number_decimal
                               (:choice
                                (:alias duration_unit "_unit")
                                (:alias filesize_unit "_unit"))))
  comment (:seq "#" (:pattern ".*"))}}
