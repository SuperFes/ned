# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "nix"
 :word keyword
 :extras [(:pattern "\\s") comment]
 :conflicts []
 :precedences []
 :externals [string_fragment
             _indented_string_fragment
             _path_start
             path_fragment
             dollar_escape
             _indented_dollar_escape]
 :inline []
 :supertypes [_expression]
 :rules
 {source_code (:choice (:field :expression _expression) :blank)
  _expression _expr_function_expression
  keyword (:pattern "if|then|else|let|inherit|in|rec|with|assert")
  identifier (:pattern "[a-zA-Z_][a-zA-Z0-9_\\'\\-]*")
  variable_expression (:field :name identifier)
  integer_expression (:pattern "[0-9]+")
  float_expression (:pattern "(([1-9][0-9]*\\.[0-9]*)|(0?\\.[0-9]+))([Ee][+-]?[0-9]+)?")
  path_expression (:seq
                   (:alias _path_start path_fragment)
                   (:repeat (:choice path_fragment (:alias _immediate_interpolation interpolation))))
  _hpath_start (:pattern "\\~\\/[a-zA-Z0-9\\._\\-\\+\\/]+")
  hpath_expression (:seq
                    (:alias _hpath_start path_fragment)
                    (:repeat
                     (:choice path_fragment (:alias _immediate_interpolation interpolation))))
  spath_expression (:pattern "<[a-zA-Z0-9\\._\\-\\+]+(\\/[a-zA-Z0-9\\._\\-\\+]+)*>")
  uri_expression (:pattern "[a-zA-Z][a-zA-Z0-9\\+\\-\\.]*:[a-zA-Z0-9%\\/\\?:@\\&=\\+\\$,\\-_\\.\\!\\~\\*\\']+")
  _expr_function_expression (:choice
                             function_expression
                             assert_expression
                             with_expression
                             let_expression
                             _expr_if)
  function_expression (:choice
                       (:seq
                        (:field :universal identifier)
                        ":"
                        (:field :body _expr_function_expression))
                       (:seq (:field :formals formals) ":" (:field :body _expr_function_expression))
                       (:seq
                        (:field :formals formals)
                        "@"
                        (:field :universal identifier)
                        ":"
                        (:field :body _expr_function_expression))
                       (:seq
                        (:field :universal identifier)
                        "@"
                        (:field :formals formals)
                        ":"
                        (:field :body _expr_function_expression)))
  formals (:choice
           (:seq "{" "}")
           (:seq
            "{"
            (:seq (:field :formal formal) (:repeat (:seq "," (:field :formal formal))))
            "}")
           (:seq
            "{"
            (:seq (:field :formal formal) (:repeat (:seq "," (:field :formal formal))))
            (:choice (:seq "," (:choice (:field :ellipses ellipses) :blank)) :blank)
            "}")
           (:seq "{" (:field :ellipses ellipses) "}"))
  formal (:seq (:field :name identifier) (:choice (:seq "?" (:field :default _expression)) :blank))
  ellipses "..."
  assert_expression (:seq
                     "assert"
                     (:field :condition _expression)
                     ";"
                     (:field :body _expr_function_expression))
  with_expression (:seq
                   "with"
                   (:field :environment _expression)
                   ";"
                   (:field :body _expr_function_expression))
  let_expression (:seq
                  "let"
                  (:choice binding_set :blank)
                  "in"
                  (:field :body _expr_function_expression))
  _expr_if (:choice if_expression _expr_op)
  if_expression (:seq
                 "if"
                 (:field :condition _expression)
                 "then"
                 (:field :consequence _expression)
                 "else"
                 (:field :alternative _expression))
  _expr_op (:choice has_attr_expression unary_expression binary_expression _expr_apply_expression)
  has_attr_expression (:prec 11
                       (:seq
                        (:field :expression _expr_op)
                        (:field :operator "?")
                        (:field :attrpath attrpath)))
  unary_expression (:choice
                    (:prec 7 (:seq (:field :operator "!") (:field :argument _expr_op)))
                    (:prec 12 (:seq (:field :operator "-") (:field :argument _expr_op))))
  binary_expression (:choice
                     (:prec-left 4
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator "==")
                       (:field :right _expr_op)))
                     (:prec-left 4
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator "!=")
                       (:field :right _expr_op)))
                     (:prec-left 5
                      (:seq (:field :left _expr_op) (:field :operator "<") (:field :right _expr_op)))
                     (:prec-left 5
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator "<=")
                       (:field :right _expr_op)))
                     (:prec-left 5
                      (:seq (:field :left _expr_op) (:field :operator ">") (:field :right _expr_op)))
                     (:prec-left 5
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator ">=")
                       (:field :right _expr_op)))
                     (:prec-left 3
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator "&&")
                       (:field :right _expr_op)))
                     (:prec-left 2
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator "||")
                       (:field :right _expr_op)))
                     (:prec-left 8
                      (:seq (:field :left _expr_op) (:field :operator "+") (:field :right _expr_op)))
                     (:prec-left 8
                      (:seq (:field :left _expr_op) (:field :operator "-") (:field :right _expr_op)))
                     (:prec-left 9
                      (:seq (:field :left _expr_op) (:field :operator "*") (:field :right _expr_op)))
                     (:prec-left 9
                      (:seq (:field :left _expr_op) (:field :operator "/") (:field :right _expr_op)))
                     (:prec-right 1
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator "->")
                       (:field :right _expr_op)))
                     (:prec-right 6
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator "//")
                       (:field :right _expr_op)))
                     (:prec-right 10
                      (:seq
                       (:field :left _expr_op)
                       (:field :operator "++")
                       (:field :right _expr_op))))
  _expr_apply_expression (:choice apply_expression _expr_select_expression)
  apply_expression (:seq
                    (:field :function _expr_apply_expression)
                    (:field :argument _expr_select_expression))
  _expr_select_expression (:choice select_expression _expr_simple)
  select_expression (:choice
                     (:seq (:field :expression _expr_simple) "." (:field :attrpath attrpath))
                     (:seq
                      (:field :expression _expr_simple)
                      "."
                      (:field :attrpath attrpath)
                      "or"
                      (:field :default _expr_select_expression)))
  _expr_simple (:choice
                variable_expression
                integer_expression
                float_expression
                string_expression
                indented_string_expression
                path_expression
                hpath_expression
                spath_expression
                uri_expression
                parenthesized_expression
                attrset_expression
                let_attrset_expression
                rec_attrset_expression
                list_expression)
  parenthesized_expression (:seq "(" (:field :expression _expression) ")")
  attrset_expression (:seq "{" (:choice binding_set :blank) "}")
  let_attrset_expression (:seq "let" "{" (:choice binding_set :blank) "}")
  rec_attrset_expression (:seq "rec" "{" (:choice binding_set :blank) "}")
  string_expression (:seq
                     "\""
                     (:repeat
                      (:choice
                       string_fragment
                       interpolation
                       (:choice escape_sequence (:seq dollar_escape (:alias "$" string_fragment)))))
                     "\"")
  escape_sequence (:token-immediate (:pattern "\\\\([^$]|\\s)"))
  indented_string_expression (:seq
                              "''"
                              (:repeat
                               (:choice
                                (:alias _indented_string_fragment string_fragment)
                                interpolation
                                (:choice
                                 (:alias _indented_escape_sequence escape_sequence)
                                 (:seq
                                  (:alias _indented_dollar_escape dollar_escape)
                                  (:alias "$" string_fragment)))))
                              "''")
  _indented_escape_sequence (:token-immediate (:pattern "'''|''\\\\([^$]|\\s)"))
  binding_set (:repeat1 (:field :binding (:choice binding inherit inherit_from)))
  binding (:seq (:field :attrpath attrpath) "=" (:field :expression _expression) ";")
  inherit (:seq "inherit" (:field :attrs inherited_attrs) ";")
  inherit_from (:seq
                "inherit"
                "("
                (:field :expression _expression)
                ")"
                (:field :attrs inherited_attrs)
                ";")
  attrpath (:seq
            (:field :attr (:choice identifier string_expression interpolation))
            (:repeat (:seq "." (:field :attr (:choice identifier string_expression interpolation)))))
  inherited_attrs (:repeat1 (:field :attr (:choice identifier string_expression interpolation)))
  _immediate_interpolation (:seq (:token-immediate "${") (:field :expression _expression) "}")
  interpolation (:seq "${" (:field :expression _expression) "}")
  list_expression (:seq "[" (:repeat (:field :element _expr_select_expression)) "]")
  comment (:token
           (:choice
            (:seq "#" (:pattern ".*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))}}
