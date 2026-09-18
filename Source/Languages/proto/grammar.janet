# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "proto"
 :extras [comment (:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source_file (:seq
               (:choice (:choice syntax edition) :blank)
               (:choice
                (:repeat
                 (:choice import package option enum message extend service empty_statement))
                :blank))
  empty_statement ";"
  edition (:seq "edition" "=" (:field :year string) ";")
  syntax (:seq "syntax" "=" (:field :version string) ";")
  import (:seq
          "import"
          (:choice (:choice "weak" "public" "option") :blank)
          (:field :path string)
          ";")
  package (:seq "package" full_ident ";")
  option (:seq "option" _option_name "=" constant ";")
  _option_name (:seq
                (:choice identifier (:seq "(" (:choice "." :blank) full_ident ")"))
                (:repeat
                 (:seq "." (:choice identifier (:seq "(" (:choice "." :blank) full_ident ")")))))
  enum (:seq (:choice (:choice "export" "local") :blank) "enum" enum_name enum_body)
  enum_name identifier
  enum_body (:seq "{" (:repeat (:choice option enum_field empty_statement reserved)) "}")
  enum_field (:seq
              identifier
              "="
              (:choice "-" :blank)
              int_lit
              (:choice
               (:seq "[" enum_value_option (:repeat (:seq "," enum_value_option)) "]")
               :blank)
              ";")
  enum_value_option (:seq _option_name "=" constant)
  message (:seq (:choice (:choice "export" "local") :blank) "message" message_name message_body)
  message_body (:seq
                "{"
                (:repeat
                 (:choice
                  field
                  enum
                  message
                  option
                  oneof
                  map_field
                  reserved
                  extensions
                  extend
                  group
                  empty_statement))
                "}")
  message_name identifier
  extend (:seq "extend" (:choice "." :blank) full_ident message_body)
  group (:seq
         (:choice (:choice "optional" "required" "repeated") :blank)
         "group"
         message_name
         "="
         field_number
         (:choice (:seq "[" field_options "]") :blank)
         message_body)
  field (:seq
         (:choice (:choice "optional" "required") :blank)
         (:choice "repeated" :blank)
         type
         identifier
         "="
         field_number
         (:choice (:seq "[" field_options "]") :blank)
         ";")
  field_options (:seq field_option (:repeat (:seq "," field_option)))
  field_option (:seq _option_name "=" constant)
  oneof (:seq
         "oneof"
         identifier
         "{"
         (:repeat (:choice option oneof_field group empty_statement))
         "}")
  oneof_field (:seq
               type
               identifier
               "="
               field_number
               (:choice (:seq "[" field_options "]") :blank)
               ";")
  map_field (:seq
             "map"
             "<"
             key_type
             ","
             type
             ">"
             identifier
             "="
             field_number
             (:choice (:seq "[" field_options "]") :blank)
             ";")
  key_type (:choice
            "int32"
            "int64"
            "uint32"
            "uint64"
            "sint32"
            "sint64"
            "fixed32"
            "fixed64"
            "sfixed32"
            "sfixed64"
            "bool"
            "string")
  type (:choice
        "double"
        "float"
        "int32"
        "int64"
        "uint32"
        "uint64"
        "sint32"
        "sint64"
        "fixed32"
        "fixed64"
        "sfixed32"
        "sfixed64"
        "bool"
        "string"
        "bytes"
        message_or_enum_type)
  reserved (:seq "reserved" (:choice ranges reserved_field_names) ";")
  extensions (:seq "extensions" ranges (:choice (:seq "[" field_options "]") :blank) ";")
  ranges (:seq range (:repeat (:seq "," range)))
  range (:seq int_lit (:choice (:seq "to" (:choice int_lit "max")) :blank))
  reserved_field_names (:seq reserved_identifier (:repeat (:seq "," reserved_identifier)))
  message_or_enum_type (:seq (:choice "." :blank) (:repeat (:seq identifier ".")) identifier)
  field_number int_lit
  service (:seq "service" service_name "{" (:repeat (:choice option rpc empty_statement)) "}")
  service_name identifier
  rpc (:seq
       "rpc"
       rpc_name
       "("
       (:choice "stream" :blank)
       message_or_enum_type
       ")"
       "returns"
       "("
       (:choice "stream" :blank)
       message_or_enum_type
       ")"
       (:choice (:seq "{" (:repeat (:choice option empty_statement)) "}") ";"))
  rpc_name identifier
  constant (:choice
            full_ident
            (:seq (:choice (:choice "-" "+") :blank) int_lit)
            (:seq (:choice (:choice "-" "+") :blank) float_lit)
            string
            bool
            block_lit)
  block_lit (:choice (:seq "{" (:repeat _block_field) "}") (:seq "<" (:repeat _block_field) ">"))
  _block_field (:seq
                (:choice identifier extension_name)
                (:choice ":" :blank)
                (:choice
                 constant
                 (:seq "[" (:choice (:seq constant (:repeat (:seq "," constant))) :blank) "]"))
                (:choice (:choice "," ";") :blank))
  extension_name (:seq
                  "["
                  (:choice "." :blank)
                  (:field :name full_ident)
                  (:choice (:seq "/" (:field :type full_ident)) :blank)
                  "]")
  identifier (:token
              (:seq
               (:choice (:pattern "[a-zA-Z]") "_")
               (:choice (:repeat (:choice (:pattern "[a-zA-Z]") (:pattern "[0-9]") "_")) :blank)))
  reserved_identifier (:token
                       (:choice
                        (:seq
                         "\""
                         (:pattern "[a-zA-Z]")
                         (:choice
                          (:repeat (:choice (:pattern "[a-zA-Z]") (:pattern "[0-9]") "_"))
                          :blank)
                         "\"")
                        (:seq
                         "'"
                         (:pattern "[a-zA-Z]")
                         (:choice
                          (:repeat (:choice (:pattern "[a-zA-Z]") (:pattern "[0-9]") "_"))
                          :blank)
                         "'")
                        (:seq
                         (:pattern "[a-zA-Z]")
                         (:choice
                          (:repeat (:choice (:pattern "[a-zA-Z]") (:pattern "[0-9]") "_"))
                          :blank))))
  full_ident (:seq identifier (:choice (:repeat (:seq "." identifier)) :blank))
  bool (:choice (:ref "true") (:ref "false"))
  (:ref "true") "true"
  (:ref "false") "false"
  int_lit (:choice decimal_lit octal_lit hex_lit)
  decimal_lit (:token (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9]"))))
  octal_lit (:token (:seq "0" (:repeat (:pattern "[0-7]"))))
  hex_lit (:token
           (:seq "0" (:choice "x" "X") (:pattern "[0-9A-Fa-f]") (:repeat (:pattern "[0-9A-Fa-f]"))))
  float_lit (:token
             (:choice
              (:seq
               (:seq (:pattern "[0-9]") (:repeat (:pattern "[0-9]")))
               "."
               (:choice (:seq (:pattern "[0-9]") (:repeat (:pattern "[0-9]"))) :blank)
               (:choice
                (:seq
                 (:choice "e" "E")
                 (:choice (:choice "+" "-") :blank)
                 (:seq (:pattern "[0-9]") (:repeat (:pattern "[0-9]"))))
                :blank))
              (:seq
               (:seq (:pattern "[0-9]") (:repeat (:pattern "[0-9]")))
               (:seq
                (:choice "e" "E")
                (:choice (:choice "+" "-") :blank)
                (:seq (:pattern "[0-9]") (:repeat (:pattern "[0-9]")))))
              (:seq
               "."
               (:seq (:pattern "[0-9]") (:repeat (:pattern "[0-9]")))
               (:choice
                (:seq
                 (:choice "e" "E")
                 (:choice (:choice "+" "-") :blank)
                 (:seq (:pattern "[0-9]") (:repeat (:pattern "[0-9]"))))
                :blank))
              "inf"
              "nan"))
  string (:repeat1
          (:choice
           (:seq
            "\""
            (:repeat (:choice (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+"))) escape_sequence))
            "\"")
           (:seq
            "'"
            (:repeat (:choice (:token-immediate (:prec 1 (:pattern "[^'\\\\]+"))) escape_sequence))
            "'")))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xuU]")
                     (:pattern "\\d{2,3}")
                     (:pattern "x[0-9a-fA-F]{2,}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "U[0-9a-fA-F]{8}"))))
  comment (:token
           (:choice
            (:seq "//" (:pattern ".*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))}}
