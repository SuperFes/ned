# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "racket"
 :extras []
 :conflicts []
 :precedences []
 :externals [_here_string_body]
 :inline []
 :supertypes []
 :rules
 {program (:repeat _token)
  _token (:choice _skip extension _datum)
  _skip (:choice
         (:pattern "[ \\t\\n\\v\\f\\r\\u{0085}\\u{00A0}\\u{1680}\\u{2000}-\\u{200A}\\u{2028}\\u{2029}\\u{202F}\\u{205F}\\u{3000}\\u{FEFF}]+" "u")
         comment
         sexp_comment
         block_comment)
  dot "."
  comment (:choice
           (:token (:seq ";" (:repeat (:pattern "[^\\r\\n\\u{85}\\u{2028}\\u{2029}]"))))
           _line_comment)
  block_comment (:seq
                 "#|"
                 (:repeat
                  (:choice
                   (:prec 100 block_comment)
                   (:pattern ".|[\\r\\n\\u{85}\\u{2028}\\u{2029}]")))
                 (:prec 100 "|#"))
  sexp_comment (:seq "#;" (:repeat _skip) _datum)
  _line_comment (:token
                 (:seq
                  (:choice "#! " "#!/")
                  (:repeat
                   (:seq
                    (:repeat (:pattern "[^\\r\\n\\u{85}\\u{2028}\\u{2029}]"))
                    "\\"
                    (:pattern "[\\r\\n\\u{85}\\u{2028}\\u{2029}]")))
                  (:repeat (:pattern "[^\\r\\n\\u{85}\\u{2028}\\u{2029}]"))))
  _datum (:choice
          boolean
          string
          here_string
          byte_string
          character
          number
          symbol
          keyword
          regex
          box
          graph
          structure
          hash
          quote
          quasiquote
          syntax
          quasisyntax
          unquote
          unquote_splicing
          unsyntax
          unsyntax_splicing
          list
          vector)
  boolean (:token (:choice "#true" "#t" "#T" "#false" "#f" "#F"))
  string _real_string
  byte_string (:seq "#" _real_string)
  here_string (:seq "#<<" _here_string_body)
  regex (:seq (:token (:choice "#rx" "#px" "#rx#" "#px#")) _real_string)
  _real_string (:seq "\"" (:repeat (:choice escape_sequence (:pattern "[^\"\\\\]+"))) "\"")
  escape_sequence (:token
                   (:choice
                    "\\a"
                    "\\b"
                    "\\t"
                    "\\n"
                    "\\v"
                    "\\f"
                    "\\r"
                    "\\e"
                    "\\\""
                    "\\'"
                    "\\\\"
                    (:seq "\\" (:pattern "[0-7]{1,3}"))
                    (:seq "\\x" (:pattern "[0-9a-fA-F]{1,2}"))
                    (:seq "\\u" (:pattern "[0-9a-fA-F]{1,4}"))
                    (:seq "\\u" (:pattern "[0-9a-fA-F]{4,4}"))
                    (:seq "\\U" (:pattern "[0-9a-fA-F]{1,8}"))
                    (:seq "\\" (:pattern "[\\r\\n]|(\\r\\n)"))))
  number (:token
          (:choice
           (:seq
            (:pattern "#[bB]")
            (:choice
             (:seq
              (:choice (:pattern "[+-]") :blank)
              (:seq
               (:choice
                (:seq
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                 (:choice "." :blank)
                 (:repeat "#"))
                (:seq
                 (:choice (:repeat1 (:pattern "[01]")) :blank)
                 "."
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                (:seq
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                 "/"
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))))
               (:choice
                (:seq
                 (:pattern "[tT]")
                 (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[01]"))))
                :blank)))
             (:seq
              (:pattern "[+-]")
              (:choice (:pattern "[iI][nN][fF]\\.[0fFtT]") (:pattern "[nN][aA][nN]\\.[0fFtT]")))))
           (:seq
            (:pattern "#[oO]")
            (:choice
             (:seq
              (:choice (:pattern "[+-]") :blank)
              (:seq
               (:choice
                (:seq
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                 (:choice "." :blank)
                 (:repeat "#"))
                (:seq
                 (:choice (:repeat1 (:pattern "[0-7]")) :blank)
                 "."
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                (:seq
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                 "/"
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))))
               (:choice
                (:seq
                 (:pattern "[tT]")
                 (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-7]"))))
                :blank)))
             (:seq
              (:pattern "[+-]")
              (:choice (:pattern "[iI][nN][fF]\\.[0fFtT]") (:pattern "[nN][aA][nN]\\.[0fFtT]")))))
           (:seq
            (:choice (:pattern "#[dD]") :blank)
            (:choice
             (:seq
              (:choice (:pattern "[+-]") :blank)
              (:seq
               (:choice
                (:seq
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                 (:choice "." :blank)
                 (:repeat "#"))
                (:seq
                 (:choice (:repeat1 (:pattern "[0-9]")) :blank)
                 "."
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                (:seq
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                 "/"
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))))
               (:choice
                (:seq
                 (:pattern "[tT]")
                 (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9]"))))
                :blank)))
             (:seq
              (:pattern "[+-]")
              (:choice (:pattern "[iI][nN][fF]\\.[0fFtT]") (:pattern "[nN][aA][nN]\\.[0fFtT]")))))
           (:seq
            (:pattern "#[xX]")
            (:choice
             (:seq
              (:choice (:pattern "[+-]") :blank)
              (:seq
               (:choice
                (:seq
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                 (:choice "." :blank)
                 (:repeat "#"))
                (:seq
                 (:choice (:repeat1 (:pattern "[0-9a-fA-F]")) :blank)
                 "."
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                (:seq
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                 "/"
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))))
               (:choice
                (:seq
                 (:pattern "[tT]")
                 (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9a-fA-F]"))))
                :blank)))
             (:seq
              (:pattern "[+-]")
              (:choice (:pattern "[iI][nN][fF]\\.[0fFtT]") (:pattern "[nN][aA][nN]\\.[0fFtT]")))))
           (:seq
            (:choice
             (:seq (:choice (:pattern "#[eiEI]") :blank) (:pattern "#[bB]"))
             (:seq (:pattern "#[bB]") (:choice (:pattern "#[eiEI]") :blank)))
            (:choice
             (:choice
              (:seq
               (:choice (:pattern "[+-]") :blank)
               (:choice
                (:repeat1 (:pattern "[01]"))
                (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[01]"))
                  (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:repeat1 (:pattern "[01]"))
                 (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]"))))
                :blank)
               (:pattern "[iI]")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:seq
                 (:choice
                  (:seq
                   (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                   (:choice "." :blank)
                   (:repeat "#"))
                  (:seq
                   (:choice (:repeat1 (:pattern "[01]")) :blank)
                   "."
                   (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                  (:seq
                   (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                   "/"
                   (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))))
                 (:choice
                  (:seq
                   (:pattern "[sldefSLDEF]")
                   (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[01]"))))
                  :blank)))
               (:seq
                (:pattern "[+-]")
                (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
              (:choice
               (:seq
                (:choice
                 (:choice
                  (:seq
                   (:choice (:pattern "[+-]") :blank)
                   (:seq
                    (:choice
                     (:seq
                      (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                      (:choice "." :blank)
                      (:repeat "#"))
                     (:seq
                      (:choice (:repeat1 (:pattern "[01]")) :blank)
                      "."
                      (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                     (:seq
                      (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                      "/"
                      (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))))
                    (:choice
                     (:seq
                      (:pattern "[sldefSLDEF]")
                      (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[01]"))))
                     :blank)))
                  (:seq
                   (:pattern "[+-]")
                   (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
                 :blank)
                (:pattern "[+-]")
                (:choice
                 (:choice
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[01]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[01]"))))
                    :blank))
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]")))
                 :blank)
                (:pattern "[iI]"))
               (:seq
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[01]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[01]"))))
                    :blank)))
                 (:seq
                  (:pattern "[+-]")
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
                "@"
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[01]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[01]"))))
                    :blank)))
                 (:seq
                  (:pattern "[+-]")
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]")))))))))
           (:seq
            (:choice
             (:seq (:choice (:pattern "#[eiEI]") :blank) (:pattern "#[oO]"))
             (:seq (:pattern "#[oO]") (:choice (:pattern "#[eiEI]") :blank)))
            (:choice
             (:choice
              (:seq
               (:choice (:pattern "[+-]") :blank)
               (:choice
                (:repeat1 (:pattern "[0-7]"))
                (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-7]"))
                  (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:repeat1 (:pattern "[0-7]"))
                 (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]"))))
                :blank)
               (:pattern "[iI]")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:seq
                 (:choice
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                   (:choice "." :blank)
                   (:repeat "#"))
                  (:seq
                   (:choice (:repeat1 (:pattern "[0-7]")) :blank)
                   "."
                   (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                   "/"
                   (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))))
                 (:choice
                  (:seq
                   (:pattern "[sldefSLDEF]")
                   (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-7]"))))
                  :blank)))
               (:seq
                (:pattern "[+-]")
                (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
              (:choice
               (:seq
                (:choice
                 (:choice
                  (:seq
                   (:choice (:pattern "[+-]") :blank)
                   (:seq
                    (:choice
                     (:seq
                      (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                      (:choice "." :blank)
                      (:repeat "#"))
                     (:seq
                      (:choice (:repeat1 (:pattern "[0-7]")) :blank)
                      "."
                      (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                     (:seq
                      (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                      "/"
                      (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))))
                    (:choice
                     (:seq
                      (:pattern "[sldefSLDEF]")
                      (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-7]"))))
                     :blank)))
                  (:seq
                   (:pattern "[+-]")
                   (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
                 :blank)
                (:pattern "[+-]")
                (:choice
                 (:choice
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-7]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-7]"))))
                    :blank))
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]")))
                 :blank)
                (:pattern "[iI]"))
               (:seq
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-7]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-7]"))))
                    :blank)))
                 (:seq
                  (:pattern "[+-]")
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
                "@"
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-7]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-7]"))))
                    :blank)))
                 (:seq
                  (:pattern "[+-]")
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]")))))))))
           (:seq
            (:choice
             (:seq (:choice (:pattern "#[eiEI]") :blank) (:choice (:pattern "#[dD]") :blank))
             (:seq (:choice (:pattern "#[dD]") :blank) (:choice (:pattern "#[eiEI]") :blank)))
            (:choice
             (:choice
              (:seq
               (:choice (:pattern "[+-]") :blank)
               (:choice
                (:repeat1 (:pattern "[0-9]"))
                (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9]"))
                  (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:repeat1 (:pattern "[0-9]"))
                 (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]"))))
                :blank)
               (:pattern "[iI]")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:seq
                 (:choice
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                   (:choice "." :blank)
                   (:repeat "#"))
                  (:seq
                   (:choice (:repeat1 (:pattern "[0-9]")) :blank)
                   "."
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                   "/"
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))))
                 (:choice
                  (:seq
                   (:pattern "[sldefSLDEF]")
                   (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9]"))))
                  :blank)))
               (:seq
                (:pattern "[+-]")
                (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
              (:choice
               (:seq
                (:choice
                 (:choice
                  (:seq
                   (:choice (:pattern "[+-]") :blank)
                   (:seq
                    (:choice
                     (:seq
                      (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                      (:choice "." :blank)
                      (:repeat "#"))
                     (:seq
                      (:choice (:repeat1 (:pattern "[0-9]")) :blank)
                      "."
                      (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                     (:seq
                      (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                      "/"
                      (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))))
                    (:choice
                     (:seq
                      (:pattern "[sldefSLDEF]")
                      (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9]"))))
                     :blank)))
                  (:seq
                   (:pattern "[+-]")
                   (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
                 :blank)
                (:pattern "[+-]")
                (:choice
                 (:choice
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-9]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9]"))))
                    :blank))
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]")))
                 :blank)
                (:pattern "[iI]"))
               (:seq
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-9]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9]"))))
                    :blank)))
                 (:seq
                  (:pattern "[+-]")
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
                "@"
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-9]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[sldefSLDEF]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9]"))))
                    :blank)))
                 (:seq
                  (:pattern "[+-]")
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]")))))))))
           (:seq
            (:choice
             (:seq (:choice (:pattern "#[eiEI]") :blank) (:pattern "#[xX]"))
             (:seq (:pattern "#[xX]") (:choice (:pattern "#[eiEI]") :blank)))
            (:choice
             (:choice
              (:seq
               (:choice (:pattern "[+-]") :blank)
               (:choice
                (:repeat1 (:pattern "[0-9a-fA-F]"))
                (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9a-fA-F]"))
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:repeat1 (:pattern "[0-9a-fA-F]"))
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]"))))
                :blank)
               (:pattern "[iI]")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:seq
                 (:choice
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                   (:choice "." :blank)
                   (:repeat "#"))
                  (:seq
                   (:choice (:repeat1 (:pattern "[0-9a-fA-F]")) :blank)
                   "."
                   (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                   "/"
                   (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))))
                 (:choice
                  (:seq
                   (:pattern "[slSL]")
                   (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9a-fA-F]"))))
                  :blank)))
               (:seq
                (:pattern "[+-]")
                (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
              (:choice
               (:seq
                (:choice
                 (:choice
                  (:seq
                   (:choice (:pattern "[+-]") :blank)
                   (:seq
                    (:choice
                     (:seq
                      (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                      (:choice "." :blank)
                      (:repeat "#"))
                     (:seq
                      (:choice (:repeat1 (:pattern "[0-9a-fA-F]")) :blank)
                      "."
                      (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                     (:seq
                      (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                      "/"
                      (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))))
                    (:choice
                     (:seq
                      (:pattern "[slSL]")
                      (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9a-fA-F]"))))
                     :blank)))
                  (:seq
                   (:pattern "[+-]")
                   (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
                 :blank)
                (:pattern "[+-]")
                (:choice
                 (:choice
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-9a-fA-F]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[slSL]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9a-fA-F]"))))
                    :blank))
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]")))
                 :blank)
                (:pattern "[iI]"))
               (:seq
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-9a-fA-F]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[slSL]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9a-fA-F]"))))
                    :blank)))
                 (:seq
                  (:pattern "[+-]")
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]"))))
                "@"
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                     (:choice "." :blank)
                     (:repeat "#"))
                    (:seq
                     (:choice (:repeat1 (:pattern "[0-9a-fA-F]")) :blank)
                     "."
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                    (:seq
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                     "/"
                     (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))))
                   (:choice
                    (:seq
                     (:pattern "[slSL]")
                     (:seq (:choice (:pattern "[+-]") :blank) (:repeat1 (:pattern "[0-9a-fA-F]"))))
                    :blank)))
                 (:seq
                  (:pattern "[+-]")
                  (:choice (:pattern "[iI][nN][fF]\\.[0fF]") (:pattern "[nN][aA][nN]\\.[0fF]")))))))))))
  decimal (:pattern "[0-9]+")
  character (:token
             (:seq
              "#\\"
              (:choice
               "nul"
               "null"
               "backspace"
               "tab"
               "newline"
               "linefeed"
               "vtab"
               "page"
               "return"
               "space"
               "rubout"
               (:pattern "[0-7]{3,3}")
               (:pattern "u[0-9a-fA-F]{1,4}")
               (:pattern "U[0-9a-fA-F]{1,8}")
               (:pattern "."))))
  symbol (:token
          (:choice
           (:pattern "#[cC][iIsS]")
           (:seq
            (:choice
             (:pattern "[^# \\t\\n\\v\\f\\r\\u{0085}\\u{00A0}\\u{1680}\\u{2000}-\\u{200A}\\u{2028}\\u{2029}\\u{202F}\\u{205F}\\u{3000}\\u{FEFF}\\(\\)\\{\\}\",'`;\\[\\]\\|\\\\]" "u")
             "#%"
             (:pattern "\\|[^\\|]*\\|")
             (:pattern "\\\\."))
            (:repeat
             (:choice
              (:pattern "[^ \\t\\n\\v\\f\\r\\u{0085}\\u{00A0}\\u{1680}\\u{2000}-\\u{200A}\\u{2028}\\u{2029}\\u{202F}\\u{205F}\\u{3000}\\u{FEFF}(){}\",'`;\\[\\]\\|\\\\]" "u")
              (:pattern "\\|[^\\|]*\\|")
              (:pattern "\\\\."))))))
  keyword (:token
           (:seq
            "#:"
            (:repeat
             (:choice
              (:pattern "[^ \\t\\n\\v\\f\\r\\u{0085}\\u{00A0}\\u{1680}\\u{2000}-\\u{200A}\\u{2028}\\u{2029}\\u{202F}\\u{205F}\\u{3000}\\u{FEFF}(){}\",'`;\\[\\]\\|\\\\]" "u")
              (:pattern "\\|[^\\|]*\\|")
              (:pattern "\\\\.")))))
  box (:seq "#&" (:repeat _skip) _datum)
  list (:choice
        (:seq "(" (:repeat (:choice _token dot)) ")")
        (:seq "[" (:repeat (:choice _token dot)) "]")
        (:seq "{" (:repeat (:choice _token dot)) "}"))
  vector (:seq (:choice "#" "#fl" "#fx") (:choice decimal :blank) list)
  structure (:seq "#s" list)
  hash (:seq
        (:token
         (:seq
          (:pattern "#[hH][aA][sS][hH]")
          (:choice
           (:choice (:pattern "[aA][lL][wW]") (:pattern "[eE][qQ][vV]") (:pattern "[eE][qQ]"))
           :blank)))
        list)
  graph (:choice
         (:token (:pattern "#[0-9]{1,8}#"))
         (:seq (:token (:pattern "#[0-9]{1,8}=")) (:repeat _skip) _datum))
  quote (:seq "'" (:repeat _skip) _datum)
  quasiquote (:seq "`" (:repeat _skip) _datum)
  syntax (:seq "#'" (:repeat _skip) _datum)
  quasisyntax (:seq "#`" (:repeat _skip) _datum)
  unquote (:seq "," (:repeat _skip) _datum)
  unquote_splicing (:seq ",@" (:repeat _skip) _datum)
  unsyntax (:seq "#," (:repeat _skip) _datum)
  unsyntax_splicing (:seq "#,@" (:repeat _skip) _datum)
  extension (:choice
             (:seq "#reader" (:repeat _skip) _datum)
             (:seq (:choice "#lang " "#!") lang_name))
  lang_name (:token
             (:choice
              (:pattern "[a-zA-Z0-9+_-]")
              (:pattern "[a-zA-Z0-9+_-][a-zA-Z0-9+_/-]*[a-zA-Z0-9+_-]")))}}
