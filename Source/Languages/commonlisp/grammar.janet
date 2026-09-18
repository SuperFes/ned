# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "commonlisp"
 :inherits "clojure"
 :extras [block_comment]
 :conflicts [[for_clause_word package_lit]
             [with_clause package_lit]
             [with_clause]
             [for_clause]
             [accumulation_clause]]
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {source (:repeat (:choice _form _gap))
  _gap (:choice _ws comment dis_expr)
  _ws (:token
       (:repeat1
        (:pattern "[\\f\\n\\r\\t \\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")))
  comment (:token (:pattern "(;|#!).*\\n?"))
  dis_expr (:seq (:field :marker "#_") (:repeat _gap) (:field :value _form))
  _form (:seq
         (:choice "#" :blank)
         (:choice
          num_lit
          fancy_literal
          vec_lit
          kwd_lit
          (:alias (:seq (:field :open "#") (:choice (:pattern "\\d+[aA]") :blank) list_lit) vec_lit)
          str_lit
          self_referential_reader_macro
          char_lit
          nil_lit
          path_lit
          sym_lit
          package_lit
          list_lit
          set_lit
          read_cond_lit
          splicing_read_cond_lit
          var_quoting_lit
          quoting_lit
          syn_quoting_lit
          unquote_splicing_lit
          unquoting_lit
          include_reader_macro
          complex_num_lit
          "."))
  num_lit (:seq
           (:token
            (:seq
             (:choice (:pattern "[+-]") :blank)
             (:choice
              (:seq
               (:choice "#x" "#X")
               (:choice (:pattern "[+-]") :blank)
               (:repeat1 (:pattern "[0-9a-fA-F]")))
              (:seq
               (:choice "#o" "#O")
               (:choice (:pattern "[+-]") :blank)
               (:repeat1 (:pattern "[0-7]")))
              (:seq
               "#"
               (:repeat1 (:pattern "[0-9]"))
               (:pattern "[rR]")
               (:repeat1 (:pattern "[0-9a-zA-Z]")))
              (:seq
               (:choice "#b" "#B")
               (:choice (:pattern "[+-]") :blank)
               (:repeat1 (:pattern "[0-1]")))
              (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
              (:seq
               (:repeat1 (:pattern "[0-9]"))
               (:choice (:seq "." (:repeat (:pattern "[0-9]"))) :blank)
               (:choice
                (:seq
                 (:pattern "[eEsSfFdDlL]")
                 (:choice (:pattern "[+-]") :blank)
                 (:repeat1 (:pattern "[0-9]")))
                :blank))
              (:seq (:repeat1 (:pattern "[0-9]")) (:choice (:pattern "[MN]") :blank)))))
           (:choice (:pattern "[sSfFdDlL]") :blank))
  kwd_lit (:prec 4 (:seq (:choice ":" "::") kwd_symbol))
  str_lit (:seq
           "\""
           (:repeat
            (:choice
             (:token-immediate (:prec 1 (:pattern "[^\\\\~\"]+")))
             (:token-immediate (:seq (:pattern "\\\\.")))
             format_specifier))
           (:choice "~" :blank)
           "\"")
  char_lit (:seq "#" (:pattern "\\\\([^\\f\\n\\r\\t ()]+|[()])"))
  nil_lit (:token "nil")
  bool_lit (:token (:choice "false" "true"))
  sym_lit (:seq
           (:token
            (:seq
             (:pattern "[^:\\f\\n\\r\\t ()\\[\\]{}\"^;`\\\\,#'\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
             (:repeat
              (:choice
               (:pattern "[^:\\f\\n\\r\\t ()\\[\\]{}\"^;`\\\\,#'\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
               (:pattern "[#']"))))))
  _metadata_lit (:seq
                 (:choice (:field :meta meta_lit) (:field :old_meta old_meta_lit))
                 (:choice (:repeat _gap) :blank))
  meta_lit (:seq
            (:field :marker "^")
            (:repeat _gap)
            (:field :value (:choice read_cond_lit map_lit str_lit kwd_lit sym_lit)))
  old_meta_lit (:seq
                (:field :marker "#^")
                (:repeat _gap)
                (:field :value (:choice read_cond_lit map_lit str_lit kwd_lit sym_lit)))
  list_lit (:seq (:repeat _metadata_lit) _bare_list_lit)
  _bare_list_lit (:choice
                  (:prec 5 defun)
                  (:prec 5 loop_macro)
                  (:seq
                   (:field :open "(")
                   (:repeat (:choice (:field :value _form) _gap))
                   (:field :close ")")))
  map_lit (:seq (:repeat _metadata_lit) _bare_map_lit)
  _bare_map_lit (:seq
                 (:field :open "{")
                 (:repeat (:choice (:field :value _form) _gap))
                 (:field :close "}"))
  vec_lit (:prec 5
           (:choice
            (:seq (:field :open (:choice "#0A" "#0a")) (:choice num_lit complex_num_lit))
            (:seq (:field :open "#") (:choice array_dimension :blank) list_lit)))
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
  sym_val_lit (:seq (:field :marker "##") (:repeat _gap) (:field :value sym_lit))
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
  syn_quoting_lit (:seq (:field :marker "`") (:repeat _gap) (:field :value _form))
  unquote_splicing_lit (:seq
                        (:repeat _metadata_lit)
                        (:field :marker ",@")
                        (:repeat _gap)
                        (:field :value _form))
  unquoting_lit (:seq (:field :marker ",") (:repeat _gap) (:field :value _form))
  block_comment (:token (:seq "#|" (:repeat (:choice (:pattern "[^|]") (:pattern "\\|[^#]"))) "|#"))
  fancy_literal (:token (:seq "|" (:repeat (:pattern "[^|]")) "|"))
  defun (:prec 5
         (:seq
          (:field :open "(")
          (:choice _gap :blank)
          defun_header
          (:choice _gap :blank)
          (:repeat (:choice (:field :value _form) _gap))
          (:field :close ")")))
  _format_token (:choice
                 (:alias
                  (:token
                   (:seq
                    (:choice (:pattern "[+-]") :blank)
                    (:choice
                     (:seq
                      (:choice "#x" "#X")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-9a-fA-F]")))
                     (:seq
                      (:choice "#o" "#O")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-7]")))
                     (:seq
                      "#"
                      (:repeat1 (:pattern "[0-9]"))
                      (:pattern "[rR]")
                      (:repeat1 (:pattern "[0-9a-zA-Z]")))
                     (:seq
                      (:choice "#b" "#B")
                      (:choice (:pattern "[+-]") :blank)
                      (:repeat1 (:pattern "[0-1]")))
                     (:seq (:repeat1 (:pattern "[0-9]")) "/" (:repeat1 (:pattern "[0-9]")))
                     (:seq
                      (:repeat1 (:pattern "[0-9]"))
                      (:choice (:seq "." (:repeat (:pattern "[0-9]"))) :blank)
                      (:choice
                       (:seq
                        (:pattern "[eEsSfFdDlL]")
                        (:choice (:pattern "[+-]") :blank)
                        (:repeat1 (:pattern "[0-9]")))
                       :blank))
                     (:seq (:repeat1 (:pattern "[0-9]")) (:choice (:pattern "[MN]") :blank)))))
                  num_lit)
                 (:seq "'" (:alias (:pattern ".") char_lit)))
  format_prefix_parameters (:choice "v" "V" "#")
  format_modifiers (:seq (:repeat (:choice _format_token ",")) (:choice "@" "@:" ":" ":@"))
  format_directive_type (:choice
                         (:seq
                          (:choice (:field :repetitions _format_token) :blank)
                          (:choice "~" "%" "&" "|"))
                         (:pattern "[cC]")
                         (:pattern "\\^")
                         "\n"
                         "\r"
                         (:pattern "[pP]")
                         (:pattern "[iI]")
                         (:pattern "[wW]")
                         (:pattern "[aA]")
                         "_"
                         (:pattern "[()]")
                         (:pattern "[{}]")
                         (:pattern "[\\[\\]]")
                         (:pattern "[<>]")
                         ";"
                         (:seq (:field :numberOfArgs _format_token) "*")
                         (:seq
                          "/"
                          (:choice
                           (:alias _package_lit_without_slash package_lit)
                           _sym_lit_without_slash)
                          "/")
                         "?"
                         "Newline"
                         (:seq
                          (:repeat (:choice _format_token ","))
                          (:pattern "[$rRbBdDgGxXeEoOsStTfF]")))
  format_specifier (:prec-left 0
                    (:seq
                     "~"
                     (:choice format_prefix_parameters :blank)
                     (:choice format_modifiers :blank)
                     (:prec 5 format_directive_type)))
  for_clause_word (:seq
                   (:choice (:seq (:choice "cl" :blank) ":") :blank)
                   (:choice
                    "in"
                    "across"
                    "being"
                    "using"
                    (:pattern "being (the|each) (hash-key[s]?|hash-value[s]?|present-symbol[s]?) (in|of)")
                    "below"
                    "above"
                    "from"
                    "to"
                    "upto"
                    "upfrom"
                    "downto"
                    "downfrom"
                    "on"
                    "by"
                    "then"
                    "="))
  _for_part (:seq (:repeat _gap) for_clause_word (:repeat _gap) _form)
  accumulation_verb (:seq
                     (:choice (:seq (:choice "cl" :blank) ":") :blank)
                     (:pattern "(maximize|minimize|(collect|append|nconc|count)(ing)?|sum(ming)?|maximizing|minimizing)"))
  for_clause (:choice
              (:seq
               (:choice
                (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "for")
                (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "and")
                (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "as"))
               (:repeat _gap)
               (:field :variable _form)
               (:choice (:field :type (:seq (:repeat _gap) _form)) :blank)
               (:repeat1 _for_part))
              (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "and"))
  with_clause (:seq
               (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "with")
               (:repeat _gap)
               (:choice _form (:seq _form (:repeat _gap) (:field :type _form)))
               (:repeat _gap)
               (:choice
                (:seq (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "=") (:repeat _gap))
                :blank)
               (:choice (:seq _form (:repeat _gap)) :blank))
  do_clause (:prec-left 0
             (:seq
              (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "do")
              (:repeat1 (:prec-left 0 (:seq (:repeat _gap) _form (:repeat _gap))))))
  while_clause (:prec-left 0
                (:seq
                 (:choice
                  (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "while")
                  (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "until"))
                 (:repeat _gap)
                 _form))
  repeat_clause (:prec-left 0
                 (:seq
                  (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "repeat")
                  (:repeat _gap)
                  _form))
  condition_clause (:prec-left 0
                    (:choice
                     (:seq
                      (:choice
                       (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "when")
                       (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "if")
                       (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "unless")
                       (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "always")
                       (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "thereis")
                       (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "never"))
                      (:repeat _gap)
                      _form)
                     (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "else")))
  accumulation_clause (:seq
                       accumulation_verb
                       (:repeat _gap)
                       _form
                       (:choice
                        (:seq
                         (:repeat _gap)
                         (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "into")
                         (:repeat _gap)
                         _form)
                        :blank))
  termination_clause (:prec-left 0
                      (:seq
                       (:choice
                        (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "finally")
                        (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "return")
                        (:seq (:choice (:seq (:choice "cl" :blank) ":") :blank) "initially"))
                       (:repeat _gap)
                       _form))
  loop_clause (:seq
               (:choice
                for_clause
                do_clause
                list_lit
                while_clause
                repeat_clause
                accumulation_clause
                condition_clause
                with_clause
                termination_clause
                while_clause))
  loop_macro (:prec 5
              (:seq
               (:field :open "(")
               (:choice _gap :blank)
               (:seq (:choice (:seq "cl" ":") :blank) "loop")
               (:repeat (:choice loop_clause _gap))
               (:field :close ")")))
  defun_keyword (:prec 10
                 (:seq
                  (:choice (:seq "cl" ":") :blank)
                  (:choice "defun" "defmacro" "defgeneric" "defmethod")))
  defun_header (:prec 5
                (:choice
                 (:seq
                  (:field :keyword defun_keyword)
                  (:repeat _gap)
                  (:choice unquoting_lit unquote_splicing_lit))
                 (:seq
                  (:field :keyword defun_keyword)
                  (:repeat _gap)
                  (:field :function_name _form)
                  (:choice
                   (:field :specifier (:seq (:repeat _gap) (:choice kwd_lit sym_lit)))
                   :blank)
                  (:repeat _gap)
                  (:field :lambda_list (:choice list_lit unquoting_lit)))
                 (:seq
                  (:field :keyword (:alias "lambda" defun_keyword))
                  (:repeat _gap)
                  (:field :lambda_list (:choice list_lit unquoting_lit)))))
  array_dimension (:prec 100 (:pattern "\\d+[aA]"))
  path_lit (:prec 5
            (:seq
             (:field :open (:choice "#P" "#p"))
             (:alias
              (:token
               (:seq
                "\""
                (:repeat (:pattern "[^\"\\\\]"))
                (:repeat (:seq "\\" (:pattern ".") (:repeat (:pattern "[^\"\\\\]"))))
                "\""))
              str_lit)))
  package_lit (:prec 2
               (:choice
                (:seq
                 (:field :package (:choice sym_lit "cl"))
                 (:choice ":" "::")
                 (:field :symbol sym_lit))
                (:prec 1 "cl")))
  _package_lit_without_slash (:seq
                              (:field :package (:choice _sym_lit_without_slash "cl"))
                              (:choice ":" "::")
                              (:field :symbol _sym_lit_without_slash))
  _sym_lit_without_slash (:alias
                          (:repeat1
                           (:pattern "[^:\\f\\n\\r\\t ()\\[\\]{}\"^;/`\\\\,#'\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]"))
                          sym_lit)
  kwd_symbol (:seq
              (:token
               (:seq
                (:pattern "[^:\\f\\n\\r\\t ()\\[\\]{}\"^;`\\\\,#'\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                (:repeat
                 (:choice
                  (:pattern "[^:\\f\\n\\r\\t ()\\[\\]{}\"^;`\\\\,#'\\u000B\\u001C\\u001D\\u001E\\u001F\\u2028\\u2029\\u1680\\u2000\\u2001\\u2002\\u2003\\u2004\\u2005\\u2006\\u2008\\u2009\\u200a\\u205f\\u3000]")
                  (:pattern "[#']"))))))
  self_referential_reader_macro (:pattern "#\\d+[=#]")
  include_reader_macro (:seq
                        (:repeat _metadata_lit)
                        (:field :marker (:choice "#+" "#-"))
                        (:repeat _gap)
                        (:field :condition _form)
                        (:repeat _gap)
                        (:field :target _form))
  complex_num_lit (:seq
                   (:repeat _metadata_lit)
                   (:field :marker (:choice "#C" "#c"))
                   (:repeat _gap)
                   "("
                   (:repeat _gap)
                   (:field :real num_lit)
                   (:repeat _gap)
                   (:field :imaginary num_lit)
                   (:repeat _gap)
                   ")")}}
