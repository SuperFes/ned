# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "hcl"
 :extras [comment _whitespace]
 :conflicts []
 :precedences []
 :externals [quoted_template_start
             quoted_template_end
             _template_literal_chunk
             template_interpolation_start
             template_interpolation_end
             template_directive_start
             template_directive_end
             heredoc_identifier]
 :inline []
 :supertypes []
 :rules
 {config_file (:choice (:choice body object) :blank)
  body (:choice (:repeat1 (:choice attribute block)))
  attribute (:seq identifier "=" expression)
  block (:seq
         identifier
         (:repeat (:choice string_lit identifier))
         block_start
         (:choice body :blank)
         block_end)
  block_start "{"
  block_end "}"
  identifier (:token
              (:seq
               (:choice (:pattern "\\p{ID_Start}" "u") "_")
               (:repeat (:choice (:pattern "\\p{ID_Continue}" "u") "-" "::"))))
  expression (:prec-right 0 (:choice _expr_term conditional))
  _expr_term (:choice
              literal_value
              template_expr
              collection_value
              variable_expr
              function_call
              for_expr
              operation
              (:prec-right 8 (:seq _expr_term index))
              (:prec-right 8 (:seq _expr_term get_attr))
              (:prec-right 8 (:seq _expr_term splat))
              (:seq "(" expression ")"))
  literal_value (:choice numeric_lit bool_lit null_lit string_lit)
  numeric_lit (:choice
               (:pattern "[0-9]+(\\.[0-9]+([eE][-+]?[0-9]+)?)?")
               (:pattern "0x[0-9a-zA-Z]+"))
  bool_lit (:choice "true" "false")
  null_lit "null"
  string_lit (:prec 2
              (:seq quoted_template_start (:choice template_literal :blank) quoted_template_end))
  collection_value (:choice tuple object)
  _comma ","
  tuple (:seq tuple_start (:choice _tuple_elems :blank) tuple_end)
  tuple_start "["
  tuple_end "]"
  _tuple_elems (:seq expression (:repeat (:seq _comma expression)) (:choice _comma :blank))
  object (:seq object_start (:choice _object_elems :blank) object_end)
  object_start "{"
  object_end "}"
  _object_elems (:seq
                 object_elem
                 (:repeat (:seq (:choice _comma :blank) object_elem))
                 (:choice _comma :blank))
  object_elem (:seq (:field :key expression) (:choice "=" ":") (:field :val expression))
  index (:choice new_index legacy_index)
  new_index (:seq "[" expression "]")
  legacy_index (:seq "." (:pattern "[0-9]+"))
  get_attr (:seq "." identifier)
  splat (:choice attr_splat full_splat)
  attr_splat (:prec-right 0 (:seq ".*" (:repeat (:choice get_attr index))))
  full_splat (:prec-right 0 (:seq "[*]" (:repeat (:choice get_attr index))))
  for_expr (:choice for_tuple_expr for_object_expr)
  for_tuple_expr (:seq tuple_start for_intro expression (:choice for_cond :blank) tuple_end)
  for_object_expr (:seq
                   object_start
                   for_intro
                   expression
                   "=>"
                   expression
                   (:choice ellipsis :blank)
                   (:choice for_cond :blank)
                   object_end)
  for_intro (:seq "for" identifier (:choice (:seq "," identifier) :blank) "in" expression ":")
  for_cond (:seq "if" expression)
  variable_expr (:prec-right 0 identifier)
  function_call (:seq
                 identifier
                 _function_call_start
                 (:choice function_arguments :blank)
                 _function_call_end)
  _function_call_start "("
  _function_call_end ")"
  function_arguments (:prec-right 0
                      (:seq
                       expression
                       (:repeat (:seq _comma expression))
                       (:choice (:choice _comma ellipsis) :blank)))
  ellipsis (:token "...")
  conditional (:prec-left 0 (:seq expression "?" expression ":" expression))
  operation (:choice unary_operation binary_operation)
  unary_operation (:prec-left 7 (:seq (:choice "-" "!") _expr_term))
  binary_operation (:choice
                    (:prec-left 6 (:seq _expr_term (:choice "*" "/" "%") _expr_term))
                    (:prec-left 5 (:seq _expr_term (:choice "+" "-") _expr_term))
                    (:prec-left 4 (:seq _expr_term (:choice ">" ">=" "<" "<=") _expr_term))
                    (:prec-left 3 (:seq _expr_term (:choice "==" "!=") _expr_term))
                    (:prec-left 2 (:seq _expr_term "&&" _expr_term))
                    (:prec-left 1 (:seq _expr_term "||" _expr_term)))
  template_expr (:choice quoted_template heredoc_template)
  quoted_template (:prec 1
                   (:seq quoted_template_start (:choice _template :blank) quoted_template_end))
  heredoc_template (:seq
                    heredoc_start
                    heredoc_identifier
                    (:choice _template :blank)
                    heredoc_identifier)
  heredoc_start (:choice "<<" "<<-")
  strip_marker "~"
  _template (:repeat1 (:choice template_interpolation template_directive template_literal))
  template_literal (:prec-right 0 (:repeat1 _template_literal_chunk))
  template_interpolation (:seq
                          template_interpolation_start
                          (:choice strip_marker :blank)
                          (:choice expression :blank)
                          (:choice strip_marker :blank)
                          template_interpolation_end)
  template_directive (:choice template_for template_if)
  template_for (:seq template_for_start (:choice _template :blank) template_for_end)
  template_for_start (:seq
                      template_directive_start
                      (:choice strip_marker :blank)
                      "for"
                      identifier
                      (:choice (:seq "," identifier) :blank)
                      "in"
                      expression
                      (:choice strip_marker :blank)
                      template_directive_end)
  template_for_end (:seq
                    template_directive_start
                    (:choice strip_marker :blank)
                    "endfor"
                    (:choice strip_marker :blank)
                    template_directive_end)
  template_if (:seq
               template_if_intro
               (:choice _template :blank)
               (:choice (:seq template_else_intro (:choice _template :blank)) :blank)
               template_if_end)
  template_if_intro (:seq
                     template_directive_start
                     (:choice strip_marker :blank)
                     "if"
                     expression
                     (:choice strip_marker :blank)
                     template_directive_end)
  template_else_intro (:seq
                       template_directive_start
                       (:choice strip_marker :blank)
                       "else"
                       (:choice strip_marker :blank)
                       template_directive_end)
  template_if_end (:seq
                   template_directive_start
                   (:choice strip_marker :blank)
                   "endif"
                   (:choice strip_marker :blank)
                   template_directive_end)
  comment (:token
           (:choice
            (:seq "#" (:pattern ".*"))
            (:seq "//" (:pattern ".*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))
  _whitespace (:token (:pattern "\\s"))}}
