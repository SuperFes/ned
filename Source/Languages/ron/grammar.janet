# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "ron"
 :extras [(:pattern "\\s") line_comment block_comment]
 :conflicts []
 :precedences []
 :externals [_string_content raw_string float block_comment]
 :inline []
 :supertypes []
 :rules
 {source_file (:seq _value)
  _value (:choice array map struct tuple _literal enum_variant)
  enum_variant identifier
  array (:seq
         "["
         (:choice (:seq _value (:repeat (:seq "," _value))) :blank)
         (:choice "," :blank)
         "]")
  map (:seq
       "{"
       (:choice (:seq map_entry (:repeat (:seq "," map_entry))) :blank)
       (:choice "," :blank)
       "}")
  struct (:choice unit_struct _tuple_struct _named_struct)
  unit_struct (:choice "()")
  struct_name identifier
  _tuple_struct (:seq struct_name tuple)
  _named_struct (:seq (:choice struct_name :blank) (:field :body _struct_body))
  _struct_body (:seq
                "("
                (:choice (:seq struct_entry (:repeat (:seq "," struct_entry))) :blank)
                (:choice "," :blank)
                ")")
  tuple (:seq "(" (:seq _value (:repeat (:seq "," _value))) (:choice "," :blank) ")")
  map_entry (:seq _value ":" _value)
  struct_entry (:seq identifier ":" _value)
  _literal (:choice string char boolean integer float negative)
  integer (:token
           (:seq
            (:choice
             (:pattern "[0-9][0-9_]*")
             (:pattern "0x[0-9a-fA-F_]+")
             (:pattern "0b[01_]+")
             (:pattern "0o[0-7_]+"))))
  negative (:seq "-" (:choice integer float))
  string (:seq
          (:alias (:pattern "b?\"") "\"")
          (:repeat (:choice _escape_sequence _string_content))
          (:token-immediate "\""))
  char (:seq
        (:choice "b" :blank)
        "'"
        (:choice (:choice _escape_sequence (:pattern "[^\\\\']")) :blank)
        "'")
  _escape_sequence (:choice
                    (:prec 2 (:token-immediate (:seq "\\" (:pattern "[^abfnrtvxu'\\\"\\\\\\?]"))))
                    (:prec 1 escape_sequence))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu]")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u{[0-9a-fA-F]+}")
                     (:pattern "x[0-9a-fA-F]{2}"))))
  boolean (:choice "true" "false")
  identifier (:pattern "(r#)?[_\\p{XID_Start}][_\\p{XID_Continue}]*")
  comment (:choice line_comment block_comment)
  line_comment (:token (:seq "//" (:pattern ".*")))}}
