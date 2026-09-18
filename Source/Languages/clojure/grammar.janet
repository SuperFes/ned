# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "clojure"
 :extras []
 :conflicts []
 :precedences []
 :externals []
 :inline [_kwd_leading_slash
          _kwd_just_slash
          _kwd_qualified
          _kwd_unqualified
          _kwd_marker
          _sym_qualified
          _sym_unqualified]
 :supertypes []
 :rules
 {source (:repeat (:choice _form _gap))
  _gap (:choice _ws comment dis_expr)
  _ws (:token
       (:repeat1
        (:pattern "[\\f\\n\\r\\t, \\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")))
  comment (:token (:pattern "(;|#!).*\\n?"))
  dis_expr (:seq (:field :marker "#_") (:repeat _gap) (:field :value _form))
  _form (:choice
         num_lit
         kwd_lit
         str_lit
         char_lit
         nil_lit
         bool_lit
         sym_lit
         list_lit
         map_lit
         vec_lit
         set_lit
         anon_fn_lit
         regex_lit
         read_cond_lit
         splicing_read_cond_lit
         ns_map_lit
         var_quoting_lit
         sym_val_lit
         evaling_lit
         tagged_or_ctor_lit
         derefing_lit
         quoting_lit
         syn_quoting_lit
         unquote_splicing_lit
         unquoting_lit)
  num_lit (:token
           (:prec 10
            (:seq
             (:choice (:pattern "[+-]") :blank)
             (:choice
              (:seq "0" (:pattern "[xX]") (:repeat1 (:pattern "[0-9a-fA-F]")) (:choice "N" :blank))
              (:seq "0" (:repeat1 (:pattern "[0-7]")) (:choice "N" :blank))
              (:seq
               (:repeat1 (:pattern "[0-9]"))
               (:pattern "[rR]")
               (:repeat1 (:pattern "[0-9a-zA-Z]")))
              (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
              (:seq
               (:repeat1 (:pattern "[0-9]"))
               (:choice (:seq "." (:repeat (:pattern "[0-9]"))) :blank)
               (:choice
                (:seq
                 (:pattern "[eE]")
                 (:choice (:pattern "[+-]") :blank)
                 (:repeat1 (:pattern "[0-9]")))
                :blank)
               (:choice "M" :blank))
              (:seq (:repeat1 (:pattern "[0-9]")) (:choice (:pattern "[MN]") :blank))))))
  kwd_lit (:choice _kwd_leading_slash _kwd_just_slash _kwd_qualified _kwd_unqualified)
  _kwd_leading_slash (:seq
                      (:field :marker _kwd_marker)
                      (:field :delimiter (:token "/"))
                      (:field :name
                       (:alias
                        (:token
                         (:repeat1
                          (:choice
                           (:pattern "[:'/]")
                           (:pattern "[^\\f\\n\\r\\t ()\\[\\]{}\"@~^;`\\\\,:/\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]"))))
                        kwd_name)))
  _kwd_just_slash (:seq (:field :marker _kwd_marker) (:field :name (:alias (:token "/") kwd_name)))
  _kwd_qualified (:prec 2
                  (:seq
                   (:field :marker _kwd_marker)
                   (:field :namespace
                    (:alias
                     (:token
                      (:seq
                       (:pattern "[^\\f\\n\\r\\t ()\\[\\]{}\"@~^;`\\\\,:/\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                       (:repeat
                        (:choice
                         (:pattern "[:']")
                         (:pattern "[^\\f\\n\\r\\t ()\\[\\]{}\"@~^;`\\\\,:/\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")))))
                     kwd_ns))
                   (:field :delimiter (:token "/"))
                   (:field :name
                    (:alias
                     (:token
                      (:repeat1
                       (:choice
                        (:pattern "[:'/]")
                        (:pattern "[^\\f\\n\\r\\t ()\\[\\]{}\"@~^;`\\\\,:/\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]"))))
                     kwd_name))))
  _kwd_unqualified (:prec 1
                    (:seq
                     (:field :marker _kwd_marker)
                     (:field :name
                      (:alias
                       (:token
                        (:seq
                         (:pattern "[^\\f\\n\\r\\t ()\\[\\]{}\"@~^;`\\\\,:/\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                         (:repeat
                          (:choice
                           (:pattern "[:']")
                           (:pattern "[^\\f\\n\\r\\t ()\\[\\]{}\"@~^;`\\\\,:/\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")))))
                       kwd_name))))
  _kwd_marker (:choice (:token ":") (:token "::"))
  str_lit (:token
           (:seq
            "\""
            (:repeat (:pattern "[^\"\\\\]"))
            (:repeat (:seq "\\" (:pattern ".") (:repeat (:pattern "[^\"\\\\]"))))
            "\""))
  char_lit (:token
            (:seq
             "\\"
             (:choice
              (:seq
               "o"
               (:choice
                (:seq (:pattern "[0-9]") (:pattern "[0-9]") (:pattern "[0-9]"))
                (:seq (:pattern "[0-9]") (:pattern "[0-9]"))
                (:seq (:pattern "[0-9]"))))
              (:choice "backspace" "formfeed" "newline" "return" "space" "tab")
              (:seq
               "u"
               (:pattern "[0-9a-fA-F]")
               (:pattern "[0-9a-fA-F]")
               (:pattern "[0-9a-fA-F]")
               (:pattern "[0-9a-fA-F]"))
              (:pattern ".|\\n"))))
  nil_lit (:token "nil")
  bool_lit (:token (:choice "false" "true"))
  sym_lit (:seq (:repeat _metadata_lit) (:choice _sym_qualified _sym_unqualified))
  _sym_qualified (:prec 1
                  (:seq
                   (:field :namespace
                    (:alias
                     (:token
                      (:seq
                       (:pattern "[^\\f\\n\\r\\t /()\\[\\]{}\"@~^;`\\\\,:#'0-9\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                       (:repeat
                        (:choice
                         (:pattern "[^\\f\\n\\r\\t /()\\[\\]{}\"@~^;`\\\\,:#'0-9\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                         (:pattern "[:#'0-9]")))))
                     sym_ns))
                   (:field :delimiter (:token "/"))
                   (:field :name
                    (:alias
                     (:token
                      (:repeat1
                       (:choice
                        (:pattern "[^\\f\\n\\r\\t /()\\[\\]{}\"@~^;`\\\\,:#'0-9\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                        (:pattern "[/:#'0-9]"))))
                     sym_name))))
  _sym_unqualified (:field :name
                    (:alias
                     (:choice
                      (:token "/")
                      (:token
                       (:seq
                        (:pattern "[^\\f\\n\\r\\t /()\\[\\]{}\"@~^;`\\\\,:#'0-9\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                        (:repeat
                         (:choice
                          (:pattern "[^\\f\\n\\r\\t /()\\[\\]{}\"@~^;`\\\\,:#'0-9\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                          (:pattern "[:#'0-9]"))))))
                     sym_name))
  _metadata_lit (:seq
                 (:choice (:field :meta meta_lit) (:field :old_meta old_meta_lit))
                 (:choice (:repeat _gap) :blank))
  meta_lit (:seq (:field :marker "^") (:repeat _gap) (:field :value _form))
  old_meta_lit (:seq (:field :marker "#^") (:repeat _gap) (:field :value _form))
  list_lit (:seq (:repeat _metadata_lit) _bare_list_lit)
  _bare_list_lit (:seq
                  (:field :open "(")
                  (:repeat (:choice (:field :value _form) _gap))
                  (:field :close ")"))
  map_lit (:seq (:repeat _metadata_lit) _bare_map_lit)
  _bare_map_lit (:seq
                 (:field :open "{")
                 (:repeat (:choice (:field :value _form) _gap))
                 (:field :close "}"))
  vec_lit (:seq (:repeat _metadata_lit) _bare_vec_lit)
  _bare_vec_lit (:seq
                 (:field :open "[")
                 (:repeat (:choice (:field :value _form) _gap))
                 (:field :close "]"))
  set_lit (:seq (:repeat _metadata_lit) _bare_set_lit)
  _bare_set_lit (:seq
                 (:field :marker "#")
                 (:field :open "{")
                 (:repeat (:choice (:field :value _form) _gap))
                 (:field :close "}"))
  anon_fn_lit (:seq (:repeat _metadata_lit) (:field :marker "#") _bare_list_lit)
  regex_lit (:seq
             (:field :marker "#")
             (:token
              (:seq
               "\""
               (:repeat (:pattern "[^\"\\\\]"))
               (:repeat (:seq "\\" (:pattern ".") (:repeat (:pattern "[^\"\\\\]"))))
               "\"")))
  read_cond_lit (:seq (:repeat _metadata_lit) (:field :marker "#?") (:repeat _ws) _bare_list_lit)
  splicing_read_cond_lit (:seq
                          (:repeat _metadata_lit)
                          (:field :marker "#?@")
                          (:repeat _ws)
                          _bare_list_lit)
  auto_res_mark (:token "::")
  ns_map_lit (:seq
              (:repeat _metadata_lit)
              (:field :marker "#")
              (:field :prefix (:choice auto_res_mark kwd_lit))
              (:repeat _gap)
              _bare_map_lit)
  var_quoting_lit (:seq
                   (:repeat _metadata_lit)
                   (:field :marker "#'")
                   (:repeat _gap)
                   (:field :value _form))
  sym_val_lit (:seq (:field :marker "##") (:repeat _gap) (:field :value _form))
  evaling_lit (:seq
               (:repeat _metadata_lit)
               (:field :marker "#=")
               (:repeat _gap)
               (:field :value (:choice list_lit read_cond_lit sym_lit)))
  tagged_or_ctor_lit (:seq
                      (:repeat _metadata_lit)
                      (:field :marker "#")
                      (:repeat _gap)
                      (:field :tag sym_lit)
                      (:repeat _gap)
                      (:field :value _form))
  derefing_lit (:seq
                (:repeat _metadata_lit)
                (:field :marker "@")
                (:repeat _gap)
                (:field :value _form))
  quoting_lit (:seq
               (:repeat _metadata_lit)
               (:field :marker "'")
               (:repeat _gap)
               (:field :value _form))
  syn_quoting_lit (:seq
                   (:repeat _metadata_lit)
                   (:field :marker "`")
                   (:repeat _gap)
                   (:field :value _form))
  unquote_splicing_lit (:seq
                        (:repeat _metadata_lit)
                        (:field :marker "~@")
                        (:repeat _gap)
                        (:field :value _form))
  unquoting_lit (:seq
                 (:repeat _metadata_lit)
                 (:field :marker "~")
                 (:repeat _gap)
                 (:field :value _form))}}
