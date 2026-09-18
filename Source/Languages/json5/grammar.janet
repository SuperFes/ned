# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "json5"
 :extras [comment (:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals []
 :inline [name]
 :supertypes []
 :rules
 {file _value
  comment (:token
           (:choice
            (:seq "//" (:pattern "[^\\n]*"))
            (:seq "/*" (:pattern "([^*]|\\*+[^/*])*\\*+") "/")))
  object (:seq
          "{"
          (:choice (:seq member (:repeat (:seq "," member)) (:choice "," :blank)) :blank)
          "}")
  member (:seq (:field :name name) ":" (:field :value _value))
  name (:choice string identifier)
  identifier (:token
              (:seq
               (:pattern "[\\$_\\p{L}]" "u")
               (:repeat (:choice (:pattern "[\\$_\\p{L}]" "u") (:pattern "[0-9]")))))
  array (:seq
         "["
         (:choice (:seq _value (:repeat (:seq "," _value)) (:choice "," :blank)) :blank)
         "]")
  string (:token
          (:choice
           (:seq
            "\""
            (:repeat
             (:choice
              (:seq
               "\\"
               (:choice
                (:pattern "[\"'\\\\/bfnrtv]")
                (:pattern "u[0-9a-fA-F]{4}")
                (:pattern "x[0-9a-fA-F]{2}")
                (:pattern "\\r?\\n")))
              (:pattern "[^\"\\\\]+")))
            "\"")
           (:seq
            "'"
            (:repeat
             (:choice
              (:seq
               "\\"
               (:choice
                (:pattern "[\"'\\\\/bfnrtv]")
                (:pattern "u[0-9a-fA-F]{4}")
                (:pattern "x[0-9a-fA-F]{2}")
                (:pattern "\\r?\\n")))
              (:pattern "[^'\\\\]+")))
            "'")))
  number (:token
          (:seq
           (:pattern "[+-]?")
           (:choice
            (:seq "0" (:pattern "[xX]") (:pattern "[0-9a-fA-F]+"))
            (:choice
             (:seq
              (:choice "0" (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9]"))))
              "."
              (:repeat (:pattern "[0-9]"))
              (:choice
               (:seq
                (:pattern "[eE]")
                (:choice (:pattern "[+-]") :blank)
                (:repeat1 (:pattern "[0-9]")))
               :blank))
             (:seq
              "."
              (:repeat (:pattern "[0-9]"))
              (:choice
               (:seq
                (:pattern "[eE]")
                (:choice (:pattern "[+-]") :blank)
                (:repeat1 (:pattern "[0-9]")))
               :blank))
             (:seq
              (:choice "0" (:seq (:pattern "[1-9]") (:repeat (:pattern "[0-9]"))))
              (:choice
               (:seq
                (:pattern "[eE]")
                (:choice (:pattern "[+-]") :blank)
                (:repeat1 (:pattern "[0-9]")))
               :blank)))
            "Infinity"
            "NaN")))
  null "null"
  (:ref "true") "true"
  (:ref "false") "false"
  _value (:choice object array number string null (:ref "true") (:ref "false"))}}
