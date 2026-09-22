# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "jsonnet"
 :word _ident
 :extras [(:pattern "\\s") comment]
 :conflicts []
 :precedences []
 :externals [_string_start _string_content _string_end]
 :inline [h objinside]
 :supertypes []
 :rules
 {document _expr
  comment (:token
           (:choice
            (:seq "//" (:pattern ".*"))
            (:seq "#" (:pattern ".*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))
  _expr (:choice
         null
         (:ref "true")
         (:ref "false")
         self
         dollar
         string
         number
         object
         array
         forloop
         fieldaccess
         indexing
         fieldaccess_super
         indexing_super
         functioncall
         id
         local_bind
         conditional
         binary
         unary
         implicit_plus
         anonymous_function
         _assert_expr
         import
         importstr
         error
         in_super
         parenthesis)
  null "null"
  (:ref "true") "true"
  (:ref "false") "false"
  self "self"
  dollar "$"
  super "super"
  local "local"
  tailstrict "tailstrict"
  number _number
  string _string
  object (:seq "{" (:choice objinside :blank) "}")
  array (:seq "[" (:choice (:seq _expr (:repeat (:seq "," _expr)) (:choice "," :blank)) :blank) "]")
  forloop (:seq "[" _expr (:choice "," :blank) forspec (:choice compspec :blank) "]")
  fieldaccess (:prec 13 (:seq _expr "." (:field :last id)))
  indexing (:prec 13
            (:seq
             _expr
             "["
             (:choice _expr :blank)
             (:choice
              (:seq ":" (:choice _expr :blank) (:choice (:seq ":" (:choice _expr :blank)) :blank))
              :blank)
             "]"))
  fieldaccess_super (:seq super "." id)
  indexing_super (:seq super "[" _expr "]")
  functioncall (:prec 13 (:seq _expr "(" (:choice args :blank) ")" (:choice tailstrict :blank)))
  id _ident
  _ident (:pattern "[_a-zA-Z][_a-zA-Z0-9]*")
  local_bind (:prec-right 0 (:seq local (:seq bind (:repeat (:seq "," bind))) ";" _expr))
  conditional (:prec-right 0
               (:seq
                "if"
                (:field :condition _expr)
                "then"
                (:field :consequence _expr)
                (:choice (:seq "else" (:field :alternative _expr)) :blank)))
  multiplicative (:choice "*" "/" "%")
  additive (:choice "+" "-")
  bitshift (:choice "<<" ">>")
  comparison (:choice "<" "<=" ">" ">=" "in")
  equality (:choice "==" "!=")
  bitand "&"
  bitxor "^"
  bitor "|"
  and "&&"
  or "||"
  binary (:choice
          (:prec-left 11
           (:seq (:field :left _expr) (:field :operator multiplicative) (:field :right _expr)))
          (:prec-left 10
           (:seq (:field :left _expr) (:field :operator additive) (:field :right _expr)))
          (:prec-left 9
           (:seq (:field :left _expr) (:field :operator bitshift) (:field :right _expr)))
          (:prec-left 8
           (:seq (:field :left _expr) (:field :operator comparison) (:field :right _expr)))
          (:prec-left 7
           (:seq (:field :left _expr) (:field :operator equality) (:field :right _expr)))
          (:prec-left 6 (:seq (:field :left _expr) (:field :operator bitand) (:field :right _expr)))
          (:prec-left 5 (:seq (:field :left _expr) (:field :operator bitxor) (:field :right _expr)))
          (:prec-left 4 (:seq (:field :left _expr) (:field :operator bitor) (:field :right _expr)))
          (:prec-left 3 (:seq (:field :left _expr) (:field :operator and) (:field :right _expr)))
          (:prec-left 2 (:seq (:field :left _expr) (:field :operator or) (:field :right _expr))))
  unary (:prec 12 (:seq (:field :operator unaryop) (:field :argument _expr)))
  unaryop (:choice "-" "+" "!" "~")
  implicit_plus (:seq _expr object)
  anonymous_function (:prec-right 0
                      (:seq
                       "function"
                       "("
                       (:choice (:field :params params) :blank)
                       ")"
                       (:field :body _expr)))
  _assert_expr (:prec-right 0 (:seq assert ";" _expr))
  import (:seq "import" string)
  importstr (:seq "importstr" string)
  error (:prec-right 0 (:seq "error" _expr))
  in_super (:prec 8 (:seq _expr "in" super))
  parenthesis (:seq "(" _expr ")")
  objinside (:choice (:seq member (:repeat (:seq "," member)) (:choice "," :blank)) objforloop)
  objforloop (:seq
              (:repeat (:seq objlocal ","))
              field
              (:repeat (:seq "," objlocal))
              (:choice "," :blank)
              forspec
              (:choice compspec :blank))
  member (:prec-right 1 (:choice objlocal assert field))
  field (:choice
         (:seq fieldname (:choice "+" :blank) h _expr)
         (:seq (:field :function fieldname) "(" (:choice params :blank) ")" h _expr))
  h (:choice ":" "::" ":::")
  objlocal (:seq local bind)
  compspec (:repeat1 (:choice forspec ifspec))
  forspec (:seq "for" id "in" _expr)
  ifspec (:seq "if" _expr)
  fieldname (:prec-right 0 (:choice id string (:seq "[" _expr "]")))
  assert (:seq "assert" _expr (:choice (:seq ":" _expr) :blank))
  bind (:choice
        (:seq id "=" _expr)
        (:prec-right 0
         (:seq
          (:field :function id)
          "("
          (:choice (:field :params params) :blank)
          ")"
          "="
          (:field :body _expr))))
  args (:choice
        (:seq
         _expr
         (:repeat (:seq "," _expr))
         (:repeat (:seq "," named_argument))
         (:choice "," :blank))
        (:seq named_argument (:repeat (:seq "," named_argument)) (:choice "," :blank)))
  named_argument (:seq id "=" _expr)
  params (:seq param (:repeat (:seq "," param)) (:choice "," :blank))
  param (:seq (:field :identifier id) (:choice (:seq "=" (:field :value _expr)) :blank))
  _number (:token
           (:choice
            (:seq (:choice "0x" "0X") (:pattern "[\\da-fA-F]+"))
            (:choice
             (:seq
              (:seq
               (:choice (:choice "-" "+") :blank)
               (:choice "0" (:seq (:pattern "[1-9]") (:choice (:pattern "\\d+") :blank))))
              "."
              (:choice (:pattern "\\d+") :blank)
              (:choice
               (:seq (:choice "e" "E") (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d+")))
               :blank))
             (:seq
              "."
              (:pattern "\\d+")
              (:choice
               (:seq (:choice "e" "E") (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d+")))
               :blank))
             (:seq
              (:seq
               (:choice (:choice "-" "+") :blank)
               (:choice "0" (:seq (:pattern "[1-9]") (:choice (:pattern "\\d+") :blank))))
              (:choice
               (:seq (:choice "e" "E") (:seq (:choice (:choice "-" "+") :blank) (:pattern "\\d+")))
               :blank)))
            (:seq (:choice "0b" "0B") (:pattern "[0-1]+"))
            (:seq (:choice "0o" "0O") (:pattern "[0-7]+"))))
  _string (:choice
           (:seq (:choice "@" :blank) (:alias _single string_start) (:alias _single string_end))
           (:seq
            (:choice "@" :blank)
            (:alias _single string_start)
            (:alias _str_single string_content)
            (:alias _single string_end))
           (:seq (:choice "@" :blank) (:alias _double string_start) (:alias _double string_end))
           (:seq
            (:choice "@" :blank)
            (:alias _double string_start)
            (:alias _str_double string_content)
            (:alias _double string_end))
           (:seq
            (:choice "@" :blank)
            (:alias _string_start string_start)
            (:alias _string_content string_content)
            (:alias _string_end string_end)))
  _single "'"
  _double "\""
  _str_double (:repeat1
               (:choice (:token-immediate (:prec 1 (:pattern "[^\\\\\"\\n]+"))) escape_sequence))
  _str_single (:repeat1
               (:choice (:token-immediate (:prec 1 (:pattern "[^\\\\'\\n]+"))) escape_sequence))
  escape_sequence (:token-immediate (:seq "\\" (:pattern "(\\\"|\\\\|\\/|b|f|n|r|t|u)")))}}
