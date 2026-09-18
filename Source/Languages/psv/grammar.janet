# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "psv"
 :extras [(:pattern "\\s")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {document (:seq (:repeat (:seq row (:pattern "\\r|\\r\\n|\\n"))) (:choice row :blank))
  row (:seq field (:repeat (:seq "|" field)))
  field (:choice text number float boolean)
  number (:choice (:pattern "\\d+") (:pattern "0[xX][0-9a-fA-F]+"))
  float (:choice (:pattern "\\d*\\.\\d+") (:pattern "\\d+\\.\\d*"))
  boolean (:choice "true" "false")
  text (:token
        (:choice
         (:pattern "[^|\\r\\n]*")
         (:seq "\"" (:repeat (:choice (:pattern "[^\"]") "\"\"")) "\"")))}}
