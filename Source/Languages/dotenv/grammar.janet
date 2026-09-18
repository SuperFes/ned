# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "dotenv"
 :word identifier
 :extras [(:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals [_end_of_assignment]
 :inline []
 :supertypes []
 :rules
 {document (:repeat (:choice comment assignment))
  assignment (:choice
              (:seq
               (:field :key (:choice identifier (:alias "export" identifier)))
               "="
               (:choice (:field :value _value) :blank)
               _end_of_assignment)
              (:prec 1
               (:seq
                "export"
                (:field :key (:choice identifier (:alias "export" identifier)))
                "="
                (:choice (:field :value _value) :blank)
                _end_of_assignment)))
  comment (:pattern "\\#[^\\n]*")
  identifier (:pattern "[A-Za-z_][A-Za-z0-9_]*")
  variable (:seq "$" (:choice identifier (:seq "{" identifier "}")))
  _value (:choice string number boolean variable value)
  string (:choice _string _literal_string _unquoted_string)
  _unquoted_string (:alias
                    (:pattern "[^\\#\\s\\\"\\'\\$]+(?:[ \\t]+[^\\#\\s\\\"\\'\\$]+)+")
                    string_content)
  _literal_string (:seq "'" (:choice (:alias (:pattern "[^']+") string_content) :blank) "'")
  _string (:seq
           "\""
           (:choice
            (:repeat (:choice (:alias (:pattern "[^\"\\$]+") string_content) variable))
            :blank)
           "\"")
  number (:choice integer float)
  integer (:choice decimal hexadecimal)
  decimal (:pattern "(\\-)?[1-9]\\d*")
  hexadecimal (:pattern "0[xX][0-9a-fA-F]+")
  float (:token (:seq (:pattern "(\\-)?[1-9]\\d*") (:pattern "\\.\\d+")))
  boolean (:token (:choice "true" "false"))
  value (:pattern "[^\\#\\s\\\"\\'\\$]+")}}
