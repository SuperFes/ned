# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "cue"
 :word identifier
 :extras [comment (:pattern "\\s")]
 :conflicts [[_embedding _label_alias_expr]]
 :precedences []
 :externals [_multi_str_content
             _multi_bytes_content
             _raw_str_content
             _raw_bytes_content
             _multi_raw_str_content
             _multi_raw_bytes_content]
 :inline [keyword_identifier]
 :supertypes [expression primary_expression]
 :rules
 {source_file (:seq
               (:choice attribute :blank)
               (:choice package_clause :blank)
               (:choice (:repeat import_declaration) :blank)
               (:choice
                (:seq
                 _declaration
                 (:repeat (:seq (:choice "," :blank) _declaration))
                 (:choice "," :blank))
                :blank))
  _package_identifier (:alias identifier package_identifier)
  package_clause (:seq "package" _package_identifier)
  import_declaration (:seq "import" (:choice import_spec import_spec_list))
  import_spec (:seq
               (:choice (:field :name (:choice "." "_" _package_identifier)) :blank)
               (:field :path string))
  import_spec_list (:seq
                    "("
                    (:choice
                     (:seq
                      import_spec
                      (:repeat (:seq (:choice "," :blank) import_spec))
                      (:choice "," :blank))
                     :blank)
                    ")")
  identifier (:token
              (:choice
               (:seq
                (:choice (:choice "_#" "#" "_") :blank)
                (:choice (:pattern "\\p{L}") "$" "_")
                (:repeat (:choice (:pattern "\\p{L}") "$" "_" (:pattern "[0-9]"))))
               (:choice "_#" "#" "_")))
  keyword_identifier (:prec -3
                      (:alias
                       (:choice
                        "let"
                        "if"
                        "for"
                        "number"
                        "float"
                        "float32"
                        "float64"
                        "uint"
                        "uint8"
                        "uint16"
                        "uint32"
                        "uint64"
                        "uint128"
                        "int"
                        "int8"
                        "int16"
                        "int32"
                        "int64"
                        "int128"
                        "string"
                        "bytes"
                        "bool")
                       identifier))
  attribute (:seq
             "@"
             identifier
             "("
             (:choice (:choice "," "-") :blank)
             (:repeat
              (:prec-right 0
               (:choice
                (:seq _attr_token (:repeat (:seq "," _attr_token)))
                (:seq "(" (:choice (:seq _attr_token (:repeat (:seq "," _attr_token))) :blank) ")")
                (:seq "[" (:choice (:seq _attr_token (:repeat (:seq "," _attr_token))) :blank) "]")
                (:seq "{" (:choice (:seq _attr_token (:repeat (:seq "," _attr_token))) :blank) "}")
                (:seq "<" (:choice (:seq _attr_token (:repeat (:seq "," _attr_token))) :blank) ">"))))
             ")")
  _attr_token (:prec-right 0
               (:choice
                _attr_item
                (:seq _attr_item "=" (:choice _attr_item attr_path))
                (:seq "(" _attr_item ")" "=" (:choice _attr_item attr_path))))
  _attr_item (:choice
              string
              number
              float
              si_unit
              boolean
              null
              top
              bottom
              primitive_type
              (:seq "[" "]" (:alias _attr_item slice_type))
              (:seq "*" (:alias _attr_item pointer_type))
              package_path
              identifier
              (:prec -1 (:pattern "[^0-9_]")))
  attr_path (:pattern "(?:\\/[\\p{L}\\d\\.]+)*[\\p{L}\\d\\.]+(?:\\/[\\p{L}\\d\\.]+)*(?:\\/)?")
  package_path (:seq identifier (:repeat1 (:seq "." identifier)))
  builtin_function (:choice "len" "close" "and" "or" "div" "mod" "quo" "rem")
  _declaration (:choice field ellipsis _embedding let_clause)
  _list_elem (:prec-right 0
              (:choice
               ellipsis
               (:seq
                _embedding
                (:choice (:repeat (:seq "," _embedding)) :blank)
                (:choice (:seq "," ellipsis) :blank)
                (:choice "," :blank))))
  list_lit (:seq "[" (:repeat _list_elem) "]")
  struct_lit (:seq
              "{"
              (:choice
               (:seq
                (:choice _declaration attribute)
                (:repeat (:seq (:choice "," :blank) (:choice _declaration attribute)))
                (:choice "," :blank))
               :blank)
              "}")
  ellipsis (:prec-left 0 (:seq "..." (:choice expression :blank)))
  _embedding (:choice comprehension _alias_expr)
  _label_name (:prec 1
               (:choice
                identifier
                keyword_identifier
                (:alias _simple_string_lit string)
                selector_expression
                (:alias parenthesized_expression dynamic)))
  _label_alias_expr (:alias _alias_expr optional)
  required (:seq _label_name "!")
  optional (:seq _label_name "?")
  _label_expr (:choice _label_name optional required (:seq "[" _label_alias_expr "]"))
  label (:seq
         (:choice (:seq (:field :alias (:choice identifier keyword_identifier)) "=") :blank)
         _label_expr)
  field (:prec-right 0 (:seq (:repeat1 (:seq label ":")) _value (:choice attribute :blank)))
  _value (:alias _alias_expr value)
  for_clause (:seq
              "for"
              (:choice identifier "_")
              (:choice (:seq "," (:choice identifier "_")) :blank)
              "in"
              expression)
  guard_clause (:seq "if" (:field :condition expression))
  let_clause (:seq "let" (:field :left identifier) "=" (:field :right expression))
  _clause (:choice for_clause guard_clause let_clause)
  comprehension (:seq
                 (:choice for_clause guard_clause)
                 (:repeat (:seq (:choice "," :blank) _clause))
                 struct_lit)
  _alias_expr (:seq (:choice (:seq (:field :alias identifier) "=") :blank) expression)
  parenthesized_expression (:seq "(" expression ")")
  expression (:prec-left 0 (:choice primary_expression unary_expression binary_expression))
  primary_expression (:choice
                      parenthesized_expression
                      selector_expression
                      index_expression
                      call_expression
                      identifier
                      _literal)
  binary_expression (:choice
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "+")
                       (:field :right expression)))
                     (:prec-left 6
                      (:seq
                       (:field :left expression)
                       (:field :operator "-")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "*")
                       (:field :right expression)))
                     (:prec-left 7
                      (:seq
                       (:field :left expression)
                       (:field :operator "/")
                       (:field :right expression)))
                     (:prec-left 1
                      (:seq
                       (:field :left expression)
                       (:field :operator "|")
                       (:field :right expression)))
                     (:prec-left 2
                      (:seq
                       (:field :left expression)
                       (:field :operator "&")
                       (:field :right expression)))
                     (:prec-left 3
                      (:seq
                       (:field :left expression)
                       (:field :operator "||")
                       (:field :right expression)))
                     (:prec-left 4
                      (:seq
                       (:field :left expression)
                       (:field :operator "&&")
                       (:field :right expression)))
                     (:prec-left 5
                      (:seq
                       (:field :left expression)
                       (:field :operator (:choice "==" "=~" "!~" "!=" "<" "<=" ">" ">="))
                       (:field :right expression))))
  unary_expression (:choice
                    (:prec 8 (:seq (:field :operator "+") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator "-") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator "!") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator "*") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator "!=") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator "<") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator "<=") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator ">") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator ">=") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator "=~") (:field :argument expression)))
                    (:prec 8 (:seq (:field :operator "!~") (:field :argument expression))))
  call_expression (:prec 9
                   (:seq
                    (:field :function (:choice builtin_function selector_expression identifier))
                    arguments))
  index_expression (:seq primary_expression "[" expression "]")
  selector_expression (:seq
                       primary_expression
                       "."
                       (:choice identifier (:alias _simple_string_lit string)))
  arguments (:seq
             "("
             (:choice
              (:seq (:choice expression :blank) (:repeat (:seq "," (:choice expression :blank))))
              :blank)
             ")")
  _literal (:choice
            struct_lit
            list_lit
            string
            number
            float
            si_unit
            boolean
            null
            top
            bottom
            primitive_type)
  primitive_type (:choice
                  "number"
                  "float"
                  "float32"
                  "float64"
                  "uint"
                  "uint8"
                  "uint16"
                  "uint32"
                  "uint64"
                  "uint128"
                  "int"
                  "int8"
                  "int16"
                  "int32"
                  "int64"
                  "int128"
                  "string"
                  "bytes"
                  "bool")
  top "_"
  bottom "_|_"
  boolean (:choice "true" "false")
  null "null"
  number (:token
          (:choice
           (:seq (:pattern "0[xX]") (:pattern "[\\da-fA-F](_?[\\da-fA-F])*"))
           (:seq (:pattern "0[bB]") (:pattern "[01](_?[01])*"))
           (:choice
            (:seq
             (:choice (:choice "-" "+") :blank)
             (:choice
              "0"
              (:seq
               (:choice "0" :blank)
               (:pattern "[1-9]")
               (:choice (:seq (:choice "_" :blank) (:pattern "\\d(_?\\d)*")) :blank))))
            (:pattern "\\d(_?\\d)*")
            (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d(_?\\d)*")))
           (:seq (:choice (:choice "-" "+") :blank) (:pattern "0[oO]") (:pattern "[0-7](_?[0-7])*"))))
  float (:token
         (:choice
          (:choice
           (:seq
            (:choice "-" :blank)
            (:choice (:pattern "\\d(_?\\d)*") :blank)
            "."
            (:choice (:pattern "\\d(_?\\d)*") :blank)
            (:choice
             (:seq (:choice "e" "E") (:choice (:choice "+" "-") :blank) (:pattern "\\d(_?\\d)*"))
             :blank))
           (:seq
            (:pattern "\\d(_?\\d)*")
            (:seq (:choice "e" "E") (:choice (:choice "+" "-") :blank) (:pattern "\\d(_?\\d)*"))))
          (:seq
           "0"
           (:choice "x" "X")
           (:choice
            (:seq
             (:choice "_" :blank)
             (:seq
              (:pattern "[0-9a-fA-F]")
              (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))
             "."
             (:choice
              (:seq
               (:pattern "[0-9a-fA-F]")
               (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))
              :blank))
            (:seq
             (:choice "_" :blank)
             (:seq
              (:pattern "[0-9a-fA-F]")
              (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]")))))
            (:seq
             "."
             (:seq
              (:pattern "[0-9a-fA-F]")
              (:repeat (:seq (:choice "_" :blank) (:pattern "[0-9a-fA-F]"))))))
           (:seq (:choice "p" "P") (:choice (:choice "+" "-") :blank) (:pattern "\\d(_?\\d)*")))))
  si_unit (:seq
           float
           (:field :unit
            (:token-immediate (:seq (:choice "K" "M" "G" "T" "P") (:choice "i" :blank)))))
  escape_char (:token-immediate
               (:seq
                "\\"
                (:repeat "#")
                (:seq (:choice "a" "b" "f" "n" "r" "t" "v" "/" "\\" "'" "\""))))
  escape_byte (:token-immediate
               (:seq
                "\\"
                (:repeat "#")
                (:choice (:pattern "[0-7]{3}") (:pattern "x[0-9a-fA-F]{2}"))))
  _escape_unicode (:choice
                   escape_char
                   (:alias
                    (:token-immediate
                     (:seq
                      "\\"
                      (:repeat "#")
                      (:choice (:pattern "u[0-9a-fA-F]{4}") (:pattern "U[0-9a-fA-F]{8}"))))
                    escape_unicode))
  string (:choice
          _simple_string_lit
          _simple_bytes_lit
          _multiline_string_lit
          _multiline_bytes_lit
          _simple_raw_string_lit
          _simple_raw_bytes_lit
          _multiline_raw_string_lit
          _multiline_raw_bytes_lit)
  _simple_string_lit (:seq
                      "\""
                      (:repeat
                       (:choice
                        (:token-immediate (:prec 1 (:pattern "[^\"\\n\\\\]+")))
                        interpolation
                        _escape_unicode))
                      "\"")
  _simple_bytes_lit (:seq
                     "'"
                     (:repeat
                      (:choice
                       (:token-immediate (:prec 1 (:pattern "[^'\\n\\\\]+")))
                       interpolation
                       escape_byte
                       _escape_unicode))
                     "'")
  _multiline_string_lit (:seq
                         (:token "\"\"\"")
                         (:repeat (:choice _multi_str_content interpolation _escape_unicode))
                         (:token "\"\"\""))
  _multiline_bytes_lit (:seq
                        (:token "'''")
                        (:repeat
                         (:choice _multi_bytes_content interpolation escape_byte _escape_unicode))
                        (:token "'''"))
  _simple_raw_string_lit (:seq
                          "#\""
                          (:repeat (:choice _raw_str_content raw_interpolation _escape_unicode))
                          "\"#")
  _simple_raw_bytes_lit (:seq
                         "#'"
                         (:repeat
                          (:choice _raw_bytes_content raw_interpolation escape_byte _escape_unicode))
                         "'#")
  _multiline_raw_string_lit (:seq
                             (:token "#\"\"\"")
                             (:repeat
                              (:choice _multi_raw_str_content raw_interpolation _escape_unicode))
                             (:token "\"\"\"#"))
  _multiline_raw_bytes_lit (:seq
                            (:token "#'''")
                            (:repeat
                             (:choice
                              _multi_raw_bytes_content
                              raw_interpolation
                              escape_byte
                              _escape_unicode))
                            (:token "'''#"))
  interpolation (:seq "\\(" expression ")")
  raw_interpolation (:seq "\\#(" expression ")")
  comment (:token (:seq "//" (:pattern ".*")))}}
