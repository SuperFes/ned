# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "awk"
 :word identifier
 :extras [(:pattern "[\\s\\t]") "\\\n" "\\\r\n"]
 :conflicts []
 :precedences [[getline_file
                getline_input
                field_ref
                "binary_exponent"
                "binary_times"
                "binary_plus"
                unary_exp
                "binary_relation"
                "binary_match"
                _binary_in
                "binary_and"
                "binary_or"
                ternary_exp
                exp_list
                piped_io_exp
                range_pattern
                _statement]
               [func_call _exp]
               [update_exp _exp]
               [if_statement _statement_separated]
               [else_clause _statement_separated]
               [print_statement printf_statement grouping]
               [_print_args grouping "binary_relation"]
               [_print_args grouping piped_io_exp]
               [for_in_statement _exp]
               [_exp string_concat assignment_exp]
               [_print_args _binary_in]]
 :externals [concatenating_space _if_else_separator _no_space _func_call]
 :inline []
 :supertypes []
 :rules
 {program (:repeat (:choice rule func_def directive comment))
  rule (:prec-right 0
        (:choice (:seq pattern (:choice block :blank)) (:seq (:choice pattern :blank) block)))
  pattern (:prec-right 0 (:choice _exp range_pattern _special_pattern))
  range_pattern (:seq (:field :start _exp) "," (:field :stop _exp))
  _special_pattern (:choice "BEGIN" "END" "BEGINFILE" "ENDFILE")
  directive (:seq (:choice "@include" "@load" "@namespace") string)
  _statement (:prec-left 0
              (:choice
               (:seq _statement_separated _statement)
               _statement_separated
               _control_statement
               _io_statement
               _exp))
  _statement_separated (:prec-right 0 (:seq _statement (:choice ";" "\n" "\r\n")))
  _control_statement (:choice
                      if_statement
                      while_statement
                      do_while_statement
                      for_statement
                      for_in_statement
                      break_statement
                      continue_statement
                      delete_statement
                      exit_statement
                      return_statement
                      switch_statement)
  if_statement (:prec-right 0
                (:seq
                 "if"
                 (:field :condition (:seq "(" _exp ")"))
                 (:repeat comment)
                 (:choice block _statement ";")
                 (:choice (:seq _if_else_separator (:repeat comment) else_clause) :blank)))
  else_clause (:seq "else" (:choice block _statement))
  while_statement (:prec-right 0
                   (:seq
                    "while"
                    (:field :condition (:seq "(" _exp ")"))
                    (:repeat comment)
                    (:choice block _statement ";")))
  do_while_statement (:prec-right 0
                      (:seq
                       "do"
                       (:repeat comment)
                       (:choice block _statement)
                       "while"
                       (:field :condition (:seq "(" _exp ")"))))
  for_statement (:prec-right 0
                 (:seq
                  "for"
                  "("
                  (:field :initializer (:choice _exp :blank))
                  ";"
                  (:field :condition (:choice _exp :blank))
                  ";"
                  (:field :advancement (:choice _exp :blank))
                  ")"
                  (:repeat comment)
                  (:choice block _statement ";")))
  for_in_statement (:prec-right 0
                    (:seq
                     "for"
                     "("
                     (:field :left (:choice identifier ns_qualified_name))
                     "in"
                     (:field :right (:choice identifier array_ref ns_qualified_name))
                     ")"
                     (:repeat comment)
                     (:choice block _statement ";")))
  break_statement "break"
  continue_statement "continue"
  delete_statement (:seq "delete" (:choice identifier array_ref ns_qualified_name))
  exit_statement (:prec-right 0 (:seq "exit" (:choice _exp :blank)))
  return_statement (:prec-right 0 (:seq "return" (:choice _exp :blank)))
  switch_statement (:seq "switch" "(" _exp ")" (:repeat comment) switch_body)
  switch_body (:seq "{" (:repeat (:choice switch_case switch_default comment)) "}")
  switch_case (:prec-right 0
               (:seq
                "case"
                (:field :value (:choice _primitive regex))
                ":"
                (:repeat comment)
                (:choice _statement :blank)))
  switch_default (:prec-right 0 (:seq "default" ":" (:repeat comment) (:choice _statement :blank)))
  _io_statement (:choice
                 next_statement
                 nextfile_statement
                 print_statement
                 printf_statement
                 redirected_io_statement
                 piped_io_statement)
  _getline_exp (:choice getline_input getline_file)
  getline_input (:prec-right 0
                 (:seq "getline" (:choice (:choice identifier ns_qualified_name array_ref) :blank)))
  getline_file (:seq
                "getline"
                (:choice (:choice identifier ns_qualified_name) :blank)
                "<"
                (:field :filename _exp))
  next_statement "next"
  nextfile_statement "nextfile"
  _print_args (:prec-right 0 (:choice _exp exp_list))
  print_statement (:prec-right 0
                   (:seq
                    "print"
                    (:choice
                     (:choice _print_args (:seq (:token-immediate "(") _print_args ")"))
                     :blank)))
  printf_statement (:seq "printf" (:choice _print_args (:seq "(" _print_args ")")))
  redirected_io_statement (:prec-right 0
                           (:seq
                            (:choice print_statement printf_statement)
                            (:choice ">" ">>")
                            (:field :filename _exp)))
  piped_io_statement (:prec-right 0
                      (:seq
                       (:choice print_statement printf_statement)
                       (:choice "|" "|&")
                       (:field :command _exp)))
  block (:seq "{" (:repeat _block_content) "}")
  _block_content (:prec-left 0 (:choice block _statement comment))
  _exp (:choice
        identifier
        ns_qualified_name
        ternary_exp
        binary_exp
        unary_exp
        update_exp
        assignment_exp
        field_ref
        func_call
        indirect_func_call
        _primitive
        array_ref
        regex
        regex_constant
        grouping
        piped_io_exp
        string_concat
        _getline_exp)
  ternary_exp (:prec-right 0
               (:seq
                (:field :condition _exp)
                "?"
                (:choice comment :blank)
                (:field :consequence _exp)
                ":"
                (:choice comment :blank)
                (:field :alternative _exp)))
  binary_exp (:choice
              (:prec-left "binary_exponent"
               (:seq (:field :left _exp) (:field :operator "^") (:field :right _exp)))
              (:prec-left "binary_exponent"
               (:seq (:field :left _exp) (:field :operator "**") (:field :right _exp)))
              (:prec-left "binary_times"
               (:seq (:field :left _exp) (:field :operator "*") (:field :right _exp)))
              (:prec-left "binary_times"
               (:seq (:field :left _exp) (:field :operator "/") (:field :right _exp)))
              (:prec-left "binary_times"
               (:seq (:field :left _exp) (:field :operator "%") (:field :right _exp)))
              (:prec-left "binary_plus"
               (:seq (:field :left _exp) (:field :operator "+") (:field :right _exp)))
              (:prec-left "binary_plus"
               (:seq (:field :left _exp) (:field :operator "-") (:field :right _exp)))
              (:prec-left "binary_relation"
               (:seq (:field :left _exp) (:field :operator "<") (:field :right _exp)))
              (:prec-left "binary_relation"
               (:seq (:field :left _exp) (:field :operator ">") (:field :right _exp)))
              (:prec-left "binary_relation"
               (:seq (:field :left _exp) (:field :operator "<=") (:field :right _exp)))
              (:prec-left "binary_relation"
               (:seq (:field :left _exp) (:field :operator ">=") (:field :right _exp)))
              (:prec-left "binary_relation"
               (:seq (:field :left _exp) (:field :operator "==") (:field :right _exp)))
              (:prec-left "binary_relation"
               (:seq (:field :left _exp) (:field :operator "!=") (:field :right _exp)))
              (:prec-left "binary_match"
               (:seq (:field :left _exp) (:field :operator "~") (:field :right _exp)))
              (:prec-left "binary_match"
               (:seq (:field :left _exp) (:field :operator "!~") (:field :right _exp)))
              (:prec-left "binary_and"
               (:seq (:field :left _exp) (:field :operator "&&") (:field :right _exp)))
              (:prec-left "binary_or"
               (:seq (:field :left _exp) (:field :operator "||") (:field :right _exp)))
              _binary_in)
  _binary_in (:prec-left 0
              (:seq
               (:field :left (:choice (:seq "(" exp_list ")") _exp))
               (:field :operator "in")
               (:field :right _exp)))
  unary_exp (:choice
             (:seq (:field :operator "!") (:field :argument _exp))
             (:seq (:field :operator "+") (:field :argument _exp))
             (:seq (:field :operator "-") (:field :argument _exp)))
  update_exp (:prec-left 0
              (:choice
               (:seq
                (:field :argument (:choice identifier field_ref array_ref ns_qualified_name))
                (:field :operator (:choice "++" "--")))
               (:seq
                (:field :operator (:choice "++" "--"))
                (:field :argument (:choice identifier field_ref array_ref ns_qualified_name)))))
  assignment_exp (:prec-right 0
                  (:seq
                   (:field :left (:choice identifier array_ref field_ref ns_qualified_name))
                   (:choice "=" "+=" "-=" "*=" "/=" "%=" "^=")
                   (:field :right _exp)))
  piped_io_exp (:seq (:field :command _exp) (:choice "|" "|&") getline_input)
  string_concat (:prec-left 0
                 (:seq
                  (:field :left
                   (:choice
                    identifier
                    ns_qualified_name
                    ternary_exp
                    binary_exp
                    unary_exp
                    field_ref
                    func_call
                    _primitive
                    array_ref
                    grouping
                    string_concat))
                  concatenating_space
                  (:field :right
                   (:choice
                    identifier
                    ns_qualified_name
                    ternary_exp
                    binary_exp
                    unary_exp
                    field_ref
                    func_call
                    _primitive
                    array_ref
                    grouping
                    string_concat))))
  field_ref (:seq "$" _exp)
  array_ref (:seq
             (:choice identifier array_ref ns_qualified_name)
             "["
             (:field :index (:choice _exp exp_list))
             "]")
  exp_list (:seq (:repeat1 (:seq _exp ",")) _exp)
  regex (:seq
         "/"
         (:choice (:field :pattern regex_pattern) :blank)
         (:token-immediate "/")
         (:choice (:field :flags regex_flags) :blank))
  _regex_char (:token-immediate (:pattern "[^/\\\\\\[\\n\\r]"))
  _regex_char_escaped (:token-immediate (:seq "\\" (:pattern ".")))
  _regex_char_class (:seq
                     (:token-immediate "[")
                     (:token-immediate ":")
                     (:choice
                      "alnum"
                      "alpha"
                      "blank"
                      "cntrl"
                      "digit"
                      "graph"
                      "lower"
                      "print"
                      "punct"
                      "space"
                      "upper"
                      "xdigit")
                     (:token-immediate ":")
                     (:token-immediate "]"))
  _regex_bracket_exp (:seq
                      (:token-immediate "[")
                      (:repeat1 (:choice _regex_char_escaped _regex_char _regex_char_class))
                      (:token-immediate "]"))
  regex_pattern (:repeat1 (:choice _regex_char _regex_char_escaped _regex_bracket_exp))
  regex_flags (:token-immediate (:pattern "[a-z]+"))
  regex_constant (:seq "@" regex)
  grouping (:seq "(" _exp ")")
  _primitive (:choice number string)
  identifier (:pattern "[a-zA-Z_][a-zA-Z0-9_]*")
  namespace (:alias identifier "namespace")
  ns_qualified_name (:seq namespace (:token-immediate "::") _no_space identifier)
  number (:choice (:pattern "[\\d.]+") (:pattern "[\\d.]+e[\\d.+-]+"))
  string (:seq "\"" (:repeat (:choice (:pattern "[^\"\\\\]+") escape_sequence)) "\"")
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     "\""
                     (:pattern "[\\\\abfnrtv]")
                     (:pattern "x[0-9a-fA-F]{1,2}")
                     (:pattern "[0-7]{1,3}"))))
  func_def (:seq
            (:choice "function" "func")
            (:field :name (:choice identifier ns_qualified_name))
            "("
            (:choice param_list :blank)
            ")"
            block)
  param_list (:seq identifier (:repeat (:seq "," (:choice comment :blank) identifier)))
  func_call (:choice
             (:seq
              (:field :name (:choice identifier ns_qualified_name))
              _func_call
              (:token-immediate "(")
              (:choice args :blank)
              ")")
             _builtin_func_call)
  _builtin_func_call (:seq
                      (:choice
                       "and"
                       "asort"
                       "asorti"
                       "bindtextdomain"
                       "compl"
                       "cos"
                       "dcgettext"
                       "dcngettext"
                       "exp"
                       "gensub"
                       "gsub"
                       "index"
                       "int"
                       "isarray"
                       "length"
                       "log"
                       "lshift"
                       "match"
                       "mktime"
                       "or"
                       "patsplit"
                       "rand"
                       "rshift"
                       "sin"
                       "split"
                       "sprintf"
                       "sqrt"
                       "srand"
                       "strftime"
                       "strtonum"
                       "sub"
                       "substr"
                       "systime"
                       "tolower"
                       "toupper"
                       "typeof"
                       "xor")
                      "("
                      (:choice args :blank)
                      ")")
  indirect_func_call (:seq "@" func_call)
  args (:seq _exp (:repeat (:seq "," _exp)))
  comment (:seq "#" (:pattern ".*"))}}
