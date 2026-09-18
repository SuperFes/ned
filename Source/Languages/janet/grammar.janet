# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "janet_simple"
 :extras [(:pattern "\0|\t|\\n|\x0b|\x0c|\\r| ") comment]
 :conflicts []
 :precedences []
 :externals [long_buf_lit long_str_lit]
 :inline []
 :supertypes []
 :rules
 {source (:repeat _lit)
  comment (:pattern "#.*")
  _lit (:choice
        bool_lit
        buf_lit
        kwd_lit
        long_buf_lit
        long_str_lit
        nil_lit
        num_lit
        str_lit
        sym_lit
        par_arr_lit
        sqr_arr_lit
        struct_lit
        tbl_lit
        par_tup_lit
        sqr_tup_lit
        qq_lit
        quote_lit
        short_fn_lit
        splice_lit
        unquote_lit)
  bool_lit (:choice "false" "true")
  kwd_lit (:prec 2 (:token (:seq ":" (:repeat (:pattern "[0-9:a-zA-Z!$%&*+\\-./<=>?@^_]")))))
  nil_lit "nil"
  num_lit (:prec 5 (:choice _radix _hex _dec))
  _radix (:token
          (:seq
           (:choice (:choice "-" "+") :blank)
           (:choice
            "2"
            "3"
            "4"
            "5"
            "6"
            "7"
            "8"
            "9"
            "10"
            "11"
            "12"
            "13"
            "14"
            "15"
            "16"
            "17"
            "18"
            "19"
            "20"
            "21"
            "22"
            "23"
            "24"
            "25"
            "26"
            "27"
            "28"
            "29"
            "30"
            "31"
            "32"
            "33"
            "34"
            "35"
            "36")
           "r"
           (:choice
            (:seq (:choice "." :blank) (:pattern "[a-zA-Z0-9][a-zA-Z0-9_]*"))
            (:seq
             (:pattern "[a-zA-Z0-9][a-zA-Z0-9_]*")
             "."
             (:choice (:pattern "[a-zA-Z0-9][a-zA-Z0-9_]*") :blank)))
           (:choice
            (:seq "&" (:choice (:choice "-" "+") :blank) (:repeat1 (:pattern "[a-zA-Z0-9]")))
            :blank)
           (:choice (:seq ":" (:pattern "[a-zA-Z]")) :blank)))
  _hex (:token
        (:seq
         (:choice (:choice "-" "+") :blank)
         "0x"
         (:choice
          (:seq (:choice "." :blank) (:pattern "[a-fA-F0-9][a-fA-F0-9_]*"))
          (:seq
           (:pattern "[a-fA-F0-9][a-fA-F0-9_]*")
           "."
           (:choice (:pattern "[a-fA-F0-9][a-fA-F0-9_]*") :blank)))
         (:choice (:seq ":" (:pattern "[a-zA-Z]")) :blank)))
  _dec (:token
        (:seq
         (:choice (:choice "-" "+") :blank)
         (:choice
          (:seq (:choice "." :blank) (:pattern "[0-9][0-9_]*"))
          (:seq (:pattern "[0-9][0-9_]*") "." (:choice (:pattern "[0-9][0-9_]*") :blank)))
         (:choice
          (:seq (:choice "e" "E") (:choice (:choice "-" "+") :blank) (:repeat1 (:pattern "[0-9]")))
          :blank)
         (:choice (:seq ":" (:pattern "[a-zA-Z]")) :blank)))
  str_lit (:token
           (:seq "\"" (:repeat (:choice (:pattern "[^\\\\\"]") (:pattern "\\\\(.|\\n)"))) "\""))
  buf_lit (:token
           (:seq "@\"" (:repeat (:choice (:pattern "[^\\\\\"]") (:pattern "\\\\(.|\\n)"))) "\""))
  sym_lit (:token
           (:seq
            (:pattern "[a-zA-Z!$%&*+\\-./<=>?@^_]")
            (:repeat (:pattern "[0-9:a-zA-Z!$%&*+\\-./<=>?@^_]"))))
  par_arr_lit (:seq "@(" (:repeat _lit) ")")
  sqr_arr_lit (:seq "@[" (:repeat _lit) "]")
  struct_lit (:seq "{" (:repeat _lit) "}")
  tbl_lit (:seq "@{" (:repeat _lit) "}")
  par_tup_lit (:seq "(" (:repeat _lit) ")")
  sqr_tup_lit (:seq "[" (:repeat _lit) "]")
  qq_lit (:seq "~" _lit)
  quote_lit (:seq "'" _lit)
  short_fn_lit (:seq "|" _lit)
  splice_lit (:seq ";" _lit)
  unquote_lit (:seq "," _lit)}}
