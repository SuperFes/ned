# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "fennel"
 :word symbol
 :extras [(:pattern "\\s") comment]
 :conflicts [[binding _sexp]
             [binding table]
             [table_binding table]
             [sequential_table_binding sequential_table]]
 :precedences []
 :externals []
 :inline []
 :supertypes []
 :rules
 {program (:repeat _sexp)
  _sexp (:choice _special_form symbol multi_symbol list sequential_table table _literal)
  _special_form (:choice
                 fn
                 lambda
                 hashfn
                 match
                 let
                 global
                 local
                 var
                 set
                 each
                 collect
                 icollect
                 accumulate
                 for
                 quote)
  each (:seq "(" "each" "[" iter_bindings "]" (:repeat _sexp) ")")
  iter_bindings (:seq
                 (:repeat _binding)
                 (:field :iterator _sexp)
                 (:choice (:seq ":until" (:field :until _sexp)) :blank))
  for (:seq "(" "for" for_clause (:repeat _sexp) ")")
  for_clause (:seq "[" symbol _sexp _sexp (:choice _sexp :blank) "]")
  let (:seq "(" "let" let_clause (:repeat _sexp) ")")
  let_clause (:seq "[" (:repeat (:seq _binding _sexp)) "]")
  global (:seq "(" "global" _binding _sexp ")")
  local (:seq "(" "local" _binding _sexp ")")
  var (:seq "(" "var" _binding _sexp ")")
  set (:seq "(" "set" _assignment _sexp ")")
  _binding (:choice multi_value_binding _non_multi_value_binding)
  multi_value_binding (:seq "(" (:repeat _non_multi_value_binding) ")")
  _non_multi_value_binding (:choice binding sequential_table_binding table_binding)
  binding symbol
  sequential_table_binding (:seq
                            "["
                            (:repeat _non_multi_value_binding)
                            (:choice (:seq "&" binding) :blank)
                            "]")
  table_binding (:seq
                 "{"
                 (:repeat
                  (:choice
                   (:seq ":" binding)
                   (:seq "&as" binding)
                   (:seq _sexp _non_multi_value_binding)))
                 "}")
  _assignment (:choice multi_value_assignment _non_multi_value_assignment)
  multi_value_assignment (:seq "(" (:repeat _non_multi_value_assignment) ")")
  _non_multi_value_assignment (:choice assignment sequential_table_assignment table_assignment)
  assignment (:choice symbol multi_symbol)
  sequential_table_assignment (:seq "[" (:repeat _non_multi_value_assignment) "]")
  table_assignment (:seq
                    "{"
                    (:repeat
                     (:choice (:seq ":" assignment) (:seq _sexp _non_multi_value_assignment)))
                    "}")
  hashfn (:choice (:seq "(" "hashfn" (:repeat _sexp) ")") (:seq "#" _sexp))
  fn (:seq "(" "fn" _function_body ")")
  lambda (:seq "(" (:choice "lambda" "λ") _function_body ")")
  _function_body (:prec 1
                  (:seq
                   (:choice (:field :name (:choice symbol multi_symbol)) :blank)
                   parameters
                   (:choice
                    (:seq (:choice (:field :docstring string) :blank) (:repeat1 _sexp))
                    :blank)))
  parameters (:seq "[" (:repeat (:choice _binding vararg)) "]")
  match (:seq "(" "match" _sexp (:repeat (:seq _pattern _sexp)) ")")
  _pattern (:choice _simple_pattern where_pattern guard_pattern)
  _simple_pattern (:choice multi_value_pattern _non_multi_value_pattern)
  guard_pattern (:seq "(" _simple_pattern "?" (:field :guard (:repeat1 _sexp)) ")")
  where_pattern (:seq
                 "("
                 "where"
                 (:choice _simple_pattern (:seq "(" "or" (:repeat _simple_pattern) ")"))
                 (:field :guard (:repeat _sexp))
                 ")")
  multi_value_pattern (:seq "(" (:repeat _non_multi_value_pattern) ")")
  _non_multi_value_pattern (:choice
                            _literal
                            symbol
                            multi_symbol
                            sequential_table_pattern
                            table_pattern)
  sequential_table_pattern (:seq "[" (:repeat _non_multi_value_pattern) "]")
  table_pattern (:seq
                 "{"
                 (:repeat
                  (:choice (:seq ":" _simple_pattern) (:seq _sexp _non_multi_value_pattern)))
                 "}")
  collect (:seq "(" "collect" "[" iter_bindings "]" (:repeat _sexp) ")")
  icollect (:seq "(" "icollect" "[" iter_bindings "]" (:repeat _sexp) ")")
  accumulate (:seq "(" "accumulate" "[" _binding _sexp iter_bindings "]" (:repeat _sexp) ")")
  quote (:choice (:seq "(" "quote" _quoted_sexp ")") (:seq (:choice "'" "`") _quoted_sexp))
  unquote (:seq "," _sexp)
  _quoted_sexp (:choice
                unquote
                symbol
                multi_symbol
                multi_symbol_method
                quoted_list
                quoted_sequential_table
                quoted_table
                _literal)
  quoted_list (:seq "(" (:repeat _quoted_sexp) ")")
  quoted_sequential_table (:seq "[" (:repeat _quoted_sexp) "]")
  quoted_table (:seq "{" (:repeat _quoted_sexp) "}")
  list (:seq "(" (:choice _sexp multi_symbol_method) (:repeat _sexp) ")")
  sequential_table (:seq "[" (:repeat _sexp) "]")
  table_pair (:prec-left 1
              (:choice
               (:seq ":" binding)
               (:seq (:field :key string) (:field :value _sexp))
               (:seq (:field :key (:choice symbol multi_symbol)) (:field :value _sexp))))
  table (:seq "{" (:repeat table_pair) "}")
  _literal (:choice string number boolean vararg (:ref "nil") nil_safe)
  (:ref "nil") "nil"
  nil_safe "?."
  vararg "..."
  boolean (:choice "true" "false")
  string (:choice
          (:pattern ":[^(){}\\[\\]\"'~;,@`\\s]+")
          (:seq
           "\""
           (:repeat (:choice (:token-immediate (:prec 1 (:pattern "[^\"\\\\]+"))) escape_sequence))
           "\""))
  escape_sequence (:token-immediate
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^xu\\d]")
                     (:pattern "\\d{1,3}")
                     (:pattern "x[\\da-fA-F]{2}")
                     (:pattern "u{[\\da-fA-F]+}"))))
  number (:token
          (:choice
           (:seq
            (:choice (:choice "-" "+") :blank)
            (:choice
             (:pattern "\\d[_\\d]*")
             (:seq "." (:pattern "\\d[_\\d]*"))
             (:seq (:pattern "\\d[_\\d]*") "." (:choice (:pattern "\\d[_\\d]*") :blank)))
            (:choice
             (:seq (:choice "e" "E") (:choice (:choice "-" "+") :blank) (:pattern "\\d[_\\d]*"))
             :blank))
           (:seq
            (:choice (:choice "-" "+") :blank)
            (:choice "0x" "0X")
            (:choice
             (:pattern "[a-fA-F\\d][_a-fA-F\\d]*")
             (:seq "." (:pattern "[a-fA-F\\d][_a-fA-F\\d]*"))
             (:seq
              (:pattern "[a-fA-F\\d][_a-fA-F\\d]*")
              "."
              (:choice (:pattern "[a-fA-F\\d][_a-fA-F\\d]*") :blank)))
            (:choice
             (:seq
              (:choice "p" "P")
              (:choice (:choice "-" "+") :blank)
              (:pattern "[a-fA-F\\d][_a-fA-F\\d]*"))
             :blank))))
  multi_symbol (:seq
                symbol
                (:repeat1 (:seq (:token-immediate (:prec 2 ".")) (:alias symbol_immediate symbol))))
  multi_symbol_method (:seq
                       (:choice symbol multi_symbol)
                       (:token-immediate (:prec 2 ":"))
                       (:alias symbol_immediate symbol))
  symbol (:token
          (:choice
           ":"
           (:seq "." (:pattern "[^(){}\\[\\]\"'~;,@`\\s]*"))
           (:pattern "[^#(){}\\[\\]\"'~;,@`.:\\s][^(){}\\[\\]\"'~;,@`.:\\s]*")))
  symbol_immediate (:token-immediate
                    (:choice
                     ":"
                     (:seq "." (:pattern "[^(){}\\[\\]\"'~;,@`\\s]*"))
                     (:pattern "[^#(){}\\[\\]\"'~;,@`.:\\s][^(){}\\[\\]\"'~;,@`.:\\s]*")))
  comment (:token (:seq ";" (:pattern ".*")))}}
