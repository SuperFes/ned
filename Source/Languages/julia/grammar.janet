# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "julia"
 :word _word_identifier
 :extras [(:pattern "\\s") line_comment block_comment]
 :conflicts [[juxtaposition_expression _primary_expression]
             [juxtaposition_expression _expression]
             [matrix_row comprehension_expression]
             [parenthesized_expression tuple_expression]]
 :precedences []
 :externals [_block_comment_rest
             _immediate_paren
             _immediate_bracket
             _immediate_brace
             _immediate_string_start
             _immediate_command_start
             _content_cmd_1
             _content_cmd_1_raw
             _content_cmd_3
             _content_cmd_3_raw
             _content_str_1
             _content_str_1_raw
             _content_str_3
             _content_str_3_raw
             _end_cmd
             _end_str]
 :inline [_block_form _terminator _definition _statement _operation]
 :supertypes [_expression _statement _definition]
 :rules
 {source_file (:choice
               (:seq
                (:seq _block_form (:repeat (:seq _terminator _block_form)))
                (:choice _terminator :blank))
               :blank)
  block (:seq
         (:seq _block_form (:repeat (:seq _terminator _block_form)))
         (:choice _terminator :blank))
  _block_form (:choice _expression assignment open_tuple)
  _bracket_form (:choice _expression (:alias _closed_assignment assignment))
  open_tuple (:prec -1 (:seq _expression (:repeat1 (:seq "," _expression))))
  assignment (:prec-right -2
              (:seq
               (:choice _primary_expression open_tuple _operation operator)
               (:alias "=" operator)
               _block_form))
  _closed_assignment (:prec-right -2
                      (:seq
                       (:choice _primary_expression _operation operator)
                       (:alias "=" operator)
                       _bracket_form))
  _expression (:choice
               _definition
               _statement
               _primary_expression
               _operation
               compound_assignment_expression
               macrocall_expression
               arrow_function_expression
               juxtaposition_expression
               ternary_expression
               operator
               integer_literal
               float_literal
               (:prec -1 (:alias "begin" identifier)))
  _definition (:choice
               module_definition
               abstract_definition
               primitive_definition
               struct_definition
               function_definition
               macro_definition)
  module_definition (:seq
                     (:choice "module" "baremodule")
                     (:field :name (:choice identifier interpolation_expression))
                     (:choice _terminator :blank)
                     (:choice block :blank)
                     "end")
  type_head (:prec -3 (:choice _primary_expression binary_expression))
  abstract_definition (:seq "abstract" "type" type_head "end")
  primitive_definition (:seq "primitive" "type" type_head integer_literal "end")
  struct_definition (:seq
                     (:choice "mutable" :blank)
                     "struct"
                     type_head
                     (:choice _terminator :blank)
                     (:choice block :blank)
                     "end")
  signature (:prec -3
             (:choice
              identifier
              call_expression
              (:alias tuple_expression argument_list)
              typed_expression
              where_expression))
  function_definition (:seq
                       "function"
                       signature
                       (:choice _terminator :blank)
                       (:choice block :blank)
                       "end")
  macro_definition (:seq
                    "macro"
                    signature
                    (:choice _terminator :blank)
                    (:choice block :blank)
                    "end")
  _statement (:choice
              compound_statement
              quote_statement
              let_statement
              if_statement
              try_statement
              for_statement
              while_statement
              break_statement
              continue_statement
              return_statement
              const_statement
              global_statement
              local_statement
              export_statement
              import_statement
              public_statement
              using_statement)
  compound_statement (:seq "begin" (:choice _terminator :blank) (:choice block :blank) "end")
  quote_statement (:seq "quote" (:choice _terminator :blank) (:choice block :blank) "end")
  let_statement (:seq
                 "let"
                 (:choice (:seq _bracket_form (:repeat (:seq "," _bracket_form))) :blank)
                 _terminator
                 (:choice block :blank)
                 "end")
  if_statement (:seq
                "if"
                (:field :condition _expression)
                (:choice _terminator :blank)
                (:choice block :blank)
                (:field :alternative (:repeat elseif_clause))
                (:field :alternative (:choice else_clause :blank))
                "end")
  elseif_clause (:seq
                 "elseif"
                 (:field :condition _expression)
                 (:choice _terminator :blank)
                 (:choice block :blank))
  else_clause (:seq "else" (:choice _terminator :blank) (:choice block :blank))
  try_statement (:seq
                 "try"
                 (:choice _terminator :blank)
                 (:choice block :blank)
                 (:choice
                  (:seq catch_clause (:choice else_clause :blank) (:choice finally_clause :blank))
                  (:seq finally_clause (:choice catch_clause :blank)))
                 "end")
  catch_clause (:prec 1
                (:seq
                 "catch"
                 (:choice identifier :blank)
                 (:choice _terminator :blank)
                 (:choice block :blank)))
  finally_clause (:seq "finally" (:choice _terminator :blank) (:choice block :blank))
  for_statement (:seq
                 "for"
                 (:seq for_binding (:repeat (:seq "," for_binding)))
                 (:choice _terminator :blank)
                 (:choice block :blank)
                 "end")
  while_statement (:seq
                   "while"
                   (:field :condition _expression)
                   (:choice _terminator :blank)
                   (:choice block :blank)
                   "end")
  break_statement "break"
  continue_statement "continue"
  return_statement (:prec-right -3 (:seq "return" (:choice _block_form :blank)))
  const_statement (:prec-right -3 (:seq "const" _block_form))
  global_statement (:prec-right -3 (:seq "global" _block_form))
  local_statement (:prec-right -3 (:seq "local" _block_form))
  import_alias (:seq _importable "as" _exportable)
  import_path (:seq (:token (:repeat1 ".")) (:choice identifier _scoped_identifier))
  _exportable (:choice
               identifier
               macro_identifier
               operator
               interpolation_expression
               (:seq "(" _exportable ")"))
  _importable (:choice _exportable (:alias _scoped_identifier import_path) import_path)
  _import_list (:prec-right 0
                (:seq
                 (:choice _importable import_alias)
                 (:repeat (:seq "," (:choice _importable import_alias)))))
  selected_import (:seq _importable (:token-immediate ":") _import_list)
  export_statement (:seq
                    "export"
                    (:prec-right 0 (:seq _exportable (:repeat (:seq "," _exportable)))))
  public_statement (:seq
                    "public"
                    (:prec-right 0 (:seq _exportable (:repeat (:seq "," _exportable)))))
  import_statement (:seq "import" (:choice _import_list selected_import))
  using_statement (:seq "using" (:choice _import_list selected_import))
  _primary_expression (:choice
                       identifier
                       boolean_literal
                       curly_expression
                       parenthesized_expression
                       tuple_expression
                       _array
                       _string
                       adjoint_expression
                       broadcast_call_expression
                       call_expression
                       (:alias _closed_macrocall_expression macrocall_expression)
                       parametrized_type_expression
                       field_expression
                       index_expression
                       interpolation_expression
                       quote_expression)
  _array (:choice comprehension_expression matrix_expression vector_expression)
  comprehension_expression (:prec -1
                            (:seq
                             "["
                             _bracket_form
                             (:choice _terminator :blank)
                             for_clause
                             (:repeat (:choice for_clause if_clause))
                             "]"))
  generator (:seq _bracket_form for_clause (:repeat (:choice for_clause if_clause)))
  if_clause (:seq "if" _expression)
  for_clause (:prec-right 0 (:seq "for" (:seq for_binding (:repeat (:seq "," for_binding)))))
  for_binding (:prec 1
               (:seq
                (:choice "outer" :blank)
                (:choice identifier tuple_expression typed_expression interpolation_expression)
                (:alias (:choice "in" "=" "∈") operator)
                _expression))
  matrix_expression (:prec -1
                     (:seq
                      "["
                      matrix_row
                      (:repeat (:seq _terminator matrix_row))
                      (:choice _terminator :blank)
                      "]"))
  matrix_row (:repeat1 (:prec -1 _bracket_form))
  vector_expression (:seq
                     "["
                     (:choice (:seq _bracket_form (:repeat (:seq "," _bracket_form))) :blank)
                     (:choice "," :blank)
                     "]")
  parenthesized_expression (:prec-dynamic 1
                            (:seq
                             "("
                             (:seq
                              (:choice _bracket_form generator)
                              (:repeat (:seq _semicolon (:choice _bracket_form generator))))
                             (:choice _semicolon :blank)
                             ")"))
  tuple_expression (:seq
                    "("
                    (:choice _semicolon :blank)
                    (:choice
                     (:seq
                      (:choice _bracket_form generator)
                      (:repeat (:seq (:choice "," _semicolon) (:choice _bracket_form generator))))
                     :blank)
                    (:choice "," :blank)
                    ")")
  curly_expression (:seq
                    "{"
                    (:choice (:seq _bracket_form (:repeat (:seq "," _bracket_form))) :blank)
                    (:choice "," :blank)
                    "}")
  adjoint_expression (:seq _primary_expression (:token-immediate "'"))
  field_expression (:prec 28
                    (:seq
                     (:field :value _primary_expression)
                     (:token-immediate ".")
                     (:choice identifier interpolation_expression quote_expression _string)))
  index_expression (:seq _primary_expression _immediate_bracket _array)
  parametrized_type_expression (:seq _primary_expression _immediate_brace curly_expression)
  call_expression (:seq
                   (:choice _primary_expression operator)
                   _immediate_paren
                   (:alias tuple_expression argument_list)
                   (:choice do_clause :blank))
  broadcast_call_expression (:seq
                             _primary_expression
                             (:token-immediate ".")
                             _immediate_paren
                             (:alias tuple_expression argument_list)
                             (:choice do_clause :blank))
  _qualified_macro_identifier (:seq _primary_expression (:token-immediate ".") macro_identifier)
  _macro_head (:choice (:alias _qualified_macro_identifier field_expression) macro_identifier)
  _closed_macrocall_expression (:seq
                                _macro_head
                                (:choice
                                 (:seq _immediate_brace curly_expression)
                                 (:seq _immediate_bracket _array)
                                 (:seq
                                  _immediate_paren
                                  (:alias tuple_expression argument_list)
                                  (:choice do_clause :blank))))
  macrocall_expression (:prec-right 0 (:seq _macro_head (:choice macro_argument_list :blank)))
  macro_argument_list (:prec-left 0 (:repeat1 (:prec -4 _block_form)))
  do_clause (:seq
             "do"
             (:choice (:seq _bracket_form (:repeat (:seq "," _bracket_form))) :blank)
             _terminator
             (:choice block :blank)
             "end")
  interpolation_expression (:prec-right 25
                            (:seq
                             "$"
                             (:choice
                              integer_literal
                              float_literal
                              identifier
                              curly_expression
                              parenthesized_expression
                              tuple_expression
                              _array
                              _string)))
  quote_expression (:prec-right 25
                    (:seq
                     ":"
                     (:choice
                      integer_literal
                      float_literal
                      _string
                      identifier
                      operator
                      (:seq _immediate_brace curly_expression)
                      (:seq _immediate_bracket _array)
                      (:seq
                       _immediate_paren
                       (:choice
                        parenthesized_expression
                        tuple_expression
                        (:seq
                         "("
                         (:alias
                          (:choice
                           "::"
                           ":="
                           ".="
                           "="
                           _assignment_operator
                           _lazy_or_operator
                           _lazy_and_operator
                           _syntactic_operator)
                          operator)
                         ")")))
                      (:alias
                       (:choice
                        _assignment_operator
                        _lazy_or_operator
                        _lazy_and_operator
                        _syntactic_operator)
                       operator)
                      (:alias
                       (:token-immediate
                        (:choice
                         "baremodule"
                         "module"
                         "abstract"
                         "primitive"
                         "mutable"
                         "struct"
                         "quote"
                         "let"
                         "if"
                         "else"
                         "elseif"
                         "try"
                         "catch"
                         "finally"
                         "for"
                         "while"
                         "break"
                         "continue"
                         "using"
                         "import"
                         "const"
                         "global"
                         "local"
                         "end"))
                       identifier))))
  _operation (:choice
              unary_expression
              binary_expression
              range_expression
              splat_expression
              typed_expression
              unary_typed_expression
              where_expression)
  binary_expression (:choice
                     (:prec-right 11
                      (:seq _expression (:alias _pair_operator operator) _expression))
                     (:prec-right 13
                      (:seq _expression (:alias _arrow_operator operator) _expression))
                     (:prec-left 14
                      (:seq _expression (:alias _lazy_or_operator operator) _expression))
                     (:prec-left 15
                      (:seq _expression (:alias _lazy_and_operator operator) _expression))
                     (:prec-left 17
                      (:seq
                       _expression
                       (:alias
                        (:choice "in" "isa" _comparison_operator _type_order_operator)
                        operator)
                       _expression))
                     (:prec-right 18
                      (:seq _expression (:alias _pipe_left_operator operator) _expression))
                     (:prec-left 19
                      (:seq _expression (:alias _pipe_right_operator operator) _expression))
                     (:prec-left 20
                      (:seq _expression (:alias _ellipsis_operator operator) _expression))
                     (:prec-left 21
                      (:seq
                       _expression
                       (:alias (:choice _unary_plus_operator _plus_operator) operator)
                       _expression))
                     (:prec-left 22
                      (:seq _expression (:alias _times_operator operator) _expression))
                     (:prec-left 23
                      (:seq _expression (:alias _rational_operator operator) _expression))
                     (:prec-left 24
                      (:seq _expression (:alias _bitshift_operator operator) _expression))
                     (:prec-left 26
                      (:seq _expression (:alias _power_operator operator) _expression)))
  unary_expression (:prec-right 25
                    (:seq
                     (:alias
                      (:choice
                       _tilde_operator
                       _type_order_operator
                       _unary_operator
                       _unary_plus_operator)
                      operator)
                     _expression))
  range_expression (:prec-left 20 (:seq _expression (:token-immediate ":") _expression))
  splat_expression (:prec 20 (:seq _expression "..."))
  ternary_expression (:prec-right 12 (:seq _expression "?" _bracket_form ":" _bracket_form))
  typed_expression (:prec 27 (:seq _expression "::" _primary_expression))
  unary_typed_expression (:prec-right 25 (:seq "::" _primary_expression))
  arrow_function_expression (:prec-right 10
                             (:seq
                              (:choice
                               identifier
                               (:alias tuple_expression argument_list)
                               typed_expression)
                              "->"
                              _bracket_form))
  juxtaposition_expression (:prec-left 0
                            (:seq
                             (:choice integer_literal float_literal adjoint_expression)
                             _primary_expression))
  compound_assignment_expression (:prec-right -2
                                  (:seq
                                   _primary_expression
                                   (:alias (:choice _assignment_operator _tilde_operator) operator)
                                   _expression))
  where_expression (:prec-left 16 (:seq _expression "where" _expression))
  macro_identifier (:seq
                    "@"
                    (:choice
                     identifier
                     operator
                     (:alias _syntactic_operator operator)
                     (:alias _scoped_identifier field_expression)))
  _scoped_identifier (:seq
                      (:choice identifier interpolation_expression)
                      (:repeat1
                       (:seq (:token-immediate ".") (:choice identifier interpolation_expression))))
  _word_identifier (:pattern "[_\\p{XID_Start}°∀-∇∎-∑∫-∳&&[^0-9#*]][^\"'`\\s\\.\\-\\[\\]#$,:;@~(){}+==*=/=//=\\\\=^=%=<<=>>=>>>=|=&=−=÷=⊻=≔⩴≕<><>←→↔↚↛↞↠↢↣↦↤↮⇎⇍⇏⇐⇒⇔⇴⇶⇷⇸⇹⇺⇻⇼⇽⇾⇿⟵⟶⟷⟹⟺⟻⟼⟽⟾⟿⤀⤁⤂⤃⤄⤅⤆⤇⤌⤍⤎⤏⤐⤑⤔⤕⤖⤗⤘⤝⤞⤟⤠⥄⥅⥆⥇⥈⥊⥋⥎⥐⥒⥓⥖⥗⥚⥛⥞⥟⥢⥤⥦⥧⥨⥩⥪⥫⥬⥭⥰⧴⬱⬰⬲⬳⬴⬵⬶⬷⬸⬹⬺⬻⬼⬽⬾⬿⭀⭁⭂⭃⥷⭄⥺⭇⭈⭉⭊⭋⭌￩￫⇜⇝↜↝↩↪↫↬↼↽⇀⇁⇄⇆⇇⇉⇋⇌⇚⇛⇠⇢↷↶↺↻><>=<=========≥≤≡≠≢∈∉∋∌⊆⊈⊂⊄⊊∝∊∍∥∦∷∺∻∽∾≁≃≂≄≅≆≇≈≉≊≋≌≍≎≐≑≒≓≖≗≘≙≚≛≜≝≞≟≣≦≧≨≩≪≫≬≭≮≯≰≱≲≳≴≵≶≷≸≹≺≻≼≽≾≿⊀⊁⊃⊅⊇⊉⊋⊏⊐⊑⊒⊜⊩⊬⊮⊰⊱⊲⊳⊴⊵⊶⊷⋍⋐⋑⋕⋖⋗⋘⋙⋚⋛⋜⋝⋞⋟⋠⋡⋢⋣⋤⋥⋦⋧⋨⋩⋪⋫⋬⋭⋲⋳⋴⋵⋶⋷⋸⋹⋺⋻⋼⋽⋾⋿⟈⟉⟒⦷⧀⧁⧡⧣⧤⧥⩦⩧⩪⩫⩬⩭⩮⩯⩰⩱⩲⩳⩵⩶⩷⩸⩹⩺⩻⩼⩽⩾⩿⪀⪁⪂⪃⪄⪅⪆⪇⪈⪉⪊⪋⪌⪍⪎⪏⪐⪑⪒⪓⪔⪕⪖⪗⪘⪙⪚⪛⪜⪝⪞⪟⪠⪡⪢⪣⪤⪥⪦⪧⪨⪩⪪⪫⪬⪭⪮⪯⪰⪱⪲⪳⪴⪵⪶⪷⪸⪹⪺⪻⪼⪽⪾⪿⫀⫁⫂⫃⫄⫅⫆⫇⫈⫉⫊⫋⫌⫍⫎⫏⫐⫑⫒⫓⫔⫕⫖⫗⫘⫙⫷⫸⫹⫺⊢⊣⟂⫪⫫…⁝⋮⋱⋰⋯++|−¦⊕⊖⊞⊟∪∨⊔∔∸≏⊎⊻⊽⋎⋓⟇⧺⧻⨈⨢⨣⨤⨥⨦⨧⨨⨩⨪⨫⨬⨭⨮⨹⨺⩁⩂⩅⩊⩌⩏⩐⩒⩔⩖⩗⩛⩝⩡⩢⩣*/%&\\\\⌿÷··⋅∘×∩∧⊗⊘⊙⊚⊛⊠⊡⊓∗∙∤⅋≀⊼⋄⋆⋇⋉⋊⋋⋌⋏⋒⟑⦸⦼⦾⦿⧶⧷⨇⨰⨱⨲⨳⨴⨵⨶⨷⨸⨻⨼⨽⩀⩃⩄⩋⩍⩎⩑⩓⩕⩘⩚⩜⩞⩟⩠⫛⊍▷⨝⟕⟖⟗⨟<<>>>>>^↑↓⇵⟰⟱⤈⤉⤊⤋⤒⤓⥉⥌⥍⥏⥑⥔⥕⥘⥙⥜⥝⥠⥡⥣⥥⥮⥯￪￬¬√∛∜+±∓]*")
  identifier _word_identifier
  boolean_literal (:choice "true" "false")
  integer_literal (:choice
                   (:token (:seq "0b" (:pattern "[01]([01]|_[01])*")))
                   (:token (:seq "0o" (:pattern "[0-7]([0-7]|_[0-7])*")))
                   (:token (:seq "0x" (:pattern "[0-9a-fA-F]([0-9a-fA-F]|_[0-9a-fA-F])*")))
                   (:pattern "[0-9]([0-9]|_[0-9])*"))
  float_literal (:choice
                 (:token
                  (:seq
                   "."
                   (:pattern "[0-9]([0-9]|_[0-9])*")
                   (:choice (:pattern "[eEf][+-]?\\d+") :blank)))
                 (:seq
                  (:pattern "[0-9]([0-9]|_[0-9])*")
                  (:token-immediate
                   (:seq
                    "."
                    (:choice (:pattern "[0-9]([0-9]|_[0-9])*") :blank)
                    (:choice (:pattern "[eEf][+-]?\\d+") :blank))))
                 (:token (:seq (:pattern "[0-9]([0-9]|_[0-9])*") (:pattern "[eEf][+-]?\\d+")))
                 (:token
                  (:seq
                   (:choice
                    (:seq
                     "0x"
                     (:pattern "[0-9a-fA-F]([0-9a-fA-F]|_[0-9a-fA-F])*")
                     (:choice "." :blank)
                     (:choice (:pattern "[0-9a-fA-F]([0-9a-fA-F]|_[0-9a-fA-F])*") :blank))
                    (:seq "0x." (:pattern "[0-9a-fA-F]([0-9a-fA-F]|_[0-9a-fA-F])*")))
                   (:pattern "p[+-]?\\d+"))))
  _string (:choice
           character_literal
           string_literal
           command_literal
           prefixed_string_literal
           prefixed_command_literal)
  escape_sequence (:token
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^uUx0-7]")
                     (:pattern "[uU][0-9a-fA-F]{1,6}")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "x[0-9a-fA-F]{2}"))))
  character_literal (:token
                     (:seq
                      "'"
                      (:choice
                       (:pattern "[^'\\\\]")
                       (:token
                        (:seq
                         "\\"
                         (:choice
                          (:pattern "[^uUx0-7]")
                          (:pattern "[uU][0-9a-fA-F]{1,6}")
                          (:pattern "[0-7]{1,3}")
                          (:pattern "x[0-9a-fA-F]{2}")))))
                      "'"))
  _delimiter_str_1 "\""
  _delimiter_str_3 "\"\"\""
  _delimiter_cmd_1 "`"
  _delimiter_cmd_3 "```"
  string_literal (:choice
                  (:seq
                   _delimiter_str_1
                   (:repeat
                    (:choice (:alias _content_str_1 content) string_interpolation escape_sequence))
                   _end_str)
                  (:seq
                   _delimiter_str_3
                   (:repeat
                    (:choice (:alias _content_str_3 content) string_interpolation escape_sequence))
                   _end_str))
  command_literal (:choice
                   (:seq
                    _delimiter_cmd_1
                    (:repeat
                     (:choice (:alias _content_cmd_1 content) string_interpolation escape_sequence))
                    _end_cmd)
                   (:seq
                    _delimiter_cmd_3
                    (:repeat
                     (:choice (:alias _content_cmd_3 content) string_interpolation escape_sequence))
                    _end_cmd))
  prefixed_string_literal (:prec-left 0
                           (:seq
                            (:field :prefix identifier)
                            _immediate_string_start
                            (:choice
                             (:seq
                              _delimiter_str_1
                              (:repeat
                               (:choice (:alias _content_str_1_raw content) escape_sequence))
                              _end_str)
                             (:seq
                              _delimiter_str_3
                              (:repeat
                               (:choice (:alias _content_str_3_raw content) escape_sequence))
                              _end_str))
                            (:choice (:field :suffix identifier) :blank)))
  prefixed_command_literal (:prec-left 0
                            (:seq
                             (:field :prefix identifier)
                             _immediate_command_start
                             (:choice
                              (:seq
                               _delimiter_cmd_1
                               (:repeat
                                (:choice (:alias _content_cmd_1_raw content) escape_sequence))
                               _end_cmd)
                              (:seq
                               _delimiter_cmd_3
                               (:repeat
                                (:choice (:alias _content_cmd_3_raw content) escape_sequence))
                               _end_cmd))
                             (:choice (:field :suffix identifier) :blank)))
  string_interpolation (:seq
                        "$"
                        (:choice identifier (:seq _immediate_paren (:seq "(" _bracket_form ")"))))
  operator (:choice
            _pair_operator
            _arrow_operator
            _comparison_operator
            _pipe_left_operator
            _pipe_right_operator
            _ellipsis_operator
            ":"
            _plus_operator
            _times_operator
            _rational_operator
            _bitshift_operator
            _power_operator
            _tilde_operator
            _type_order_operator
            _unary_operator
            _unary_plus_operator)
  _assignment_operator (:choice
                        ":="
                        "$="
                        ".="
                        (:token
                         (:seq
                          (:choice "." :blank)
                          (:choice
                           "+="
                           "-="
                           "*="
                           "/="
                           "//="
                           "\\="
                           "^="
                           "%="
                           "<<="
                           ">>="
                           ">>>="
                           "|="
                           "&="
                           "−="
                           "÷="
                           "⊻="
                           "≔"
                           "⩴"
                           "≕"))))
  _pair_operator (:token (:seq (:choice "." :blank) "=>"))
  _arrow_operator (:token
                   (:seq
                    (:choice "." :blank)
                    (:choice
                     "<--"
                     "-->"
                     "<-->"
                     "←"
                     "→"
                     "↔"
                     "↚"
                     "↛"
                     "↞"
                     "↠"
                     "↢"
                     "↣"
                     "↦"
                     "↤"
                     "↮"
                     "⇎"
                     "⇍"
                     "⇏"
                     "⇐"
                     "⇒"
                     "⇔"
                     "⇴"
                     "⇶"
                     "⇷"
                     "⇸"
                     "⇹"
                     "⇺"
                     "⇻"
                     "⇼"
                     "⇽"
                     "⇾"
                     "⇿"
                     "⟵"
                     "⟶"
                     "⟷"
                     "⟹"
                     "⟺"
                     "⟻"
                     "⟼"
                     "⟽"
                     "⟾"
                     "⟿"
                     "⤀"
                     "⤁"
                     "⤂"
                     "⤃"
                     "⤄"
                     "⤅"
                     "⤆"
                     "⤇"
                     "⤌"
                     "⤍"
                     "⤎"
                     "⤏"
                     "⤐"
                     "⤑"
                     "⤔"
                     "⤕"
                     "⤖"
                     "⤗"
                     "⤘"
                     "⤝"
                     "⤞"
                     "⤟"
                     "⤠"
                     "⥄"
                     "⥅"
                     "⥆"
                     "⥇"
                     "⥈"
                     "⥊"
                     "⥋"
                     "⥎"
                     "⥐"
                     "⥒"
                     "⥓"
                     "⥖"
                     "⥗"
                     "⥚"
                     "⥛"
                     "⥞"
                     "⥟"
                     "⥢"
                     "⥤"
                     "⥦"
                     "⥧"
                     "⥨"
                     "⥩"
                     "⥪"
                     "⥫"
                     "⥬"
                     "⥭"
                     "⥰"
                     "⧴"
                     "⬱"
                     "⬰"
                     "⬲"
                     "⬳"
                     "⬴"
                     "⬵"
                     "⬶"
                     "⬷"
                     "⬸"
                     "⬹"
                     "⬺"
                     "⬻"
                     "⬼"
                     "⬽"
                     "⬾"
                     "⬿"
                     "⭀"
                     "⭁"
                     "⭂"
                     "⭃"
                     "⥷"
                     "⭄"
                     "⥺"
                     "⭇"
                     "⭈"
                     "⭉"
                     "⭊"
                     "⭋"
                     "⭌"
                     "￩"
                     "￫"
                     "⇜"
                     "⇝"
                     "↜"
                     "↝"
                     "↩"
                     "↪"
                     "↫"
                     "↬"
                     "↼"
                     "↽"
                     "⇀"
                     "⇁"
                     "⇄"
                     "⇆"
                     "⇇"
                     "⇉"
                     "⇋"
                     "⇌"
                     "⇚"
                     "⇛"
                     "⇠"
                     "⇢"
                     "↷"
                     "↶"
                     "↺"
                     "↻")))
  _lazy_or_operator (:token (:seq (:choice "." :blank) "||"))
  _lazy_and_operator (:token (:seq (:choice "." :blank) "&&"))
  _comparison_operator (:token
                        (:seq
                         (:choice "." :blank)
                         (:choice
                          ">"
                          "<"
                          ">="
                          "<="
                          "=="
                          "==="
                          "!="
                          "!=="
                          "≥"
                          "≤"
                          "≡"
                          "≠"
                          "≢"
                          "∈"
                          "∉"
                          "∋"
                          "∌"
                          "⊆"
                          "⊈"
                          "⊂"
                          "⊄"
                          "⊊"
                          "∝"
                          "∊"
                          "∍"
                          "∥"
                          "∦"
                          "∷"
                          "∺"
                          "∻"
                          "∽"
                          "∾"
                          "≁"
                          "≃"
                          "≂"
                          "≄"
                          "≅"
                          "≆"
                          "≇"
                          "≈"
                          "≉"
                          "≊"
                          "≋"
                          "≌"
                          "≍"
                          "≎"
                          "≐"
                          "≑"
                          "≒"
                          "≓"
                          "≖"
                          "≗"
                          "≘"
                          "≙"
                          "≚"
                          "≛"
                          "≜"
                          "≝"
                          "≞"
                          "≟"
                          "≣"
                          "≦"
                          "≧"
                          "≨"
                          "≩"
                          "≪"
                          "≫"
                          "≬"
                          "≭"
                          "≮"
                          "≯"
                          "≰"
                          "≱"
                          "≲"
                          "≳"
                          "≴"
                          "≵"
                          "≶"
                          "≷"
                          "≸"
                          "≹"
                          "≺"
                          "≻"
                          "≼"
                          "≽"
                          "≾"
                          "≿"
                          "⊀"
                          "⊁"
                          "⊃"
                          "⊅"
                          "⊇"
                          "⊉"
                          "⊋"
                          "⊏"
                          "⊐"
                          "⊑"
                          "⊒"
                          "⊜"
                          "⊩"
                          "⊬"
                          "⊮"
                          "⊰"
                          "⊱"
                          "⊲"
                          "⊳"
                          "⊴"
                          "⊵"
                          "⊶"
                          "⊷"
                          "⋍"
                          "⋐"
                          "⋑"
                          "⋕"
                          "⋖"
                          "⋗"
                          "⋘"
                          "⋙"
                          "⋚"
                          "⋛"
                          "⋜"
                          "⋝"
                          "⋞"
                          "⋟"
                          "⋠"
                          "⋡"
                          "⋢"
                          "⋣"
                          "⋤"
                          "⋥"
                          "⋦"
                          "⋧"
                          "⋨"
                          "⋩"
                          "⋪"
                          "⋫"
                          "⋬"
                          "⋭"
                          "⋲"
                          "⋳"
                          "⋴"
                          "⋵"
                          "⋶"
                          "⋷"
                          "⋸"
                          "⋹"
                          "⋺"
                          "⋻"
                          "⋼"
                          "⋽"
                          "⋾"
                          "⋿"
                          "⟈"
                          "⟉"
                          "⟒"
                          "⦷"
                          "⧀"
                          "⧁"
                          "⧡"
                          "⧣"
                          "⧤"
                          "⧥"
                          "⩦"
                          "⩧"
                          "⩪"
                          "⩫"
                          "⩬"
                          "⩭"
                          "⩮"
                          "⩯"
                          "⩰"
                          "⩱"
                          "⩲"
                          "⩳"
                          "⩵"
                          "⩶"
                          "⩷"
                          "⩸"
                          "⩹"
                          "⩺"
                          "⩻"
                          "⩼"
                          "⩽"
                          "⩾"
                          "⩿"
                          "⪀"
                          "⪁"
                          "⪂"
                          "⪃"
                          "⪄"
                          "⪅"
                          "⪆"
                          "⪇"
                          "⪈"
                          "⪉"
                          "⪊"
                          "⪋"
                          "⪌"
                          "⪍"
                          "⪎"
                          "⪏"
                          "⪐"
                          "⪑"
                          "⪒"
                          "⪓"
                          "⪔"
                          "⪕"
                          "⪖"
                          "⪗"
                          "⪘"
                          "⪙"
                          "⪚"
                          "⪛"
                          "⪜"
                          "⪝"
                          "⪞"
                          "⪟"
                          "⪠"
                          "⪡"
                          "⪢"
                          "⪣"
                          "⪤"
                          "⪥"
                          "⪦"
                          "⪧"
                          "⪨"
                          "⪩"
                          "⪪"
                          "⪫"
                          "⪬"
                          "⪭"
                          "⪮"
                          "⪯"
                          "⪰"
                          "⪱"
                          "⪲"
                          "⪳"
                          "⪴"
                          "⪵"
                          "⪶"
                          "⪷"
                          "⪸"
                          "⪹"
                          "⪺"
                          "⪻"
                          "⪼"
                          "⪽"
                          "⪾"
                          "⪿"
                          "⫀"
                          "⫁"
                          "⫂"
                          "⫃"
                          "⫄"
                          "⫅"
                          "⫆"
                          "⫇"
                          "⫈"
                          "⫉"
                          "⫊"
                          "⫋"
                          "⫌"
                          "⫍"
                          "⫎"
                          "⫏"
                          "⫐"
                          "⫑"
                          "⫒"
                          "⫓"
                          "⫔"
                          "⫕"
                          "⫖"
                          "⫗"
                          "⫘"
                          "⫙"
                          "⫷"
                          "⫸"
                          "⫹"
                          "⫺"
                          "⊢"
                          "⊣"
                          "⟂"
                          "⫪"
                          "⫫")))
  _pipe_right_operator (:token (:seq (:choice "." :blank) "|>"))
  _pipe_left_operator (:token (:seq (:choice "." :blank) "<|"))
  _ellipsis_operator (:token
                      (:choice
                       ".."
                       (:token
                        (:seq (:choice "." :blank) (:choice "…" "⁝" "⋮" "⋱" "⋰" "⋯")))))
  _plus_operator (:token
                  (:seq
                   (:choice "." :blank)
                   (:choice
                    "++"
                    "|"
                    "−"
                    "¦"
                    "⊕"
                    "⊖"
                    "⊞"
                    "⊟"
                    "∪"
                    "∨"
                    "⊔"
                    "∔"
                    "∸"
                    "≏"
                    "⊎"
                    "⊻"
                    "⊽"
                    "⋎"
                    "⋓"
                    "⟇"
                    "⧺"
                    "⧻"
                    "⨈"
                    "⨢"
                    "⨣"
                    "⨤"
                    "⨥"
                    "⨦"
                    "⨧"
                    "⨨"
                    "⨩"
                    "⨪"
                    "⨫"
                    "⨬"
                    "⨭"
                    "⨮"
                    "⨹"
                    "⨺"
                    "⩁"
                    "⩂"
                    "⩅"
                    "⩊"
                    "⩌"
                    "⩏"
                    "⩐"
                    "⩒"
                    "⩔"
                    "⩖"
                    "⩗"
                    "⩛"
                    "⩝"
                    "⩡"
                    "⩢"
                    "⩣")))
  _times_operator (:token
                   (:seq
                    (:choice "." :blank)
                    (:choice
                     "*"
                     "/"
                     "%"
                     "&"
                     "\\"
                     "⌿"
                     "÷"
                     "·"
                     "·"
                     "⋅"
                     "∘"
                     "×"
                     "∩"
                     "∧"
                     "⊗"
                     "⊘"
                     "⊙"
                     "⊚"
                     "⊛"
                     "⊠"
                     "⊡"
                     "⊓"
                     "∗"
                     "∙"
                     "∤"
                     "⅋"
                     "≀"
                     "⊼"
                     "⋄"
                     "⋆"
                     "⋇"
                     "⋉"
                     "⋊"
                     "⋋"
                     "⋌"
                     "⋏"
                     "⋒"
                     "⟑"
                     "⦸"
                     "⦼"
                     "⦾"
                     "⦿"
                     "⧶"
                     "⧷"
                     "⨇"
                     "⨰"
                     "⨱"
                     "⨲"
                     "⨳"
                     "⨴"
                     "⨵"
                     "⨶"
                     "⨷"
                     "⨸"
                     "⨻"
                     "⨼"
                     "⨽"
                     "⩀"
                     "⩃"
                     "⩄"
                     "⩋"
                     "⩍"
                     "⩎"
                     "⩑"
                     "⩓"
                     "⩕"
                     "⩘"
                     "⩚"
                     "⩜"
                     "⩞"
                     "⩟"
                     "⩠"
                     "⫛"
                     "⊍"
                     "▷"
                     "⨝"
                     "⟕"
                     "⟖"
                     "⟗"
                     "⨟")))
  _rational_operator (:token (:seq (:choice "." :blank) "//"))
  _bitshift_operator (:token (:seq (:choice "." :blank) (:choice "<<" ">>" ">>>")))
  _power_operator (:token
                   (:seq
                    (:choice "." :blank)
                    (:choice
                     "^"
                     "↑"
                     "↓"
                     "⇵"
                     "⟰"
                     "⟱"
                     "⤈"
                     "⤉"
                     "⤊"
                     "⤋"
                     "⤒"
                     "⤓"
                     "⥉"
                     "⥌"
                     "⥍"
                     "⥏"
                     "⥑"
                     "⥔"
                     "⥕"
                     "⥘"
                     "⥙"
                     "⥜"
                     "⥝"
                     "⥠"
                     "⥡"
                     "⥣"
                     "⥥"
                     "⥮"
                     "⥯"
                     "￪"
                     "￬")))
  _tilde_operator (:token (:seq (:choice "." :blank) "~"))
  _type_order_operator (:token (:seq (:choice "." :blank) (:choice "<:" ">:")))
  _unary_operator (:token (:seq (:choice "." :blank) (:choice "!" "¬" "√" "∛" "∜")))
  _unary_plus_operator (:token (:seq (:choice "." :blank) (:choice "+" "-" "±" "∓")))
  _syntactic_operator (:choice "$" "." "..." "->" "?")
  _semicolon (:seq ";" (:repeat (:token-immediate ";")))
  _terminator (:choice (:pattern "\\r?\\n") _semicolon)
  block_comment (:seq (:pattern "#=") _block_comment_rest)
  line_comment (:seq (:pattern "#") (:pattern ".*"))}}
