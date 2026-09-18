# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "org"
 :extras [(:pattern "[ \\f\\t\\v\\u00a0\\u1680\\u2000-\\u200a\\u2028\\u2029\\u202f\\u205f\\u3000\\ufeff]")]
 :conflicts [[item] [_tag_expr_start expr] [paragraph] [fndef] [expr drawer] [entry expr]]
 :precedences [["document_directive" "body_directive"] ["special" "immediate" "non-immediate"]]
 :externals [_liststart
             _listend
             _listitemend
             bullet
             _stars
             _sectionend
             _eof
             _bold_open
             _bold_close
             _italic_open
             _italic_close
             _underline_open
             _underline_close
             _verbatim_open
             _verbatim_close
             _code_open
             _code_close
             _strikethrough_open
             _strikethrough_close]
 :inline [_nl _eol _ts_contents _directive_list _body_contents]
 :supertypes []
 :rules
 {document (:seq (:choice (:field :body body) :blank) (:repeat (:field :subsection section)))
  body _body_contents
  _body_contents (:choice
                  (:repeat1 _nl)
                  (:seq (:repeat _nl) _multis)
                  (:seq
                   (:repeat _nl)
                   (:repeat1
                    (:seq
                     (:choice
                      (:seq _multis _nl)
                      (:seq (:choice (:choice paragraph fndef) :blank) _element))
                     (:repeat _nl)))
                   (:choice _multis :blank)))
  _multis (:choice paragraph _directive_list fndef)
  _element (:choice comment drawer list block dynamic_block table latex_env)
  section (:seq
           (:field :headline headline)
           (:choice (:field :plan plan) :blank)
           (:choice (:field :property_drawer property_drawer) :blank)
           (:choice (:field :body body) :blank)
           (:repeat (:field :subsection section))
           _sectionend)
  stars (:seq _stars (:pattern "\\*+"))
  headline (:seq
            (:field :stars stars)
            (:pattern "[ \\t]+")
            (:choice (:field :item item) :blank)
            (:choice (:field :tags tag_list) :blank)
            _eol)
  item (:repeat1 _inline_content)
  _inline_content (:choice expr bold italic underline verbatim code strikethrough)
  _bold_content (:choice expr italic underline strikethrough)
  _italic_content (:choice expr bold underline strikethrough)
  _underline_content (:choice expr bold italic strikethrough)
  _strikethrough_content (:choice expr bold italic underline)
  _verbatim_content expr
  _code_content expr
  bold (:seq _bold_open (:repeat1 _bold_content) _bold_close)
  italic (:seq _italic_open (:repeat1 _italic_content) _italic_close)
  underline (:seq _underline_open (:repeat1 _underline_content) _underline_close)
  verbatim (:seq _verbatim_open (:repeat1 _verbatim_content) _verbatim_close)
  code (:seq _code_open (:repeat1 _code_content) _code_close)
  strikethrough (:seq _strikethrough_open (:repeat1 _strikethrough_content) _strikethrough_close)
  tag_list (:prec-dynamic 1
            (:seq
             _tag_expr_start
             (:repeat1
              (:seq (:field :tag (:alias _noc_expr tag)) (:token-immediate (:prec "special" ":"))))))
  _tag_expr_start (:token (:prec "non-immediate" ":"))
  property_drawer (:seq
                   (:alias (:pattern ":[Pp][Rr][Oo][Pp][Ee][Rr][Tt][Ii][Ee][Ss]:") ":properties:")
                   (:repeat1 _nl)
                   (:repeat (:seq property (:repeat1 _nl)))
                   (:prec-dynamic 1 (:alias (:pattern ":[Ee][Nn][Dd]:") ":end:"))
                   _eol)
  property (:seq
            ":"
            (:field :name (:alias _immediate_expr expr))
            (:token-immediate ":")
            (:field :value (:choice (:alias _expr_line value) :blank)))
  plan (:seq (:repeat1 entry) (:prec-dynamic 1 _eol))
  entry (:seq
         (:choice
          (:seq
           (:field :name (:alias (:token (:prec "non-immediate" (:pattern "\\p{L}+"))) entry_name))
           (:token-immediate (:prec "immediate" ":")))
          :blank)
         (:field :timestamp timestamp))
  timestamp (:choice
             (:seq (:token (:prec "non-immediate" "<")) _ts_contents ">")
             (:seq (:token (:prec "non-immediate" "<")) _ts_contents ">--<" _ts_contents ">")
             (:seq (:token (:prec "non-immediate" "[")) _ts_contents "]")
             (:seq (:token (:prec "non-immediate" "[")) _ts_contents "]")
             (:seq (:token (:prec "non-immediate" "[")) _ts_contents "]--[" _ts_contents "]")
             (:seq "<%%" tsexp (:token (:prec "special" ">")))
             (:seq "[%%" tsexp (:token (:prec "special" "]"))))
  tsexp (:repeat1 (:alias _ts_expr expr))
  _ts_contents (:seq (:repeat _ts_element) (:field :date date) (:repeat _ts_element))
  date (:pattern "\\p{N}{1,4}-\\p{N}{1,4}-\\p{N}{1,4}")
  _ts_element (:choice
               (:field :day (:alias (:pattern "\\p{L}[^\\]>\\p{Z}\\t\\n\\r]*") day))
               (:field :time
                (:alias (:pattern "\\p{N}?\\p{N}[:.]\\p{N}\\p{N}( ?\\p{L}{1,2})?") time))
               (:field :duration
                (:alias
                 (:pattern "\\p{N}?\\p{N}[:.]\\p{N}\\p{N}( ?\\p{L}{1,2})?-\\p{N}?\\p{N}[:.]\\p{N}\\p{N}( ?\\p{L}{1,2})?")
                 duration))
               (:field :repeat (:alias (:pattern "[.+]?\\+\\p{N}+\\p{L}") repeat))
               (:field :delay (:alias (:pattern "--?\\p{N}+\\p{L}") delay))
               (:alias (:prec -1 (:pattern "[^\\[<\\]>\\p{Z}\\t\\n\\r]+")) expr))
  paragraph (:seq (:choice _directive_list :blank) _multiline_text)
  fndef (:seq
         (:choice _directive_list :blank)
         (:seq
          (:alias (:pattern "\\[[Ff][Nn]:") "[fn:")
          (:field :label (:alias (:pattern "[^\\p{Z}\\t\\n\\r\\]]+") expr))
          "]")
         (:field :description (:alias _multiline_text description)))
  _directive_list (:repeat1 (:field :directive directive))
  directive (:seq
             "#+"
             (:field :name (:alias _immediate_expr expr))
             (:token-immediate ":")
             (:field :value (:choice (:alias _expr_line value) :blank))
             _eol)
  comment (:prec-right 0 (:repeat1 (:seq (:pattern "#[^+\\n\\r]") (:repeat expr) _eol)))
  drawer (:seq
          (:choice _directive_list :blank)
          (:token (:prec "non-immediate" ":"))
          (:field :name (:alias _noc_expr expr))
          (:token-immediate (:prec "special" ":"))
          _nl
          (:choice (:field :contents contents) :blank)
          (:prec-dynamic 1 (:alias (:pattern ":[Ee][Nn][Dd]:") ":end:"))
          _eol)
  block (:seq
         (:choice _directive_list :blank)
         (:alias (:pattern "#\\+[Bb][Ee][Gg][Ii][Nn]_") "#+begin_")
         (:field :name expr)
         (:choice (:repeat1 (:field :parameter expr)) :blank)
         _nl
         (:choice (:field :contents contents) :blank)
         (:alias (:pattern "#\\+[Ee][Nn][Dd]_") "#+end_")
         (:field :end_name (:alias _immediate_expr expr))
         _eol)
  dynamic_block (:seq
                 (:choice _directive_list :blank)
                 (:alias (:pattern "#\\+[Bb][Ee][Gg][Ii][Nn]:") "#+begin:")
                 (:field :name expr)
                 (:repeat (:field :parameter expr))
                 _nl
                 (:choice (:field :contents contents) :blank)
                 (:alias (:pattern "#\\+[Ee][Nn][Dd]:") "#+end:")
                 (:choice (:field :end_name expr) :blank)
                 _eol)
  list (:seq
        (:choice _directive_list :blank)
        _liststart
        (:repeat (:seq listitem _listitemend (:repeat _nl)))
        (:seq listitem _listend))
  listitem (:seq
            (:field :bullet bullet)
            (:choice (:field :checkbox checkbox) :blank)
            (:choice _eof (:field :contents _body_contents)))
  checkbox (:choice
            "[ ]"
            (:seq
             (:token (:prec "non-immediate" "["))
             (:field :status (:alias _checkbox_status_expr expr))
             (:token-immediate (:prec "special" "]"))))
  table (:prec-right 0
         (:seq (:choice _directive_list :blank) (:repeat1 (:choice row hr)) (:repeat formula)))
  row (:prec 1 (:seq (:repeat1 cell) (:choice (:token (:prec 1 "|")) :blank) _eol))
  cell (:seq
        (:token (:prec 1 "|"))
        (:choice (:field :contents (:alias _expr_line contents)) :blank))
  hr (:seq
      (:token (:prec 1 "|"))
      (:repeat1 (:seq (:token-immediate (:prec 1 (:pattern "[-+]+"))) (:choice "|" :blank)))
      _eol)
  formula (:seq
           (:alias (:pattern "#\\+[Tt][Bb][Ll][Ff][Mm]:") "#+tblfm:")
           (:field :formula (:choice _expr_line :blank))
           _eol)
  latex_env (:seq
             (:choice _directive_list :blank)
             (:choice
              (:seq
               (:alias (:pattern "\\\\[Bb][Ee][Gg][Ii][Nn]\\{") "\\begin{")
               (:field :name (:alias (:pattern "[\\p{L}\\p{N}*]+") name))
               (:token-immediate "}")
               _nl
               (:choice (:field :contents contents) :blank)
               (:alias (:pattern "\\\\[Ee][Nn][Dd]\\{") "\\end{")
               (:alias (:pattern "[\\p{L}\\p{N}*]+") name)
               (:token-immediate "}"))
              (:seq
               (:token (:seq (:alias (:pattern "\\\\\\[") "\\[") (:choice "\n" "\r")))
               (:choice (:field :contents contents) :blank)
               (:alias (:pattern "\\\\\\]") "\\]"))
              (:seq
               (:token (:seq (:alias (:pattern "\\\\\\(") "\\(") (:choice "\n" "\r")))
               (:choice (:field :contents contents) :blank)
               (:alias (:pattern "\\\\\\)") "\\)")))
             _eol)
  contents (:seq
            (:choice _expr_line :blank)
            (:repeat1 _nl)
            (:repeat (:seq _expr_line (:repeat1 _nl))))
  _nl (:choice "\n" "\r")
  _eol (:choice "\n" "\r" _eof)
  _expr_line (:repeat1 _inline_content)
  _multiline_text (:repeat1 (:seq (:repeat1 _inline_content) _eol))
  _immediate_expr (:repeat1
                   (:choice
                    (:token-immediate (:prec "immediate" "!"))
                    (:token-immediate (:prec "immediate" "\""))
                    (:token-immediate (:prec "immediate" "#"))
                    (:token-immediate (:prec "immediate" "$"))
                    (:token-immediate (:prec "immediate" "%"))
                    (:token-immediate (:prec "immediate" "&"))
                    (:token-immediate (:prec "immediate" "'"))
                    (:token-immediate (:prec "immediate" "("))
                    (:token-immediate (:prec "immediate" ")"))
                    (:token-immediate (:prec "immediate" "*"))
                    (:token-immediate (:prec "immediate" "+"))
                    (:token-immediate (:prec "immediate" ","))
                    (:token-immediate (:prec "immediate" "-"))
                    (:token-immediate (:prec "immediate" "."))
                    (:token-immediate (:prec "immediate" "/"))
                    (:token-immediate (:prec "immediate" ":"))
                    (:token-immediate (:prec "immediate" ";"))
                    (:token-immediate (:prec "immediate" "<"))
                    (:token-immediate (:prec "immediate" "="))
                    (:token-immediate (:prec "immediate" ">"))
                    (:token-immediate (:prec "immediate" "?"))
                    (:token-immediate (:prec "immediate" "@"))
                    (:token-immediate (:prec "immediate" "["))
                    (:token-immediate (:prec "immediate" "]"))
                    (:token-immediate (:prec "immediate" "\\"))
                    (:token-immediate (:prec "immediate" "^"))
                    (:token-immediate (:prec "immediate" "_"))
                    (:token-immediate (:prec "immediate" "`"))
                    (:token-immediate (:prec "immediate" "{"))
                    (:token-immediate (:prec "immediate" "|"))
                    (:token-immediate (:prec "immediate" "}"))
                    (:token-immediate (:prec "immediate" "~"))
                    (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{L}+"))) "str")
                    (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{N}+"))) "num")
                    (:alias
                     (:token-immediate
                      (:prec "immediate" (:pattern "[^\\p{Z}\\p{L}\\p{N}\\t\\n\\r]")))
                     "sym")))
  _noc_expr (:repeat1
             (:choice
              (:token-immediate (:prec "immediate" "!"))
              (:token-immediate (:prec "immediate" "\""))
              (:token-immediate (:prec "immediate" "#"))
              (:token-immediate (:prec "immediate" "$"))
              (:token-immediate (:prec "immediate" "%"))
              (:token-immediate (:prec "immediate" "&"))
              (:token-immediate (:prec "immediate" "'"))
              (:token-immediate (:prec "immediate" "("))
              (:token-immediate (:prec "immediate" ")"))
              (:token-immediate (:prec "immediate" "*"))
              (:token-immediate (:prec "immediate" "+"))
              (:token-immediate (:prec "immediate" ","))
              (:token-immediate (:prec "immediate" "-"))
              (:token-immediate (:prec "immediate" "."))
              (:token-immediate (:prec "immediate" "/"))
              (:token-immediate (:prec "immediate" ";"))
              (:token-immediate (:prec "immediate" "<"))
              (:token-immediate (:prec "immediate" "="))
              (:token-immediate (:prec "immediate" ">"))
              (:token-immediate (:prec "immediate" "?"))
              (:token-immediate (:prec "immediate" "@"))
              (:token-immediate (:prec "immediate" "["))
              (:token-immediate (:prec "immediate" "]"))
              (:token-immediate (:prec "immediate" "\\"))
              (:token-immediate (:prec "immediate" "^"))
              (:token-immediate (:prec "immediate" "_"))
              (:token-immediate (:prec "immediate" "`"))
              (:token-immediate (:prec "immediate" "{"))
              (:token-immediate (:prec "immediate" "|"))
              (:token-immediate (:prec "immediate" "}"))
              (:token-immediate (:prec "immediate" "~"))
              (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{L}+"))) "str")
              (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{N}+"))) "num")
              (:alias
               (:token-immediate (:prec "immediate" (:pattern "[^\\p{Z}\\p{L}\\p{N}\\t\\n\\r]")))
               "sym")))
  _checkbox_status_expr (:choice
                         (:token-immediate (:prec "immediate" "!"))
                         (:token-immediate (:prec "immediate" "\""))
                         (:token-immediate (:prec "immediate" "#"))
                         (:token-immediate (:prec "immediate" "$"))
                         (:token-immediate (:prec "immediate" "%"))
                         (:token-immediate (:prec "immediate" "&"))
                         (:token-immediate (:prec "immediate" "'"))
                         (:token-immediate (:prec "immediate" "("))
                         (:token-immediate (:prec "immediate" ")"))
                         (:token-immediate (:prec "immediate" "*"))
                         (:token-immediate (:prec "immediate" "+"))
                         (:token-immediate (:prec "immediate" ","))
                         (:token-immediate (:prec "immediate" "-"))
                         (:token-immediate (:prec "immediate" "."))
                         (:token-immediate (:prec "immediate" "/"))
                         (:token-immediate (:prec "immediate" ":"))
                         (:token-immediate (:prec "immediate" ";"))
                         (:token-immediate (:prec "immediate" "<"))
                         (:token-immediate (:prec "immediate" "="))
                         (:token-immediate (:prec "immediate" ">"))
                         (:token-immediate (:prec "immediate" "?"))
                         (:token-immediate (:prec "immediate" "@"))
                         (:token-immediate (:prec "immediate" "["))
                         (:token-immediate (:prec "immediate" "\\"))
                         (:token-immediate (:prec "immediate" "^"))
                         (:token-immediate (:prec "immediate" "_"))
                         (:token-immediate (:prec "immediate" "`"))
                         (:token-immediate (:prec "immediate" "{"))
                         (:token-immediate (:prec "immediate" "|"))
                         (:token-immediate (:prec "immediate" "}"))
                         (:token-immediate (:prec "immediate" "~"))
                         (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{L}+"))) "str")
                         (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{N}+"))) "num")
                         (:alias
                          (:token-immediate
                           (:prec "immediate" (:pattern "[^\\p{Z}\\p{L}\\p{N}\\t\\n\\r]")))
                          "sym"))
  _ts_expr (:seq
            (:choice
             (:token (:prec "non-immediate" "!"))
             (:token (:prec "non-immediate" "\""))
             (:token (:prec "non-immediate" "#"))
             (:token (:prec "non-immediate" "$"))
             (:token (:prec "non-immediate" "%"))
             (:token (:prec "non-immediate" "&"))
             (:token (:prec "non-immediate" "'"))
             (:token (:prec "non-immediate" "("))
             (:token (:prec "non-immediate" ")"))
             (:token (:prec "non-immediate" "*"))
             (:token (:prec "non-immediate" "+"))
             (:token (:prec "non-immediate" ","))
             (:token (:prec "non-immediate" "-"))
             (:token (:prec "non-immediate" "."))
             (:token (:prec "non-immediate" "/"))
             (:token (:prec "non-immediate" ":"))
             (:token (:prec "non-immediate" ";"))
             (:token (:prec "non-immediate" "<"))
             (:token (:prec "non-immediate" "="))
             (:token (:prec "non-immediate" "?"))
             (:token (:prec "non-immediate" "@"))
             (:token (:prec "non-immediate" "["))
             (:token (:prec "non-immediate" "\\"))
             (:token (:prec "non-immediate" "^"))
             (:token (:prec "non-immediate" "_"))
             (:token (:prec "non-immediate" "`"))
             (:token (:prec "non-immediate" "{"))
             (:token (:prec "non-immediate" "|"))
             (:token (:prec "non-immediate" "}"))
             (:token (:prec "non-immediate" "~"))
             (:alias (:token (:prec "non-immediate" (:pattern "\\p{L}+"))) "str")
             (:alias (:token (:prec "non-immediate" (:pattern "\\p{N}+"))) "num")
             (:alias
              (:token (:prec "non-immediate" (:pattern "[^\\p{Z}\\p{L}\\p{N}\\t\\n\\r]")))
              "sym"))
            (:repeat
             (:choice
              (:token-immediate (:prec "immediate" "!"))
              (:token-immediate (:prec "immediate" "\""))
              (:token-immediate (:prec "immediate" "#"))
              (:token-immediate (:prec "immediate" "$"))
              (:token-immediate (:prec "immediate" "%"))
              (:token-immediate (:prec "immediate" "&"))
              (:token-immediate (:prec "immediate" "'"))
              (:token-immediate (:prec "immediate" "("))
              (:token-immediate (:prec "immediate" ")"))
              (:token-immediate (:prec "immediate" "*"))
              (:token-immediate (:prec "immediate" "+"))
              (:token-immediate (:prec "immediate" ","))
              (:token-immediate (:prec "immediate" "-"))
              (:token-immediate (:prec "immediate" "."))
              (:token-immediate (:prec "immediate" "/"))
              (:token-immediate (:prec "immediate" ":"))
              (:token-immediate (:prec "immediate" ";"))
              (:token-immediate (:prec "immediate" "<"))
              (:token-immediate (:prec "immediate" "="))
              (:token-immediate (:prec "immediate" "?"))
              (:token-immediate (:prec "immediate" "@"))
              (:token-immediate (:prec "immediate" "["))
              (:token-immediate (:prec "immediate" "\\"))
              (:token-immediate (:prec "immediate" "^"))
              (:token-immediate (:prec "immediate" "_"))
              (:token-immediate (:prec "immediate" "`"))
              (:token-immediate (:prec "immediate" "{"))
              (:token-immediate (:prec "immediate" "|"))
              (:token-immediate (:prec "immediate" "}"))
              (:token-immediate (:prec "immediate" "~"))
              (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{L}+"))) "str")
              (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{N}+"))) "num")
              (:alias
               (:token-immediate (:prec "immediate" (:pattern "[^\\p{Z}\\p{L}\\p{N}\\t\\n\\r]")))
               "sym"))))
  expr (:seq
        (:choice
         (:token (:prec "non-immediate" "!"))
         (:token (:prec "non-immediate" "\""))
         (:token (:prec "non-immediate" "#"))
         (:token (:prec "non-immediate" "$"))
         (:token (:prec "non-immediate" "%"))
         (:token (:prec "non-immediate" "&"))
         (:token (:prec "non-immediate" "'"))
         (:token (:prec "non-immediate" "("))
         (:token (:prec "non-immediate" ")"))
         (:token (:prec "non-immediate" "*"))
         (:token (:prec "non-immediate" "+"))
         (:token (:prec "non-immediate" ","))
         (:token (:prec "non-immediate" "-"))
         (:token (:prec "non-immediate" "."))
         (:token (:prec "non-immediate" "/"))
         (:token (:prec "non-immediate" ":"))
         (:token (:prec "non-immediate" ";"))
         (:token (:prec "non-immediate" "<"))
         (:token (:prec "non-immediate" "="))
         (:token (:prec "non-immediate" ">"))
         (:token (:prec "non-immediate" "?"))
         (:token (:prec "non-immediate" "@"))
         (:token (:prec "non-immediate" "["))
         (:token (:prec "non-immediate" "]"))
         (:token (:prec "non-immediate" "\\"))
         (:token (:prec "non-immediate" "^"))
         (:token (:prec "non-immediate" "_"))
         (:token (:prec "non-immediate" "`"))
         (:token (:prec "non-immediate" "{"))
         (:token (:prec "non-immediate" "|"))
         (:token (:prec "non-immediate" "}"))
         (:token (:prec "non-immediate" "~"))
         (:alias (:token (:prec "non-immediate" (:pattern "\\p{L}+"))) "str")
         (:alias (:token (:prec "non-immediate" (:pattern "\\p{N}+"))) "num")
         (:alias (:token (:prec "non-immediate" (:pattern "[^\\p{Z}\\p{L}\\p{N}\\t\\n\\r]"))) "sym"))
        (:repeat
         (:choice
          (:token-immediate (:prec "immediate" "!"))
          (:token-immediate (:prec "immediate" "\""))
          (:token-immediate (:prec "immediate" "#"))
          (:token-immediate (:prec "immediate" "$"))
          (:token-immediate (:prec "immediate" "%"))
          (:token-immediate (:prec "immediate" "&"))
          (:token-immediate (:prec "immediate" "'"))
          (:token-immediate (:prec "immediate" "("))
          (:token-immediate (:prec "immediate" ")"))
          (:token-immediate (:prec "immediate" "*"))
          (:token-immediate (:prec "immediate" "+"))
          (:token-immediate (:prec "immediate" ","))
          (:token-immediate (:prec "immediate" "-"))
          (:token-immediate (:prec "immediate" "."))
          (:token-immediate (:prec "immediate" "/"))
          (:token-immediate (:prec "immediate" ":"))
          (:token-immediate (:prec "immediate" ";"))
          (:token-immediate (:prec "immediate" "<"))
          (:token-immediate (:prec "immediate" "="))
          (:token-immediate (:prec "immediate" ">"))
          (:token-immediate (:prec "immediate" "?"))
          (:token-immediate (:prec "immediate" "@"))
          (:token-immediate (:prec "immediate" "["))
          (:token-immediate (:prec "immediate" "]"))
          (:token-immediate (:prec "immediate" "\\"))
          (:token-immediate (:prec "immediate" "^"))
          (:token-immediate (:prec "immediate" "_"))
          (:token-immediate (:prec "immediate" "`"))
          (:token-immediate (:prec "immediate" "{"))
          (:token-immediate (:prec "immediate" "|"))
          (:token-immediate (:prec "immediate" "}"))
          (:token-immediate (:prec "immediate" "~"))
          (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{L}+"))) "str")
          (:alias (:token-immediate (:prec "immediate" (:pattern "\\p{N}+"))) "num")
          (:alias
           (:token-immediate (:prec "immediate" (:pattern "[^\\p{Z}\\p{L}\\p{N}\\t\\n\\r]")))
           "sym"))))}}
