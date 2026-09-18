# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "typst"
 :word _identifier
 :extras [comment _sp _lb]
 :conflicts [[_math_attach_sup _math_attach_sub]]
 :precedences []
 :externals [_indent
             _dedent
             _redent
             _line_start_check
             _token_content
             _token_strong
             _token_emph
             _barrier
             _token_bracket
             _token_section
             _termination
             _token_inlined_item_end
             _token_inlined_stmt_end
             _token_blocked_expr_end
             _token_math_letter
             _token_math_ident
             _token_math_frac
             _token_math_group_end
             _token_else
             _token_unit
             _token_url
             _token_item
             _token_term
             _token_head_1
             _token_head_2
             _token_head_3
             _token_head_4
             _token_head_5
             _token_head_p
             _token_string_blob
             _token_raw_span_blob
             _token_raw_blck_blob
             _token_raw_lang
             _token_identifier
             _token_label
             _token_anti_markup
             comment
             _sp
             _immediate
             _immediate_paren
             _immediate_brack
             _immediate_ident
             _immediate_math_call
             _immediate_math_apply
             _immediate_math_field
             _immediate_math_prime
             _recovery]
 :inline [_pattern]
 :supertypes []
 :rules
 {source_file (:seq _line_start_check (:repeat (:choice _content_lb _line_content)))
  _line_content (:prec-right 0
                 (:choice (:seq (:choice section item term) (:repeat _markup)) (:repeat1 _markup)))
  parbreak (:token
            (:seq
             (:pattern "([\\n\\v\\f\\x85\\u2028\\u2029]|\\r\\n?)")
             (:repeat1
              (:seq
               (:repeat (:pattern "[\\t\\x20\\xa0\\u1680\\u2000-\\u200a\\u202f\\u205f\\u3000]"))
               (:pattern "([\\n\\v\\f\\x85\\u2028\\u2029]|\\r\\n?)")))))
  escape (:seq
          (:token
           (:choice
            (:seq (:pattern "\\\\u") (:pattern "\\{[0-9a-fA-F]*\\}"))
            (:seq
             (:pattern "\\\\")
             (:pattern "[^\\f\\r\\n\\t\\v\\x20\\x85\\xa0\\u1680\\u2000-\\u200a\\u2028\\u2029\\u202f\\u205f\\u3000]"))))
          _immediate)
  url (:seq (:pattern "http(s?):\\/\\/") _token_url)
  _lb (:pattern "([\\n\\v\\f\\x85\\u2028\\u2029]|\\r\\n?)")
  _content_lb (:seq (:choice _redent :blank) (:choice parbreak _lb) _line_start_check)
  linebreak (:pattern "\\\\")
  quote (:pattern "\"|'")
  _brackets (:seq
             (:alias _token_bracket text)
             (:seq _line_start_check (:repeat (:choice _content_lb _line_content)))
             (:alias _termination text))
  _markup (:choice
           _code
           text
           _brackets
           strong
           emph
           raw_blck
           raw_span
           math
           url
           label
           ref
           shorthand
           quote
           linebreak)
  text (:prec-right 0 (:repeat1 (:choice _token_anti_markup escape (:pattern "."))))
  _indented (:seq _indent (:repeat (:choice _content_lb _line_content)) _dedent)
  item (:seq
        (:alias _token_item "-")
        _barrier
        (:repeat _markup)
        _termination
        (:choice _indented :blank))
  term (:seq
        (:alias _token_term "/")
        (:field :term (:repeat _markup))
        ":"
        _barrier
        (:repeat _markup)
        _termination
        (:choice _indented :blank))
  _section (:seq _token_section (:repeat (:choice _content_lb _line_content)) _termination)
  section (:seq heading (:alias _section content))
  heading (:seq
           (:choice
            (:alias _token_head_1 "=")
            (:alias _token_head_2 "==")
            (:alias _token_head_3 "===")
            (:alias _token_head_4 "====")
            (:alias _token_head_5 "=====")
            (:alias _token_head_p "======"))
           _barrier
           (:repeat _markup)
           _termination)
  strong (:seq
          (:alias _token_strong "*")
          (:repeat (:choice _content_lb _line_content))
          (:alias _termination "*"))
  emph (:seq
        (:alias _token_emph "_")
        (:repeat (:choice _content_lb _line_content))
        (:alias _termination "_"))
  raw_blck (:seq
            "```"
            (:choice (:field :lang (:alias _token_raw_lang ident)) :blank)
            (:alias _token_raw_blck_blob blob)
            "```"
            _immediate)
  raw_span (:seq "`" (:alias _token_raw_span_blob blob) "`" _immediate)
  shorthand (:token (:prec 1 (:choice "--" "---" "-?" "~" "...")))
  math (:seq "$" (:choice formula :blank) "$" _immediate)
  formula (:repeat1 _math_expr)
  _math_expr (:choice
              _code
              (:alias _math_group group)
              (:alias _token_math_letter letter)
              (:alias _math_number number)
              (:alias _math_symbol symbol)
              (:alias _math_symbol symbol)
              (:alias _math_fac fac)
              (:alias _math_call call)
              (:alias _math_apply apply)
              (:alias _math_shorthand shorthand)
              _math_item
              escape
              string
              linebreak
              (:alias _math_div fraction)
              (:alias _math_root root)
              (:alias _math_prime prime)
              (:alias _math_attach_sup attach)
              (:alias _math_attach_sub attach)
              (:alias _math_token_align align))
  _math_token_align "&"
  _math_token_colon ":"
  _math_token_rpar (:token (:prec 2 ")"))
  _math_token_lpar (:token (:prec 1 "("))
  _math_token_ldlm (:token (:prec 0 (:choice "(" "[" "{" "[|")))
  _math_token_orph (:token (:prec 0 (:choice ")" "]" "}" "|]" "||" "|")))
  _math_group (:prec 1
               (:seq
                (:alias _math_token_ldlm "(")
                (:choice formula :blank)
                (:alias _token_math_group_end ")")))
  _math_item (:prec 8 (:choice (:alias _token_math_ident ident) (:alias _math_field field)))
  _math_number (:pattern "[0-9]+(\\.[0-9]+)?")
  _math_div (:prec-left 3 (:seq _math_expr (:alias _token_math_frac "/") _math_expr))
  _math_root (:prec-left 4 (:seq (:pattern "√|∛|∜") _math_expr))
  _math_fac (:prec-left 6 (:seq _math_expr "!"))
  _math_prime (:prec-left 6 (:seq _math_expr _immediate_math_prime))
  _math_attach_sup (:prec-right 5
                    (:seq
                     _math_expr
                     "^"
                     (:field :sup _math_expr)
                     (:choice (:seq "_" (:field :sub _math_expr)) :blank)))
  _math_attach_sub (:prec-right 5
                    (:seq
                     _math_expr
                     "_"
                     (:field :sub _math_expr)
                     (:choice (:seq "^" (:field :sup _math_expr)) :blank)))
  _math_field (:prec-left 9
               (:seq
                _math_item
                _immediate_math_field
                (:field :field (:alias _token_math_ident ident))))
  _math_call (:prec 8
              (:seq
               (:field :item _math_item)
               _immediate_math_call
               (:alias _math_token_lpar "(")
               (:repeat
                (:seq
                 (:choice (:alias _math_tagged tagged) (:choice formula :blank))
                 (:choice "," ";")))
               (:choice (:alias _math_tagged tagged) (:choice formula :blank))
               (:alias _math_token_rpar ")")))
  _math_tag (:prec 9 (:choice (:alias _token_math_ident ident) (:alias _token_math_letter ident)))
  _math_tagged (:prec 9 (:seq (:field :field _math_tag) _math_token_colon formula))
  _math_apply (:prec 7
               (:seq
                (:field :item
                 (:choice
                  (:alias _token_math_letter letter)
                  (:alias _math_shorthand shorthand)
                  escape
                  string
                  _math_item))
                _immediate_math_apply
                _math_group))
  _math_shorthand (:seq
                   (:token
                    (:prec 1
                     (:choice
                      "=>"
                      "->"
                      "|->"
                      "->>"
                      "-->"
                      "~>"
                      "~~>"
                      "<=="
                      "<-"
                      "<<-"
                      "<--"
                      "<->"
                      "<-->"
                      "<=>"
                      "<==>"
                      ":="
                      "::="
                      "=:"
                      "!="
                      "<="
                      "<<"
                      "<<<"
                      ">="
                      ">>"
                      ">>>"
                      "...")))
                   _immediate)
  _math_symbol (:choice _math_token_colon _math_token_orph (:token (:prec -1 (:pattern "."))))
  _code (:seq
         "#"
         (:choice
          (:seq _item (:alias _token_inlined_item_end "end"))
          (:seq _stmt (:alias _token_inlined_stmt_end "end"))))
  _item (:prec 1
         (:choice
          auto
          none
          math
          raw_span
          raw_blck
          flow
          ident
          label
          bool
          number
          string
          branch
          field
          block
          group
          call
          content
          context))
  _stmt (:choice let set import include for while show return)
  _expr (:choice
         auto
         none
         math
         raw_span
         raw_blck
         flow
         ident
         label
         bool
         number
         string
         branch
         field
         block
         group
         call
         content
         context
         elude
         assign
         lambda
         not
         or
         and
         cmp
         in
         add
         sub
         mul
         div
         sign
         let
         set
         import
         include
         for
         while
         show
         return
         tagged)
  _pattern (:choice ident group)
  _identifier (:pattern "[\\p{XID_Start}_][\\p{XID_Continue}\\-]*")
  ident (:seq _identifier _immediate)
  unit _token_unit
  bool (:choice "true" "false")
  number (:prec-right 0
          (:seq
           (:token
            (:choice
             (:pattern "0x[0-9a-fA-F]+")
             (:pattern "0o[0-7]+")
             (:pattern "0b[01]+")
             (:pattern "[0-9]+(\\.([0-9]+(e[+-]?[0-9]+)?)?)?")
             (:pattern "\\.?[0-9]+(e[+-]?[0-9]+)?")))
           _immediate
           (:choice (:seq unit _immediate) :blank)))
  string (:seq "\"" (:repeat (:choice _token_string_blob escape)) "\"" _immediate)
  tagged (:prec-left 1 (:seq (:field :field _expr) ":" _expr))
  context (:prec-left 2 (:seq "context" _expr))
  elude (:prec-left 3 (:seq ".." (:choice _expr :blank)))
  assign (:prec-right 4
          (:seq
           (:field :pattern _expr)
           (:alias (:token (:choice "=" "+=" "-=" "*=" "/=")) "assign")
           (:field :value _expr)))
  lambda (:prec-right 5 (:seq (:field :pattern _expr) "=>" (:field :value _expr)))
  or (:prec-left 6 (:seq _expr "or" _expr))
  not (:prec-left 7 (:seq "not" _expr))
  and (:prec-left 7 (:seq _expr "and" _expr))
  cmp (:prec-left 8 (:seq _expr (:choice "<" ">" "<=" ">=" "==" "!=") _expr))
  in (:prec-left 9 (:seq _expr (:choice "not" :blank) "in" _expr))
  add (:prec-left 10 (:seq _expr "+" _expr))
  sub (:prec-left 10 (:seq _expr "-" _expr))
  mul (:prec-left 11 (:seq _expr "*" _expr))
  div (:prec-left 11 (:seq _expr "/" _expr))
  sign (:prec 12 (:seq (:choice "+" "-") _expr))
  call (:prec 13
        (:seq
         (:field :item _expr)
         (:choice (:seq _immediate_brack content) (:seq _immediate_paren group))))
  field (:prec 13 (:seq _expr "." (:field :field ident)))
  label (:seq "<" _token_label ">")
  ref (:seq "@" _token_label (:choice (:seq _immediate_brack content) :blank))
  content (:seq
           (:alias _token_content "[")
           (:seq _line_start_check (:repeat (:choice _content_lb _line_content)))
           (:alias _termination "]")
           _immediate)
  group (:seq
         "("
         (:choice ":" :blank)
         (:repeat (:seq _expr ","))
         (:choice _expr :blank)
         ")"
         _immediate)
  block (:seq "{" (:repeat (:seq _expr (:alias _token_blocked_expr_end "sep"))) "}" _immediate)
  branch (:prec-right 2
          (:seq
           "if"
           (:field :condition _expr)
           (:choice block content)
           (:repeat (:choice _sp comment))
           (:choice (:seq (:alias _token_else "else") (:choice block content branch)) :blank)))
  let (:prec-right 3
       (:seq
        "let"
        (:field :pattern _expr)
        (:choice (:seq (:token (:prec 1 "=")) (:field :value _expr)) :blank)))
  set (:prec-right 1 (:seq "set" call (:choice (:seq "if" (:field :condition _expr)) :blank)))
  as (:seq "as" ident)
  import (:prec-right 1
          (:seq
           "import"
           (:field :import _expr)
           (:choice as :blank)
           (:choice
            (:seq
             (:token (:prec 10 ":"))
             (:choice
              (:alias "*" wildcard)
              (:seq
               (:repeat (:seq ident (:choice as :blank) (:token (:prec 1 ","))))
               (:choice (:seq ident (:choice as :blank)) :blank))))
            :blank)))
  include (:prec 0 (:seq "include" _expr))
  for (:seq "for" (:field :pattern _pattern) "in" (:field :value _expr) (:choice block content))
  while (:seq "while" (:field :condition _expr) (:choice block content))
  show (:seq
        "show"
        (:choice (:field :pattern _expr) :blank)
        (:token (:prec 10 ":"))
        (:field :value _expr))
  return (:prec-right 0 (:seq "return" (:choice _expr :blank)))
  flow (:choice "break" "continue")
  auto "auto"
  none "none"}}
