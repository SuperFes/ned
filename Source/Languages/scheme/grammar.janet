# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "scheme"
 :extras []
 :conflicts []
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {program (:repeat _token)
  _token (:choice _intertoken _datum)
  _intertoken (:choice
               (:token (:repeat1 (:pattern "[ \\r\\n\\t\\f\\v\\p{Zs}\\p{Zl}\\p{Zp}]")))
               directive
               comment
               block_comment)
  comment (:choice (:pattern ";.*") (:seq "#;" (:repeat _intertoken) _datum))
  directive (:seq
             "#!"
             (:repeat _intertoken)
             (:token
              (:choice
               (:repeat1
                (:pattern "[^ \\r\\n\\t\\f\\v\\p{Zs}\\p{Zl}\\p{Zp}#;\"'`,\\(\\)\\{\\}\\[\\]\\\\\\|]"))
               (:seq
                "|"
                (:repeat
                 (:choice
                  (:pattern "[^\\|\\\\]+")
                  (:pattern "\\\\[xX][0-9a-fA-F]+;")
                  (:pattern "\\\\[abtnr]")
                  "\\|"))
                "|"))))
  block_comment (:seq
                 "#|"
                 (:repeat
                  (:choice
                   (:prec 100 block_comment)
                   (:pattern ".|[\\r\\n\\u{85}\\u{2028}\\u{2029}]")))
                 (:prec 100 "|#"))
  _datum (:choice
          boolean
          character
          string
          number
          symbol
          vector
          byte_vector
          list
          quote
          quasiquote
          unquote
          unquote_splicing
          syntax
          quasisyntax
          unsyntax
          unsyntax_splicing
          keyword)
  boolean (:token
           (:choice
            (:seq "#" (:pattern "[tTfF]"))
            (:seq "#" (:pattern "[tTfF]"))
            (:seq
             "#"
             (:choice
              (:pattern "[tTfF]")
              (:pattern "[tT][rR][uU][eE]")
              (:pattern "[fF][aA][lL][sS][eE]")))))
  number (:token
          (:choice
           (:choice
            (:seq
             (:choice
              (:seq (:choice "#b" "#B") (:choice (:choice "#i" "#e" "#I" "#E") :blank))
              (:seq (:choice (:choice "#i" "#e" "#I" "#E") :blank) (:choice "#b" "#B")))
             (:choice
              (:seq
               (:choice (:pattern "[+-]") :blank)
               (:choice
                (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                (:seq
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                 "/"
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                ""))
              (:seq
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                 ""))
               "@"
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                 "")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                  (:seq
                   (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                   "/"
                   (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                  ""))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[01]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[01]")) (:repeat "#")))
                 "")
                :blank)
               "i")))
            (:seq
             (:choice
              (:seq (:choice "#o" "#O") (:choice (:choice "#i" "#e" "#I" "#E") :blank))
              (:seq (:choice (:choice "#i" "#e" "#I" "#E") :blank) (:choice "#o" "#O")))
             (:choice
              (:seq
               (:choice (:pattern "[+-]") :blank)
               (:choice
                (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                (:seq
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                 "/"
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                ""))
              (:seq
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                 ""))
               "@"
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                 "")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                   "/"
                   (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                  ""))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-7]")) (:repeat "#")))
                 "")
                :blank)
               "i")))
            (:seq
             (:choice
              (:seq
               (:choice (:choice "#d" "#D") :blank)
               (:choice (:choice "#i" "#e" "#I" "#E") :blank))
              (:seq
               (:choice (:choice "#i" "#e" "#I" "#E") :blank)
               (:choice (:choice "#d" "#D") :blank)))
             (:choice
              (:seq
               (:choice (:pattern "[+-]") :blank)
               (:choice
                (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                (:seq
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                 "/"
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                (:choice
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                  (:choice
                   (:seq
                    (:pattern "[eEsSfFdDlL]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))
                 (:seq
                  "."
                  (:repeat1 (:pattern "[0-9]"))
                  (:repeat "#")
                  (:choice
                   (:seq
                    (:pattern "[eEsSfFdDlL]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))
                 (:seq
                  (:repeat1 (:pattern "[0-9]"))
                  "."
                  (:repeat (:pattern "[0-9]"))
                  (:repeat "#")
                  (:choice
                   (:seq
                    (:pattern "[eEsSfFdDlL]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))
                 (:seq
                  (:repeat1 (:pattern "[0-9]"))
                  (:repeat1 "#")
                  "."
                  (:repeat "#")
                  (:choice
                   (:seq
                    (:pattern "[eEsSfFdDlL]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank)))))
              (:seq
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                 (:choice
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   "."
                   (:repeat1 (:pattern "[0-9]"))
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   (:repeat1 (:pattern "[0-9]"))
                   "."
                   (:repeat (:pattern "[0-9]"))
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   (:repeat1 (:pattern "[0-9]"))
                   (:repeat1 "#")
                   "."
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank)))))
               "@"
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                 (:choice
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   "."
                   (:repeat1 (:pattern "[0-9]"))
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   (:repeat1 (:pattern "[0-9]"))
                   "."
                   (:repeat (:pattern "[0-9]"))
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   (:repeat1 (:pattern "[0-9]"))
                   (:repeat1 "#")
                   "."
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                   "/"
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                  (:choice
                   (:seq
                    (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                    (:choice
                     (:seq
                      (:pattern "[eEsSfFdDlL]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    "."
                    (:repeat1 (:pattern "[0-9]"))
                    (:repeat "#")
                    (:choice
                     (:seq
                      (:pattern "[eEsSfFdDlL]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    "."
                    (:repeat (:pattern "[0-9]"))
                    (:repeat "#")
                    (:choice
                     (:seq
                      (:pattern "[eEsSfFdDlL]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    (:repeat1 "#")
                    "."
                    (:repeat "#")
                    (:choice
                     (:seq
                      (:pattern "[eEsSfFdDlL]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank)))))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#")))
                 (:choice
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9]")) (:repeat "#"))
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   "."
                   (:repeat1 (:pattern "[0-9]"))
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   (:repeat1 (:pattern "[0-9]"))
                   "."
                   (:repeat (:pattern "[0-9]"))
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   (:repeat1 (:pattern "[0-9]"))
                   (:repeat1 "#")
                   "."
                   (:repeat "#")
                   (:choice
                    (:seq
                     (:pattern "[eEsSfFdDlL]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))))
                :blank)
               "i")))
            (:seq
             (:choice
              (:seq (:choice "#x" "#X") (:choice (:choice "#i" "#e" "#I" "#E") :blank))
              (:seq (:choice (:choice "#i" "#e" "#I" "#E") :blank) (:choice "#x" "#X")))
             (:choice
              (:seq
               (:choice (:pattern "[+-]") :blank)
               (:choice
                (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                (:seq
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                 "/"
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                ""))
              (:seq
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                 ""))
               "@"
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                 "")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                  (:seq
                   (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                   "/"
                   (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                  ""))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                 (:seq
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#"))
                  "/"
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) (:repeat "#")))
                 "")
                :blank)
               "i"))))
           (:choice
            (:seq
             (:choice
              (:seq (:choice "#b" "#B") (:choice (:choice "#i" "#e" "#I" "#E") :blank))
              (:seq (:choice (:choice "#i" "#e" "#I" "#E") :blank) (:choice "#b" "#B")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:seq
                 (:choice
                  (:repeat1 (:pattern "[01]"))
                  (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                  (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
               (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[01]"))
                   (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
               "@"
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[01]"))
                   (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0"))))
              (:seq
               (:choice
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:repeat1 (:pattern "[01]"))
                    (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                    (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                 (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[01]"))
                   (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank))))
                 (:choice "nan.0" "inf.0"))
                :blank)
               "i")))
            (:seq
             (:choice
              (:seq (:choice "#o" "#O") (:choice (:choice "#i" "#e" "#I" "#E") :blank))
              (:seq (:choice (:choice "#i" "#e" "#I" "#E") :blank) (:choice "#o" "#O")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:seq
                 (:choice
                  (:repeat1 (:pattern "[0-7]"))
                  (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                  (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
               (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-7]"))
                   (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
               "@"
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-7]"))
                   (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0"))))
              (:seq
               (:choice
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:repeat1 (:pattern "[0-7]"))
                    (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                    (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                 (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-7]"))
                   (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank))))
                 (:choice "nan.0" "inf.0"))
                :blank)
               "i")))
            (:seq
             (:choice
              (:seq
               (:choice (:choice "#d" "#D") :blank)
               (:choice (:choice "#i" "#e" "#I" "#E") :blank))
              (:seq
               (:choice (:choice "#i" "#e" "#I" "#E") :blank)
               (:choice (:choice "#d" "#D") :blank)))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:seq
                 (:choice
                  (:repeat1 (:pattern "[0-9]"))
                  (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                  (:seq
                   (:choice
                    (:seq
                     (:repeat1 (:pattern "[0-9]"))
                     (:choice
                      (:seq
                       (:pattern "[eEsSfFdDlL]")
                       (:choice (:pattern "[+-]") :blank)
                       (:repeat1 (:pattern "[0-9]")))
                      :blank))
                    (:seq
                     "."
                     (:repeat1 (:pattern "[0-9]"))
                     (:choice
                      (:seq
                       (:pattern "[eEsSfFdDlL]")
                       (:choice (:pattern "[+-]") :blank)
                       (:repeat1 (:pattern "[0-9]")))
                      :blank))
                    (:seq
                     (:repeat1 (:pattern "[0-9]"))
                     "."
                     (:repeat (:pattern "[0-9]"))
                     (:choice
                      (:seq
                       (:pattern "[eEsSfFdDlL]")
                       (:choice (:pattern "[+-]") :blank)
                       (:repeat1 (:pattern "[0-9]")))
                      :blank))
                    (:seq
                     (:repeat1 (:pattern "[0-9]"))
                     "."
                     (:choice
                      (:seq
                       (:pattern "[eEsSfFdDlL]")
                       (:choice (:pattern "[+-]") :blank)
                       (:repeat1 (:pattern "[0-9]")))
                      :blank)))
                   (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
               (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-9]"))
                   (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                   (:seq
                    (:choice
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      "."
                      (:repeat1 (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      "."
                      (:repeat (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      "."
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank)))
                    (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
               "@"
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-9]"))
                   (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                   (:seq
                    (:choice
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      "."
                      (:repeat1 (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      "."
                      (:repeat (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      "."
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank)))
                    (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0"))))
              (:seq
               (:choice
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:repeat1 (:pattern "[0-9]"))
                    (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                    (:seq
                     (:choice
                      (:seq
                       (:repeat1 (:pattern "[0-9]"))
                       (:choice
                        (:seq
                         (:pattern "[eEsSfFdDlL]")
                         (:choice (:pattern "[+-]") :blank)
                         (:repeat1 (:pattern "[0-9]")))
                        :blank))
                      (:seq
                       "."
                       (:repeat1 (:pattern "[0-9]"))
                       (:choice
                        (:seq
                         (:pattern "[eEsSfFdDlL]")
                         (:choice (:pattern "[+-]") :blank)
                         (:repeat1 (:pattern "[0-9]")))
                        :blank))
                      (:seq
                       (:repeat1 (:pattern "[0-9]"))
                       "."
                       (:repeat (:pattern "[0-9]"))
                       (:choice
                        (:seq
                         (:pattern "[eEsSfFdDlL]")
                         (:choice (:pattern "[+-]") :blank)
                         (:repeat1 (:pattern "[0-9]")))
                        :blank))
                      (:seq
                       (:repeat1 (:pattern "[0-9]"))
                       "."
                       (:choice
                        (:seq
                         (:pattern "[eEsSfFdDlL]")
                         (:choice (:pattern "[+-]") :blank)
                         (:repeat1 (:pattern "[0-9]")))
                        :blank)))
                     (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                 (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-9]"))
                   (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                   (:seq
                    (:choice
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      "."
                      (:repeat1 (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      "."
                      (:repeat (:pattern "[0-9]"))
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      "."
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank)))
                    (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank))))
                 (:choice "nan.0" "inf.0"))
                :blank)
               "i")))
            (:seq
             (:choice
              (:seq (:choice "#x" "#X") (:choice (:choice "#i" "#e" "#I" "#E") :blank))
              (:seq (:choice (:choice "#i" "#e" "#I" "#E") :blank) (:choice "#x" "#X")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:seq
                 (:choice
                  (:repeat1 (:pattern "[0-9a-fA-F]"))
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                  (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
               (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-9a-fA-F]"))
                   (:seq
                    (:repeat1 (:pattern "[0-9a-fA-F]"))
                    "/"
                    (:repeat1 (:pattern "[0-9a-fA-F]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
               "@"
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-9a-fA-F]"))
                   (:seq
                    (:repeat1 (:pattern "[0-9a-fA-F]"))
                    "/"
                    (:repeat1 (:pattern "[0-9a-fA-F]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0"))))
              (:seq
               (:choice
                (:choice
                 (:seq
                  (:choice (:pattern "[+-]") :blank)
                  (:seq
                   (:choice
                    (:repeat1 (:pattern "[0-9a-fA-F]"))
                    (:seq
                     (:repeat1 (:pattern "[0-9a-fA-F]"))
                     "/"
                     (:repeat1 (:pattern "[0-9a-fA-F]")))
                    (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank)))))
                 (:seq (:pattern "[+-]") (:choice "nan.0" "inf.0")))
                :blank)
               (:pattern "[+-]")
               (:choice
                (:choice
                 (:seq
                  (:choice
                   (:repeat1 (:pattern "[0-9a-fA-F]"))
                   (:seq
                    (:repeat1 (:pattern "[0-9a-fA-F]"))
                    "/"
                    (:repeat1 (:pattern "[0-9a-fA-F]")))
                   (:seq "" (:choice (:seq "|" (:repeat1 (:pattern "[0-9]"))) :blank))))
                 (:choice "nan.0" "inf.0"))
                :blank)
               "i"))))
           (:choice
            (:seq
             (:choice
              (:seq (:pattern "#[bB]") (:choice (:pattern "#[ieIE]") :blank))
              (:seq (:choice (:pattern "#[ieIE]") :blank) (:pattern "#[bB]")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:repeat1 (:pattern "[01]"))
                 (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                 ""))
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[01]"))
                  (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               "@"
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[01]"))
                  (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[01]"))
                  (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:pattern "[+-]")
               (:choice
                (:repeat1 (:pattern "[01]"))
                (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                "")
               "i")
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[01]"))
                  (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:pattern "[+-]")
               "i")
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[01]"))
                  (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))
               "i")
              (:seq
               (:pattern "[+-]")
               (:choice
                (:repeat1 (:pattern "[01]"))
                (:seq (:repeat1 (:pattern "[01]")) "/" (:repeat1 (:pattern "[01]")))
                "")
               "i")
              (:seq
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))
               "i")
              (:seq (:pattern "[+-]") "i")))
            (:seq
             (:choice
              (:seq (:pattern "#[oO]") (:choice (:pattern "#[ieIE]") :blank))
              (:seq (:choice (:pattern "#[ieIE]") :blank) (:pattern "#[oO]")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:repeat1 (:pattern "[0-7]"))
                 (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                 ""))
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-7]"))
                  (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               "@"
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-7]"))
                  (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-7]"))
                  (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:pattern "[+-]")
               (:choice
                (:repeat1 (:pattern "[0-7]"))
                (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                "")
               "i")
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-7]"))
                  (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:pattern "[+-]")
               "i")
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-7]"))
                  (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))
               "i")
              (:seq
               (:pattern "[+-]")
               (:choice
                (:repeat1 (:pattern "[0-7]"))
                (:seq (:repeat1 (:pattern "[0-7]")) "/" (:repeat1 (:pattern "[0-7]")))
                "")
               "i")
              (:seq
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))
               "i")
              (:seq (:pattern "[+-]") "i")))
            (:seq
             (:choice
              (:seq (:choice (:pattern "#[dD]") :blank) (:choice (:pattern "#[ieIE]") :blank))
              (:seq (:choice (:pattern "#[ieIE]") :blank) (:choice (:pattern "#[dD]") :blank)))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:repeat1 (:pattern "[0-9]"))
                 (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                 (:choice
                  (:seq
                   (:repeat1 (:pattern "[0-9]"))
                   (:choice
                    (:seq
                     (:pattern "[eE]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   "."
                   (:repeat1 (:pattern "[0-9]"))
                   (:choice
                    (:seq
                     (:pattern "[eE]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank))
                  (:seq
                   (:repeat1 (:pattern "[0-9]"))
                   "."
                   (:repeat (:pattern "[0-9]"))
                   (:choice
                    (:seq
                     (:pattern "[eE]")
                     (:choice (:pattern "[+-]") :blank)
                     (:repeat1 (:pattern "[0-9]")))
                    :blank)))))
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9]"))
                  (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                  (:choice
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    "."
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    "."
                    (:repeat (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank)))))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               "@"
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9]"))
                  (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                  (:choice
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    "."
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    "."
                    (:repeat (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank)))))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9]"))
                  (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                  (:choice
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    "."
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    "."
                    (:repeat (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank)))))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:pattern "[+-]")
               (:choice
                (:repeat1 (:pattern "[0-9]"))
                (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                (:choice
                 (:seq
                  (:repeat1 (:pattern "[0-9]"))
                  (:choice
                   (:seq
                    (:pattern "[eE]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))
                 (:seq
                  "."
                  (:repeat1 (:pattern "[0-9]"))
                  (:choice
                   (:seq
                    (:pattern "[eE]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))
                 (:seq
                  (:repeat1 (:pattern "[0-9]"))
                  "."
                  (:repeat (:pattern "[0-9]"))
                  (:choice
                   (:seq
                    (:pattern "[eE]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))))
               "i")
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9]"))
                  (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                  (:choice
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    "."
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    "."
                    (:repeat (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank)))))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:pattern "[+-]")
               "i")
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9]"))
                  (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                  (:choice
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    "."
                    (:repeat1 (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank))
                   (:seq
                    (:repeat1 (:pattern "[0-9]"))
                    "."
                    (:repeat (:pattern "[0-9]"))
                    (:choice
                     (:seq
                      (:pattern "[eE]")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9]")))
                     :blank)))))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))
               "i")
              (:seq
               (:pattern "[+-]")
               (:choice
                (:repeat1 (:pattern "[0-9]"))
                (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                (:choice
                 (:seq
                  (:repeat1 (:pattern "[0-9]"))
                  (:choice
                   (:seq
                    (:pattern "[eE]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))
                 (:seq
                  "."
                  (:repeat1 (:pattern "[0-9]"))
                  (:choice
                   (:seq
                    (:pattern "[eE]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))
                 (:seq
                  (:repeat1 (:pattern "[0-9]"))
                  "."
                  (:repeat (:pattern "[0-9]"))
                  (:choice
                   (:seq
                    (:pattern "[eE]")
                    (:choice (:pattern "[+-]") :blank)
                    (:repeat1 (:pattern "[0-9]")))
                   :blank))))
               "i")
              (:seq
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))
               "i")
              (:seq (:pattern "[+-]") "i")))
            (:seq
             (:choice
              (:seq (:pattern "#[xX]") (:choice (:pattern "#[ieIE]") :blank))
              (:seq (:choice (:pattern "#[ieIE]") :blank) (:pattern "#[xX]")))
             (:choice
              (:choice
               (:seq
                (:choice (:pattern "[+-]") :blank)
                (:choice
                 (:repeat1 (:pattern "[0-9a-fA-F]"))
                 (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                 ""))
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9a-fA-F]"))
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               "@"
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9a-fA-F]"))
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))))
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9a-fA-F]"))
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:pattern "[+-]")
               (:choice
                (:repeat1 (:pattern "[0-9a-fA-F]"))
                (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                "")
               "i")
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9a-fA-F]"))
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:pattern "[+-]")
               "i")
              (:seq
               (:choice
                (:seq
                 (:choice (:pattern "[+-]") :blank)
                 (:choice
                  (:repeat1 (:pattern "[0-9a-fA-F]"))
                  (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                  ""))
                (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0")))
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))
               "i")
              (:seq
               (:pattern "[+-]")
               (:choice
                (:repeat1 (:pattern "[0-9a-fA-F]"))
                (:seq (:repeat1 (:pattern "[0-9a-fA-F]")) "/" (:repeat1 (:pattern "[0-9a-fA-F]")))
                "")
               "i")
              (:seq
               (:choice (:pattern "[+-][iI][nN][fF]\\.0") (:pattern "[+-][nN][aA][nN]\\.0"))
               "i")
              (:seq (:pattern "[+-]") "i"))))))
  character (:token
             (:choice
              (:seq
               "#\\"
               (:choice
                (:pattern "[sS][pP][aA][cC][eE]")
                (:pattern "[nN][eE][wW][lL][iI][nN][eE]")
                (:pattern ".|[\\r\\n\\u{85}\\u{2028}\\u{2029}]")))
              (:seq
               "#\\"
               (:choice
                "nul"
                "alarm"
                "backspace"
                "tab"
                "linefeed"
                "newline"
                "vtab"
                "page"
                "return"
                "esc"
                "space"
                "delete"
                (:pattern "x[0-9a-fA-F]+")
                (:pattern ".|[\\r\\n\\u{85}\\u{2028}\\u{2029}]")))
              (:seq
               "#\\"
               (:choice
                "alarm"
                "backspace"
                "delete"
                "escape"
                "newline"
                "null"
                "return"
                "space"
                "tab"
                (:pattern "[xX][0-9a-fA-F]+")
                (:pattern ".|[\\r\\n\\u{85}\\u{2028}\\u{2029}]")))
              (:seq "#\\" (:choice "bel" "ls" "nel" "rubout" "vt"))))
  string (:seq "\"" (:repeat (:choice escape_sequence (:pattern "[^\"\\\\]+"))) "\"")
  escape_sequence (:token
                   (:choice
                    (:choice "\\\"" "\\\\")
                    (:seq
                     "\\"
                     (:choice
                      (:pattern "[abtnvfr\"\\\\]")
                      (:pattern "x[0-9a-fA-F]+;")
                      (:seq
                       (:pattern "[\\t\\p{Zs}]")
                       (:pattern "[\\n\\r\\u{2028}\\u{0085}]|(\\r\\n)|(\\r\\u{0085})")
                       (:pattern "[\\t\\p{Zs}]"))))
                    (:seq
                     "\\"
                     (:choice
                      (:pattern "[abtnr\"\\\\]")
                      (:seq
                       (:repeat (:pattern "[\\t\\p{Zs}]"))
                       (:pattern "[\\n\\r\\u{2028}\\u{0085}]|(\\r\\n)|(\\r\\u{0085})")
                       (:repeat (:pattern "[\\t\\p{Zs}]")))
                      (:pattern "[xX][0-9a-fA-F]+;")))
                    (:pattern "\\\\.")))
  symbol (:token
          (:token
           (:choice
            (:repeat1
             (:pattern "[^ \\r\\n\\t\\f\\v\\p{Zs}\\p{Zl}\\p{Zp}#;\"'`,\\(\\)\\{\\}\\[\\]\\\\\\|]"))
            (:seq
             "|"
             (:repeat
              (:choice
               (:pattern "[^\\|\\\\]+")
               (:pattern "\\\\[xX][0-9a-fA-F]+;")
               (:pattern "\\\\[abtnr]")
               "\\|"))
             "|"))))
  keyword (:token
           (:seq
            "#:"
            (:token
             (:choice
              (:repeat1
               (:pattern "[^ \\r\\n\\t\\f\\v\\p{Zs}\\p{Zl}\\p{Zp}#;\"'`,\\(\\)\\{\\}\\[\\]\\\\\\|]"))
              (:seq
               "|"
               (:repeat
                (:choice
                 (:pattern "[^\\|\\\\]+")
                 (:pattern "\\\\[xX][0-9a-fA-F]+;")
                 (:pattern "\\\\[abtnr]")
                 "\\|"))
               "|")))))
  list (:choice
        (:seq "(" (:repeat _token) ")")
        (:seq "[" (:repeat _token) "]")
        (:seq "{" (:repeat _token) "}"))
  quote (:seq "'" (:repeat _intertoken) _datum)
  quasiquote (:seq "`" (:repeat _intertoken) _datum)
  syntax (:seq "#'" (:repeat _intertoken) _datum)
  quasisyntax (:seq "#`" (:repeat _intertoken) _datum)
  unquote (:seq "," (:repeat _intertoken) _datum)
  unquote_splicing (:seq ",@" (:repeat _intertoken) _datum)
  unsyntax (:seq "#," (:repeat _intertoken) _datum)
  unsyntax_splicing (:seq "#,@" (:repeat _intertoken) _datum)
  vector (:seq "#(" (:repeat _token) ")")
  byte_vector (:seq "#vu8(" (:repeat _token) ")")}}
