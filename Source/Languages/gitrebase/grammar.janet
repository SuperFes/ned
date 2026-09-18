# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "git_rebase"
 :extras [(:pattern "[\\t\\f\\v ]+") comment]
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source (:choice
          (:seq
           (:repeat (:pattern "[\\r?\\n]+"))
           (:seq operation (:repeat (:seq (:repeat1 (:pattern "[\\r?\\n]+")) operation)))
           (:repeat (:pattern "[\\r?\\n]+")))
          (:repeat (:pattern "[\\r?\\n]+")))
  operation (:choice
             _label_operation
             _exec_operation
             _merge_operation
             _fixup_operation
             _nullary_operation)
  _nullary_operation command
  _label_operation (:seq command label (:choice message :blank))
  _merge_operation (:seq command option label label (:choice message :blank))
  _fixup_operation (:seq command option label (:choice message :blank))
  _exec_operation (:seq (:alias (:choice "x" "exec") command) message)
  option (:choice "-c" "-C")
  label (:pattern "\\S+")
  message (:token (:prec -1 (:pattern "\\S[^\\n\\r]*")))
  command (:pattern "[a-zA-Z-]+")
  comment (:token (:prec -1 (:pattern "#[^\\r\\n]*")))}}
