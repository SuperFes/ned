# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "desktop"
 :extras [(:pattern "[ \\t]")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {desktop_entry (:seq (:repeat (:choice comment (:pattern "\\n"))) (:repeat group))
  comment (:token (:seq (:pattern "#.*") (:pattern "\\n")))
  group (:seq header (:repeat (:choice entry comment (:pattern "\\n"))))
  header (:seq "[" group_name "]" (:pattern "\\n"))
  entry (:seq
         (:field :key identifier)
         (:choice (:field :locale locale) :blank)
         "="
         (:pattern "[ \\t]*")
         (:field :value (:choice (:ref "true") (:ref "false") string list))
         (:pattern "\\n"))
  locale (:seq
          (:token-immediate "[")
          language
          (:choice (:seq (:token-immediate "_") country) :blank)
          (:choice (:seq (:token-immediate ".") encoding) :blank)
          (:choice (:seq (:token-immediate "@") modifier) :blank)
          (:token-immediate "]"))
  language (:token-immediate (:pattern "[a-z-]+"))
  country (:token-immediate (:pattern "[a-zA-Z]+"))
  encoding (:token-immediate (:pattern "[a-zA-Z0-9-]+"))
  modifier (:token-immediate (:pattern "[a-zA-Z0-9-]+"))
  (:ref "true") "true"
  (:ref "false") "false"
  string (:repeat1 (:choice (:pattern "[^;\\n\\r]") escape_sequence field_code))
  escape_sequence (:pattern "\\\\[sntr\\\\;]")
  field_code (:pattern "%[%fFuUdDnNickvm]")
  list (:choice (:seq string ";") (:seq string (:repeat1 (:seq ";" string)) (:choice ";" :blank)))
  group_name (:pattern "[\\x20-\\x5A\\x5C\\x5E-\\x7E]+")
  identifier (:pattern "[A-Za-z0-9-]+")}}
