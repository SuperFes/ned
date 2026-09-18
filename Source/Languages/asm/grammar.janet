# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "asm"
 :extras [(:pattern " |\\t|\\r") line_comment block_comment]
 :conflicts [[_expr _tc_expr]]
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {program (:choice
           (:seq _item (:repeat (:seq (:repeat1 "\n") _item)) (:choice (:repeat1 "\n") :blank))
           :blank)
  _item (:choice meta label const instruction)
  meta (:seq
        (:field :kind meta_ident)
        (:choice
         (:choice
          ident
          (:seq int (:repeat (:seq "," int)))
          (:seq float (:repeat (:seq "," float)))
          (:seq string (:repeat (:seq "," string))))
         :blank))
  label (:choice
         (:seq
          (:choice meta_ident (:alias word ident) (:alias _ident ident))
          ":"
          (:choice (:seq "(" ident ")") :blank))
         (:seq "label" (:field :name word)))
  const (:seq "const" (:field :name word) (:field :value _tc_expr))
  instruction (:seq
               (:field :kind word)
               (:choice
                (:choice (:seq _expr (:repeat (:seq "," _expr)) (:choice "," :blank)) :blank)
                (:repeat _tc_expr)))
  _expr (:choice ptr ident int string float)
  ptr (:choice
       (:seq
        (:choice (:seq (:choice "byte" "word" "dword" "qword") "ptr") :blank)
        "["
        reg
        (:choice (:seq (:choice "+" "-") (:choice int ident)) :blank)
        "]")
       (:seq (:choice int :blank) "(" reg ")")
       (:seq "*" "rel" "[" int "]")
       (:seq "[" reg (:choice (:seq "," int) :blank) "]" (:choice "!" :blank)))
  _tc_expr (:choice ident int string tc_infix)
  tc_infix (:choice
            (:prec-left 0 (:seq (:field :lhs _tc_expr) (:field :op "+") (:field :rhs _tc_expr)))
            (:prec-left 0 (:seq (:field :lhs _tc_expr) (:field :op "-") (:field :rhs _tc_expr)))
            (:prec-left 1 (:seq (:field :lhs _tc_expr) (:field :op "*") (:field :rhs _tc_expr)))
            (:prec-left 1 (:seq (:field :lhs _tc_expr) (:field :op "/") (:field :rhs _tc_expr)))
            (:prec-left 1 (:seq (:field :lhs _tc_expr) (:field :op "%") (:field :rhs _tc_expr)))
            (:prec-left 2 (:seq (:field :lhs _tc_expr) (:field :op "|") (:field :rhs _tc_expr)))
            (:prec-left 3 (:seq (:field :lhs _tc_expr) (:field :op "^") (:field :rhs _tc_expr)))
            (:prec-left 4 (:seq (:field :lhs _tc_expr) (:field :op "&") (:field :rhs _tc_expr))))
  int (:choice
       (:seq
        "#"
        (:token-immediate
         (:pattern "-?([0-9][0-9_]*|(0x|\\$)[0-9A-Fa-f][0-9A-Fa-f_]*|0b[01][01_]*)")))
       (:pattern "-?([0-9][0-9_]*|(0x|\\$)[0-9A-Fa-f][0-9A-Fa-f_]*|0b[01][01_]*)"))
  float (:pattern "-?[0-9][0-9_]*\\.([0-9][0-9_]*)?")
  string (:pattern "\"[^\"]*\"")
  word (:pattern "[a-zA-Z0-9_]+")
  _reg (:pattern "%?[a-z0-9]+")
  address (:pattern "\\$[a-zA-Z0-9_]+")
  reg (:choice _reg word address)
  meta_ident (:pattern "\\.[a-z_]+")
  _ident (:pattern "[a-zA-Z_0-9.]+")
  ident (:choice _ident meta_ident reg)
  line_comment (:choice (:seq "#" (:token-immediate (:pattern ".*"))) (:pattern "(\\/\\/|;).*"))
  block_comment (:token (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/"))}}
