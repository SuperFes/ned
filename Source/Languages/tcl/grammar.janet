# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "tcl"
 :word simple_word
 :extras [(:pattern "\\s+") (:pattern "\\\\\\r?\\n")]
 :conflicts []
 :precedences []
 :externals [_concat _immediate]
 :inline [_builtin _terminator _word]
 :supertypes []
 :rules
 {source_file (:repeat (:seq (:choice _command :blank) _terminator))
  _terminator (:choice "\n" ";")
  comment (:pattern "#[^\\n]*")
  _builtin (:choice
            _conditional
            global
            namespace
            procedure
            set
            try
            foreach
            expr_cmd
            while
            catch
            regexp)
  regexp (:seq "regexp" _word_simple _concat_word (:repeat _concat_word))
  while (:seq "while" expr _word)
  expr_cmd (:seq "expr" expr)
  foreach (:seq "foreach" arguments _word_simple _word)
  global (:seq "global" (:repeat _concat_word))
  namespace (:seq "namespace" word_list)
  try (:seq
       "try"
       _word
       (:choice (:seq "on" "error" arguments _word) :blank)
       (:choice finally :blank))
  finally (:seq "finally" _word)
  _command (:choice _builtin comment command)
  command (:seq (:field :name _word) (:choice (:field :arguments word_list) :blank))
  word_list (:repeat1 _word)
  unpack "{*}"
  _word (:seq (:choice unpack :blank) (:choice braced_word _concat_word))
  _word_simple (:seq
                (:choice
                 escaped_character
                 command_substitution
                 simple_word
                 quoted_word
                 variable_substitution
                 braced_word_simple)
                (:repeat
                 (:seq
                  _concat
                  (:choice
                   escaped_character
                   command_substitution
                   simple_word
                   quoted_word
                   variable_substitution
                   braced_word_simple))))
  _concat_word (:seq
                (:choice
                 escaped_character
                 command_substitution
                 (:seq simple_word (:choice array_index :blank))
                 quoted_word
                 variable_substitution)
                (:repeat
                 (:seq
                  _concat
                  (:choice
                   escaped_character
                   command_substitution
                   (:seq simple_word (:choice array_index :blank))
                   quoted_word
                   variable_substitution))))
  _ns_delim (:token-immediate "::")
  _ident_imm (:token-immediate (:pattern "[a-zA-Z_][a-zA-Z0-9_]*"))
  _ident (:pattern "[a-zA-Z_][a-zA-Z0-9_]*")
  _id_immediate (:seq (:choice _ns_delim :blank) _ident_imm (:repeat (:seq _ns_delim _ident_imm)))
  id (:seq (:choice (:seq "::" _ident_imm) _ident) (:repeat (:seq _ns_delim _ident_imm)))
  array_index (:seq (:token-immediate "(") _word_simple ")")
  variable_substitution (:seq
                         (:choice
                          (:seq "$" (:alias _id_immediate id))
                          (:seq "$" "{" (:pattern "[^}]+") "}"))
                         (:choice array_index :blank))
  braced_word (:seq
               "{"
               (:choice
                (:seq
                 (:seq _command (:repeat (:seq (:repeat1 _terminator) _command)))
                 (:repeat _terminator))
                :blank)
               "}")
  braced_word_simple (:seq "{" (:repeat _word_simple) "}")
  set (:seq
       "set"
       (:choice (:seq id (:choice array_index :blank)) (:seq "$" "{" (:pattern "[^}]+") "}"))
       (:choice _word_simple :blank))
  procedure (:seq "proc" (:field :name _word) (:field :arguments arguments) (:field :body _word))
  _argument_word (:choice simple_word quoted_word braced_word)
  argument (:choice
            (:field :name simple_word)
            (:seq
             "{"
             (:field :name simple_word)
             (:choice (:field :default _argument_word) :blank)
             "}"))
  arguments (:choice (:seq "{" (:repeat argument) "}") simple_word)
  number (:pattern "[+-]?[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?")
  _boolean (:token
            (:choice "1" "0" (:pattern "[Tt][Rr][Uu][Ee]") (:pattern "[Ff][Aa][Ll][Ss][Ee]")))
  _expr_atom_no_brace (:choice
                       number
                       _boolean
                       (:seq simple_word "(" _expr ")")
                       command_substitution
                       quoted_word
                       variable_substitution)
  _expr (:choice
         unary_expr
         binop_expr
         ternary_expr
         escaped_character
         (:seq "(" _expr ")")
         _expr_atom_no_brace
         braced_word_simple)
  expr (:choice (:seq "{" _expr "}") _expr_atom_no_brace)
  unary_expr (:prec-left 150 (:seq (:choice "-" "+" "~" "!") _expr))
  binop_expr (:choice
              (:prec-left 140 (:seq _expr "**" _expr))
              (:prec-left 130 (:seq _expr (:choice "/" "*" "%") _expr))
              (:prec-left 120 (:seq _expr (:choice "+" "-") _expr))
              (:prec-left 110 (:seq _expr (:choice "<<" ">>") _expr))
              (:prec-left 100 (:seq _expr (:choice ">" "<" ">=" "<=") _expr))
              (:prec-left 90 (:seq _expr (:choice "==" "!=") _expr))
              (:prec-left 80 (:seq _expr (:choice "eq" "ne") _expr))
              (:prec-left 70 (:seq _expr (:choice "in" "ni") _expr))
              (:prec-left 60 (:seq _expr "&" _expr))
              (:prec-left 50 (:seq _expr "^" _expr))
              (:prec-left 40 (:seq _expr "|" _expr))
              (:prec-left 30 (:seq _expr "&&" _expr))
              (:prec-left 20 (:seq _expr "||" _expr)))
  ternary_expr (:prec-left 10 (:seq _expr "?" _expr ":" _expr))
  elseif (:seq "elseif" (:field :condition expr) (:field :consequence _word))
  else (:seq "else" (:field :consequence _word))
  if (:seq
      "if"
      (:field :condition expr)
      (:field :consequence _word)
      (:repeat (:field :alternative elseif))
      (:choice (:field :alternative else) :blank))
  _conditional (:choice if else elseif)
  catch (:seq "catch" _word (:choice _concat_word :blank))
  quoted_word (:seq
               "\""
               (:repeat
                (:choice
                 variable_substitution
                 _quoted_word_content
                 command_substitution
                 escaped_character))
               "\"")
  escaped_character (:pattern "\\\\.")
  _quoted_word_content (:token (:prec -1 (:pattern "[^$\\\\\\[\\]\"]+")))
  command_substitution (:seq "[" _command "]")
  simple_word (:pattern "[^!$\\s\\\\\\[\\]{}();\"]+")}}
