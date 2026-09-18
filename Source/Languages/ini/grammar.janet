# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "ini"
 :extras [comment _blank (:pattern "[\\t ]")]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {document (:seq (:repeat _blank) (:choice (:repeat (:seq setting)) :blank) (:repeat section))
  section (:prec-left 0 (:seq section_name (:repeat (:seq setting))))
  section_name (:seq "[" (:alias (:pattern "[^\\[\\]]+") text) "]" (:pattern "\\r?\\n"))
  setting (:seq
           (:alias (:pattern "[^;#=\\s\\[]+( *[^;#=\\s\\[])*") setting_name)
           "="
           (:choice (:alias (:pattern ".+") setting_value) :blank)
           (:pattern "\\r?\\n"))
  comment (:seq (:pattern "[;#]") (:alias (:pattern "[^\\r\\n]*") text) (:pattern "\\r?\\n"))
  _blank (:field :blank (:pattern "\\r?\\n"))}}
