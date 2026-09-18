# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "pem"
 :extras [(:pattern "\\r?\\n")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {pem (:repeat (:choice content comment))
  content (:seq header data footer)
  header (:seq dashes "BEGIN" " " label dashes)
  footer (:seq dashes "END" " " label dashes)
  data (:repeat1 (:pattern "[A-Za-z0-9+/]+={0,2}"))
  label (:pattern "[^-]+")
  dashes "-----"
  comment (:token (:prec -2 (:pattern ".+")))}}
