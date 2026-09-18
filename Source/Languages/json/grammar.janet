# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "json"
 :extras [(:pattern "\\s") comment]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes [_value]
 :rules
 {document (:repeat _value)
  _value (:choice object array number string (:ref "true") (:ref "false") null)
  object (:seq "{" (:choice (:seq pair (:repeat (:seq "," pair))) :blank) "}")
  pair (:seq (:field :key string) ":" (:field :value _value))
  array (:seq "[" (:choice (:seq _value (:repeat (:seq "," _value))) :blank) "]")
  string (:choice (:seq "\"" "\"") (:seq "\"" _string_content "\""))
  _string_content (:repeat1 (:choice string_content escape_sequence))
  string_content (:token-immediate (:prec 1 (:pattern "[^\\\\\"\\n]+")))
  escape_sequence (:token-immediate (:seq "\\" (:pattern "(\\\"|\\\\|\\/|b|f|n|r|t|u)")))
  number (:token
          (:choice
           (:seq
            (:seq
             (:choice "-" :blank)
             (:choice "0" (:seq (:pattern "[1-9]") (:choice (:pattern "\\d+") :blank))))
            "."
            (:choice (:pattern "\\d+") :blank)
            (:choice (:seq (:choice "e" "E") (:seq (:choice "-" :blank) (:pattern "\\d+"))) :blank))
           (:seq
            (:seq
             (:choice "-" :blank)
             (:choice "0" (:seq (:pattern "[1-9]") (:choice (:pattern "\\d+") :blank))))
            (:choice (:seq (:choice "e" "E") (:seq (:choice "-" :blank) (:pattern "\\d+"))) :blank))))
  (:ref "true") "true"
  (:ref "false") "false"
  null "null"
  comment (:token
           (:choice
            (:seq "//" (:pattern ".*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))}}
