# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "properties"
 :extras []
 :conflicts []
 :precedences []
 :externals [_eof]
 :inline []
 :supertypes []
 :rules
 {file (:repeat
        (:seq (:repeat _space) (:choice (:choice property comment) :blank) (:choice _eol _eof)))
  property (:prec-left 1
            (:seq
             key
             (:repeat _space)
             (:choice (:choice "=" ":") :blank)
             (:repeat _space)
             (:choice value :blank)))
  key (:prec-right 0 (:repeat1 (:choice "." _char escape _index)))
  _index (:seq "[" (:alias (:repeat _char) index) "]")
  value (:repeat1 (:choice _char escape substitution _linebreak))
  substitution (:seq
                "${"
                (:repeat1 (:choice key substitution))
                (:choice (:choice _default _secret) :blank)
                "}")
  _default (:seq ":" (:choice (:field :default _content) :blank))
  _secret (:seq "::" (:alias _content secret))
  _content (:repeat1 (:choice _char escape substitution))
  _linebreak (:seq "\\" _eol)
  escape (:choice (:pattern "\\\\[^\\r\\n]") (:pattern "\\\\u[0-9a-fA-F]{4}"))
  comment (:pattern "[#!].*")
  _space (:pattern "[ \\t\\f]")
  _char (:prec -1 (:pattern "."))
  _eol (:pattern "[\\r\\n]|\\r\\n")}}
