# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "ruby"
 :word identifier
 :extras [comment heredoc_body (:pattern "\\s") (:pattern "\\\\\\r?\\n")]
 :conflicts []
 :precedences []
 :externals [_line_break
             _no_line_break
             simple_symbol
             _string_start
             _symbol_start
             _subshell_start
             _regex_start
             _string_array_start
             _symbol_array_start
             _heredoc_body_start
             string_content
             heredoc_content
             _string_end
             heredoc_end
             heredoc_beginning
             "/"
             _block_ampersand
             _splat_star
             _unary_minus
             _unary_minus_num
             _binary_minus
             _binary_star
             _singleton_class_left_angle_left_langle
             hash_key_symbol
             _identifier_suffix
             _constant_suffix
             _hash_splat_star_star
             _binary_star_star
             _element_reference_bracket
             _short_interpolation]
 :inline [_arg_rhs _call_operator]
 :supertypes [_statement
              _arg
              _call_operator
              _method_name
              _expression
              _variable
              _primary
              _simple_numeric
              _lhs
              _nonlocal_variable
              _pattern_top_expr_body
              _pattern_expr
              _pattern_expr_basic
              _pattern_primitive
              _pattern_constant]
 :rules
 {program (:seq
           (:choice _statements :blank)
           (:choice (:choice (:seq (:pattern "__END__") uninterpreted)) :blank))
  uninterpreted (:pattern "(.|\\s)*")
  block_body _statements
  _statements (:choice
               (:seq
                (:repeat1 (:choice (:seq _statement _terminator) empty_statement))
                (:choice _statement :blank))
               _statement)
  begin_block (:seq "BEGIN" "{" (:choice _statements :blank) "}")
  end_block (:seq "END" "{" (:choice _statements :blank) "}")
  _statement (:choice
              undef
              alias
              if_modifier
              unless_modifier
              while_modifier
              until_modifier
              rescue_modifier
              begin_block
              end_block
              _expression)
  method (:seq "def" _method_rest)
  singleton_method (:seq
                    "def"
                    (:seq
                     (:choice (:field :object _variable) (:seq "(" (:field :object _arg) ")"))
                     (:choice "." "::"))
                    _method_rest)
  _method_rest (:seq
                (:field :name _method_name)
                (:choice
                 _body_expr
                 (:seq
                  (:field :parameters (:alias parameters method_parameters))
                  (:choice
                   (:seq
                    (:choice _terminator :blank)
                    (:choice (:field :body body_statement) :blank)
                    "end")
                   _body_expr))
                 (:seq
                  (:choice (:field :parameters (:alias bare_parameters method_parameters)) :blank)
                  _terminator
                  (:choice (:field :body body_statement) :blank)
                  "end")))
  rescue_modifier_arg (:prec 16 (:seq (:field :body _arg) "rescue" (:field :handler _arg)))
  rescue_modifier_expression (:prec 16
                              (:seq (:field :body _expression) "rescue" (:field :handler _arg)))
  _body_expr (:seq "=" (:field :body (:choice _arg (:alias rescue_modifier_arg rescue_modifier))))
  parameters (:seq
              "("
              (:choice (:seq _formal_parameter (:repeat (:seq "," _formal_parameter))) :blank)
              ")")
  bare_parameters (:seq _simple_formal_parameter (:repeat (:seq "," _formal_parameter)))
  block_parameters (:seq
                    "|"
                    (:seq
                     (:choice
                      (:seq _formal_parameter (:repeat (:seq "," _formal_parameter)))
                      :blank)
                     (:choice "," :blank))
                    (:choice
                     (:seq
                      ";"
                      (:seq
                       (:field :locals identifier)
                       (:repeat (:seq "," (:field :locals identifier)))))
                     :blank)
                    "|")
  _formal_parameter (:choice _simple_formal_parameter (:alias parameters destructured_parameter))
  _simple_formal_parameter (:choice
                            identifier
                            splat_parameter
                            hash_splat_parameter
                            hash_splat_nil
                            forward_parameter
                            block_parameter
                            keyword_parameter
                            optional_parameter)
  forward_parameter "..."
  splat_parameter (:prec-right -2 (:seq "*" (:field :name (:choice identifier :blank))))
  hash_splat_parameter (:seq "**" (:field :name (:choice identifier :blank)))
  hash_splat_nil (:seq "**" "nil")
  block_parameter (:seq "&" (:field :name (:choice identifier :blank)))
  keyword_parameter (:prec-right 51
                     (:seq
                      (:field :name identifier)
                      (:token-immediate ":")
                      (:field :value (:choice _arg :blank))))
  optional_parameter (:prec 51 (:seq (:field :name identifier) "=" (:field :value _arg)))
  class (:seq
         "class"
         (:field :name (:choice constant scope_resolution))
         (:choice (:seq (:field :superclass superclass) _terminator) (:choice _terminator :blank))
         (:choice (:field :body body_statement) :blank)
         "end")
  superclass (:seq "<" _expression)
  singleton_class (:seq
                   "class"
                   (:alias _singleton_class_left_angle_left_langle "<<")
                   (:field :value _arg)
                   _terminator
                   (:choice (:field :body body_statement) :blank)
                   "end")
  module (:seq
          "module"
          (:field :name (:choice constant scope_resolution))
          (:choice _terminator :blank)
          (:choice (:field :body body_statement) :blank)
          "end")
  return_command (:prec-left 0 (:seq "return" (:alias command_argument_list argument_list)))
  yield_command (:prec-left 0 (:seq "yield" (:alias command_argument_list argument_list)))
  break_command (:prec-left 0 (:seq "break" (:alias command_argument_list argument_list)))
  next_command (:prec-left 0 (:seq "next" (:alias command_argument_list argument_list)))
  return (:prec-left 0 (:seq "return" (:choice argument_list :blank)))
  yield (:prec-left 0 (:seq "yield" (:choice argument_list :blank)))
  break (:prec-left 0 (:seq "break" (:choice argument_list :blank)))
  next (:prec-left 0 (:seq "next" (:choice argument_list :blank)))
  redo (:prec-left 0 (:seq "redo" (:choice argument_list :blank)))
  retry (:prec-left 0 (:seq "retry" (:choice argument_list :blank)))
  if_modifier (:prec 16 (:seq (:field :body _statement) "if" (:field :condition _expression)))
  unless_modifier (:prec 16
                   (:seq (:field :body _statement) "unless" (:field :condition _expression)))
  while_modifier (:prec 16 (:seq (:field :body _statement) "while" (:field :condition _expression)))
  until_modifier (:prec 16 (:seq (:field :body _statement) "until" (:field :condition _expression)))
  rescue_modifier (:prec 16 (:seq (:field :body _statement) "rescue" (:field :handler _expression)))
  while (:seq "while" (:field :condition _statement) (:field :body do))
  until (:seq "until" (:field :condition _statement) (:field :body do))
  for (:seq
       "for"
       (:field :pattern (:choice _lhs left_assignment_list))
       (:field :value in)
       (:field :body do))
  in (:seq "in" _arg)
  do (:seq (:choice "do" _terminator) (:choice _statements :blank) "end")
  case (:seq
        "case"
        (:choice (:seq (:choice _line_break :blank) (:field :value _statement)) :blank)
        (:choice _terminator :blank)
        (:repeat when)
        (:choice else :blank)
        "end")
  case_match (:seq
              "case"
              (:seq (:choice _line_break :blank) (:field :value _statement))
              (:choice _terminator :blank)
              (:repeat1 (:field :clauses in_clause))
              (:choice (:field :else else) :blank)
              "end")
  when (:seq
        "when"
        (:seq (:field :pattern pattern) (:repeat (:seq "," (:field :pattern pattern))))
        (:choice _terminator (:field :body then)))
  in_clause (:seq
             "in"
             (:field :pattern _pattern_top_expr_body)
             (:field :guard (:choice _guard :blank))
             (:choice _terminator (:field :body then)))
  pattern (:choice _arg splat_argument)
  _guard (:choice if_guard unless_guard)
  if_guard (:seq "if" (:field :condition _expression))
  unless_guard (:seq "unless" (:field :condition _expression))
  _pattern_top_expr_body (:prec -1
                          (:choice
                           _pattern_expr
                           (:alias _array_pattern_n array_pattern)
                           (:alias _find_pattern_body find_pattern)
                           (:alias _hash_pattern_body hash_pattern)))
  _array_pattern_n (:prec-right 0
                    (:choice
                     (:seq _pattern_expr (:alias "," splat_parameter))
                     (:seq _pattern_expr "," (:choice _pattern_expr _array_pattern_n))
                     (:seq splat_parameter (:repeat (:seq "," _pattern_expr)))))
  _pattern_expr (:choice as_pattern _pattern_expr_alt)
  as_pattern (:seq (:field :value _pattern_expr) "=>" (:field :name identifier))
  _pattern_expr_alt (:choice alternative_pattern _pattern_expr_basic)
  alternative_pattern (:seq
                       (:field :alternatives _pattern_expr_basic)
                       (:repeat1 (:seq "|" (:field :alternatives _pattern_expr_basic))))
  _array_pattern_body (:choice _pattern_expr _array_pattern_n)
  array_pattern (:prec-right -1
                 (:choice
                  (:seq "[" (:choice _array_pattern_body :blank) "]")
                  (:seq
                   (:field :class _pattern_constant)
                   (:token-immediate "[")
                   (:choice _array_pattern_body :blank)
                   "]")
                  (:seq
                   (:field :class _pattern_constant)
                   (:token-immediate "(")
                   (:choice _array_pattern_body :blank)
                   ")")))
  _find_pattern_body (:seq splat_parameter (:repeat1 (:seq "," _pattern_expr)) "," splat_parameter)
  find_pattern (:choice
                (:seq "[" _find_pattern_body "]")
                (:seq
                 (:field :class _pattern_constant)
                 (:token-immediate "[")
                 _find_pattern_body
                 "]")
                (:seq
                 (:field :class _pattern_constant)
                 (:token-immediate "(")
                 _find_pattern_body
                 ")"))
  _hash_pattern_body (:prec-right 0
                      (:choice
                       (:seq
                        (:seq keyword_pattern (:repeat (:seq "," keyword_pattern)))
                        (:choice "," :blank))
                       (:seq
                        (:seq keyword_pattern (:repeat (:seq "," keyword_pattern)))
                        ","
                        _hash_pattern_any_rest)
                       _hash_pattern_any_rest))
  keyword_pattern (:prec-right -1
                   (:seq
                    (:field :key
                     (:choice
                      (:alias identifier hash_key_symbol)
                      (:alias constant hash_key_symbol)
                      (:alias identifier_suffix hash_key_symbol)
                      (:alias constant_suffix hash_key_symbol)
                      string))
                    (:token-immediate ":")
                    (:choice (:field :value _pattern_expr) :blank)))
  _hash_pattern_any_rest (:choice hash_splat_parameter hash_splat_nil)
  hash_pattern (:prec-right -1
                (:choice
                 (:seq "{" (:choice _hash_pattern_body :blank) "}")
                 (:seq
                  (:field :class _pattern_constant)
                  (:token-immediate "[")
                  _hash_pattern_body
                  "]")
                 (:seq
                  (:field :class _pattern_constant)
                  (:token-immediate "(")
                  _hash_pattern_body
                  ")")))
  _pattern_expr_basic (:prec-right -1
                       (:choice
                        _pattern_value
                        identifier
                        array_pattern
                        find_pattern
                        hash_pattern
                        parenthesized_pattern))
  parenthesized_pattern (:seq "(" _pattern_expr ")")
  _pattern_value (:prec-right -1
                  (:choice
                   _pattern_primitive
                   (:alias _pattern_range range)
                   variable_reference_pattern
                   expression_reference_pattern
                   _pattern_constant))
  _pattern_range (:choice
                  (:seq
                   (:field :begin _pattern_primitive)
                   (:field :operator (:choice ".." "..."))
                   (:field :end _pattern_primitive))
                  (:seq (:field :operator (:choice ".." "...")) (:field :end _pattern_primitive))
                  (:seq (:field :begin _pattern_primitive) (:field :operator (:choice ".." "..."))))
  _pattern_primitive (:choice _pattern_literal _pattern_lambda)
  _pattern_lambda (:prec-right -1 lambda)
  _pattern_literal (:prec-right -1
                    (:choice
                     _literal
                     string
                     subshell
                     heredoc_beginning
                     regex
                     string_array
                     symbol_array
                     _keyword_variable))
  _keyword_variable (:prec-right -1
                     (:choice (:ref "nil") self (:ref "true") (:ref "false") line file encoding))
  line "__LINE__"
  file "__FILE__"
  encoding "__ENCODING__"
  variable_reference_pattern (:seq "^" (:field :name (:choice identifier _nonlocal_variable)))
  expression_reference_pattern (:seq "^" "(" (:field :value _expression) ")")
  _pattern_constant (:prec-right -1
                     (:choice constant (:alias _pattern_constant_resolution scope_resolution)))
  _pattern_constant_resolution (:seq
                                (:choice (:field :scope _pattern_constant) :blank)
                                "::"
                                (:field :name constant))
  if (:seq
      "if"
      (:field :condition _statement)
      (:choice _terminator (:field :consequence then))
      (:field :alternative (:choice (:choice else elsif) :blank))
      "end")
  unless (:seq
          "unless"
          (:field :condition _statement)
          (:choice _terminator (:field :consequence then))
          (:field :alternative (:choice (:choice else elsif) :blank))
          "end")
  elsif (:seq
         "elsif"
         (:field :condition _statement)
         (:choice _terminator (:field :consequence then))
         (:field :alternative (:choice (:choice else elsif) :blank)))
  else (:seq "else" (:choice _terminator :blank) (:choice _statements :blank))
  then (:choice
        (:seq _terminator _statements)
        (:seq (:choice _terminator :blank) "then" (:choice _statements :blank)))
  begin (:seq "begin" (:choice _terminator :blank) (:choice _body_statement :blank) "end")
  ensure (:seq "ensure" (:choice _statements :blank))
  rescue (:seq
          "rescue"
          (:field :exceptions (:choice exceptions :blank))
          (:field :variable (:choice exception_variable :blank))
          (:choice _terminator (:field :body then)))
  exceptions (:seq (:choice _arg splat_argument) (:repeat (:seq "," (:choice _arg splat_argument))))
  exception_variable (:seq "=>" _lhs)
  body_statement _body_statement
  _body_statement (:choice
                   (:seq _statements (:repeat (:choice rescue else ensure)))
                   (:seq (:choice _statements :blank) (:repeat1 (:choice rescue else ensure))))
  _expression (:choice
               (:alias command_binary binary)
               (:alias command_unary unary)
               (:alias command_assignment assignment)
               (:alias command_operator_assignment operator_assignment)
               (:alias command_call call)
               (:alias command_call_with_block call)
               (:prec-left 0 (:alias _chained_command_call call))
               (:alias return_command return)
               (:alias yield_command yield)
               (:alias break_command break)
               (:alias next_command next)
               match_pattern
               test_pattern
               _arg)
  match_pattern (:prec 100
                 (:seq (:field :value _arg) "=>" (:field :pattern _pattern_top_expr_body)))
  test_pattern (:prec 100 (:seq (:field :value _arg) "in" (:field :pattern _pattern_top_expr_body)))
  _arg (:choice
        (:alias _unary_minus_pow unary)
        _primary
        assignment
        operator_assignment
        conditional
        range
        binary
        unary)
  _unary_minus_pow (:seq
                    (:field :operator (:alias _unary_minus_num "-"))
                    (:field :operand (:alias _pow binary)))
  _pow (:prec-right 80
        (:seq
         (:field :left _simple_numeric)
         (:field :operator (:alias _binary_star_star "**"))
         (:field :right _arg)))
  _primary (:choice
            parenthesized_statements
            _lhs
            (:alias _function_identifier_call call)
            call
            array
            string_array
            symbol_array
            hash
            subshell
            _literal
            string
            character
            chained_string
            regex
            lambda
            method
            singleton_method
            class
            singleton_class
            module
            begin
            while
            until
            if
            unless
            for
            case
            case_match
            return
            yield
            break
            next
            redo
            retry
            (:alias parenthesized_unary unary)
            heredoc_beginning)
  parenthesized_statements (:seq "(" (:choice _statements :blank) ")")
  element_reference (:prec-left 1
                     (:seq
                      (:field :object _primary)
                      (:alias _element_reference_bracket "[")
                      (:choice _argument_list_with_trailing_comma :blank)
                      "]"
                      (:choice (:field :block (:choice block do_block)) :blank)))
  scope_resolution (:prec-left 57
                    (:seq
                     (:choice "::" (:seq (:field :scope _primary) (:token-immediate "::")))
                     (:field :name constant)))
  _call_operator (:choice "." "&." (:token-immediate "::"))
  _call (:prec-left 56
         (:seq
          (:field :receiver _primary)
          (:field :operator _call_operator)
          (:field :method (:choice identifier operator constant _function_identifier))))
  command_call (:seq
                (:choice
                 _call
                 _chained_command_call
                 (:field :method (:choice _variable _function_identifier)))
                (:field :arguments (:alias command_argument_list argument_list)))
  command_call_with_block (:choice
                           (:seq
                            (:choice
                             _call
                             (:field :method (:choice _variable _function_identifier)))
                            (:prec 1
                             (:seq
                              (:field :arguments (:alias command_argument_list argument_list))
                              (:field :block block))))
                           (:seq
                            (:choice
                             _call
                             (:field :method (:choice _variable _function_identifier)))
                            (:prec -1
                             (:seq
                              (:field :arguments (:alias command_argument_list argument_list))
                              (:field :block do_block)))))
  _chained_command_call (:seq
                         (:field :receiver (:alias command_call_with_block call))
                         (:field :operator _call_operator)
                         (:field :method
                          (:choice identifier _function_identifier operator constant)))
  call (:choice
        (:seq
         (:choice
          (:choice _call (:field :method (:choice _variable _function_identifier)))
          (:prec-left 56 (:seq (:field :receiver _primary) (:field :operator _call_operator))))
         (:field :arguments argument_list))
        (:prec 1
         (:seq
          (:seq
           (:choice
            (:choice _call (:field :method (:choice _variable _function_identifier)))
            (:prec-left 56 (:seq (:field :receiver _primary) (:field :operator _call_operator))))
           (:field :arguments argument_list))
          (:field :block block)))
        (:prec -1
         (:seq
          (:seq
           (:choice
            (:choice _call (:field :method (:choice _variable _function_identifier)))
            (:prec-left 56 (:seq (:field :receiver _primary) (:field :operator _call_operator))))
           (:field :arguments argument_list))
          (:field :block do_block)))
        (:prec 1
         (:seq
          (:choice _call (:field :method (:choice _variable _function_identifier)))
          (:field :block block)))
        (:prec -1
         (:seq
          (:choice _call (:field :method (:choice _variable _function_identifier)))
          (:field :block do_block))))
  command_argument_list (:prec-right 0 (:seq _argument (:repeat (:seq "," _argument))))
  argument_list (:prec-right 0
                 (:seq
                  (:token-immediate "(")
                  (:choice _argument_list_with_trailing_comma :blank)
                  ")"))
  _argument_list_with_trailing_comma (:prec-right 0
                                      (:seq
                                       (:seq _argument (:repeat (:seq "," _argument)))
                                       (:choice "," :blank)))
  _argument (:prec-left 0
             (:choice
              _expression
              splat_argument
              hash_splat_argument
              forward_argument
              block_argument
              pair))
  forward_argument "..."
  splat_argument (:prec-right 0 (:seq (:alias _splat_star "*") (:choice _arg :blank)))
  hash_splat_argument (:prec-right 0
                       (:seq (:alias _hash_splat_star_star "**") (:choice _arg :blank)))
  block_argument (:prec-right 0 (:seq (:alias _block_ampersand "&") (:choice _arg :blank)))
  do_block (:seq
            "do"
            (:choice _terminator :blank)
            (:choice
             (:seq (:field :parameters block_parameters) (:choice _terminator :blank))
             :blank)
            (:choice (:field :body body_statement) :blank)
            "end")
  block (:prec 1
         (:seq
          "{"
          (:field :parameters (:choice block_parameters :blank))
          (:choice (:field :body block_body) :blank)
          "}"))
  _arg_rhs (:choice _arg (:alias rescue_modifier_arg rescue_modifier))
  assignment (:prec-right 15
              (:choice
               (:seq
                (:field :left (:choice _lhs left_assignment_list))
                "="
                (:field :right (:choice _arg_rhs splat_argument right_assignment_list)))))
  command_assignment (:prec-right 15
                      (:seq
                       (:field :left (:choice _lhs left_assignment_list))
                       "="
                       (:field :right
                        (:choice _expression (:alias rescue_modifier_expression rescue_modifier)))))
  operator_assignment (:prec-right 15
                       (:seq
                        (:field :left _lhs)
                        (:field :operator
                         (:choice
                          "+="
                          "-="
                          "*="
                          "**="
                          "/="
                          "||="
                          "|="
                          "&&="
                          "&="
                          "%="
                          ">>="
                          "<<="
                          "^="))
                        (:field :right _arg_rhs)))
  command_operator_assignment (:prec-right 15
                               (:seq
                                (:field :left _lhs)
                                (:field :operator
                                 (:choice
                                  "+="
                                  "-="
                                  "*="
                                  "**="
                                  "/="
                                  "||="
                                  "|="
                                  "&&="
                                  "&="
                                  "%="
                                  ">>="
                                  "<<="
                                  "^="))
                                (:field :right
                                 (:choice
                                  _expression
                                  (:alias rescue_modifier_expression rescue_modifier)))))
  conditional (:prec-right 20
               (:seq
                (:field :condition _arg)
                "?"
                (:field :consequence _arg)
                ":"
                (:field :alternative _arg)))
  range (:prec-right 25
         (:choice
          (:seq (:field :begin _arg) (:field :operator (:choice ".." "...")) (:field :end _arg))
          (:seq (:field :operator (:choice ".." "...")) (:field :end _arg))
          (:seq (:field :begin _arg) (:field :operator (:choice ".." "...")))))
  binary (:choice
          (:prec-left -2 (:seq (:field :left _arg) (:field :operator "and") (:field :right _arg)))
          (:prec-left -2 (:seq (:field :left _arg) (:field :operator "or") (:field :right _arg)))
          (:prec-left 30 (:seq (:field :left _arg) (:field :operator "||") (:field :right _arg)))
          (:prec-left 35 (:seq (:field :left _arg) (:field :operator "&&") (:field :right _arg)))
          (:prec-left 60
           (:seq (:field :left _arg) (:field :operator (:choice "<<" ">>")) (:field :right _arg)))
          (:prec-left 45
           (:seq
            (:field :left _arg)
            (:field :operator (:choice "<" "<=" ">" ">="))
            (:field :right _arg)))
          (:prec-left 55 (:seq (:field :left _arg) (:field :operator "&") (:field :right _arg)))
          (:prec-left 50
           (:seq (:field :left _arg) (:field :operator (:choice "^" "|")) (:field :right _arg)))
          (:prec-left 65
           (:seq
            (:field :left _arg)
            (:field :operator (:choice "+" (:alias _binary_minus "-")))
            (:field :right _arg)))
          (:prec-left 70
           (:seq
            (:field :left _arg)
            (:field :operator (:choice "/" "%" (:alias _binary_star "*")))
            (:field :right _arg)))
          (:prec-right 40
           (:seq
            (:field :left _arg)
            (:field :operator (:choice "==" "!=" "===" "<=>" "=~" "!~"))
            (:field :right _arg)))
          (:prec-right 80
           (:seq
            (:field :left _arg)
            (:field :operator (:alias _binary_star_star "**"))
            (:field :right _arg))))
  command_binary (:prec-left 0
                  (:seq
                   (:field :left _expression)
                   (:field :operator (:choice "or" "and"))
                   (:field :right _expression)))
  unary (:choice
         (:prec 10 (:seq (:field :operator "defined?") (:field :operand _arg)))
         (:prec-right 5 (:seq (:field :operator "not") (:field :operand _arg)))
         (:prec-right 75
          (:seq
           (:field :operator (:choice (:alias _unary_minus "-") (:alias _binary_minus "-") "+"))
           (:field :operand _arg)))
         (:prec-right 85 (:seq (:field :operator (:choice "!" "~")) (:field :operand _arg))))
  command_unary (:choice
                 (:prec 10 (:seq (:field :operator "defined?") (:field :operand _expression)))
                 (:prec-right 5 (:seq (:field :operator "not") (:field :operand _expression)))
                 (:prec-right 75
                  (:seq
                   (:field :operator (:choice (:alias _unary_minus "-") "+"))
                   (:field :operand _expression)))
                 (:prec-right 85
                  (:seq (:field :operator (:choice "!" "~")) (:field :operand _expression))))
  parenthesized_unary (:prec 56
                       (:seq
                        (:field :operator (:choice "defined?" "not"))
                        (:field :operand parenthesized_statements)))
  unary_literal (:prec-right 75
                 (:seq
                  (:field :operator (:choice (:alias _unary_minus_num "-") "+"))
                  (:field :operand _simple_numeric)))
  _literal (:choice simple_symbol delimited_symbol _numeric)
  _numeric (:choice _simple_numeric (:alias unary_literal unary))
  _simple_numeric (:choice integer float complex rational)
  right_assignment_list (:prec -1
                         (:seq
                          (:choice _arg splat_argument)
                          (:repeat (:seq "," (:choice _arg splat_argument)))))
  left_assignment_list _mlhs
  _mlhs (:prec-left -1
         (:seq
          (:seq
           (:choice _lhs rest_assignment destructured_left_assignment)
           (:repeat (:seq "," (:choice _lhs rest_assignment destructured_left_assignment))))
          (:choice "," :blank)))
  destructured_left_assignment (:prec -1 (:seq "(" _mlhs ")"))
  rest_assignment (:prec -1 (:seq "*" (:choice _lhs :blank)))
  _function_identifier (:choice
                        (:alias identifier_suffix identifier)
                        (:alias constant_suffix constant))
  _function_identifier_call (:prec-left 0 (:field :method _function_identifier))
  _lhs (:prec-left 0
        (:choice
         _variable
         (:ref "true")
         (:ref "false")
         (:ref "nil")
         scope_resolution
         element_reference
         (:alias _call call)))
  _variable (:prec-right 0 (:choice self super _nonlocal_variable identifier constant))
  operator (:choice
            ".."
            "|"
            "^"
            "&"
            "<=>"
            "=="
            "==="
            "=~"
            ">"
            ">="
            "<"
            "<="
            "+"
            "!="
            "-"
            "*"
            "/"
            "%"
            "!"
            "!~"
            "**"
            "<<"
            ">>"
            "~"
            "+@"
            "-@"
            "~@"
            "[]"
            "[]="
            "`")
  _method_name (:choice
                identifier
                _function_identifier
                constant
                setter
                simple_symbol
                delimited_symbol
                operator
                _nonlocal_variable)
  _nonlocal_variable (:choice instance_variable class_variable global_variable)
  setter (:seq (:field :name identifier) (:token-immediate "="))
  undef (:seq "undef" (:seq _method_name (:repeat (:seq "," _method_name))))
  alias (:seq "alias" (:field :name _method_name) (:field :alias _method_name))
  comment (:token
           (:prec -2
            (:choice
             (:seq "#" (:pattern ".*"))
             (:seq
              (:pattern "=begin.*\\r?\\n")
              (:repeat
               (:choice
                (:pattern "[^=]")
                (:pattern "=[^e]")
                (:pattern "=e[^n]")
                (:pattern "=en[^d]")))
              (:pattern "[\\s*]*=end.*")))))
  integer (:pattern "0[bB][01](_?[01])*|0[oO]?[0-7](_?[0-7])*|(0[dD])?\\d(_?\\d)*|0[xX][0-9a-fA-F](_?[0-9a-fA-F])*")
  _int_or_float (:choice integer float)
  float (:pattern "\\d(_?\\d)*(\\.\\d)?(_?\\d)*([eE][\\+-]?\\d(_?\\d)*)?")
  complex (:choice
           (:seq _int_or_float (:token-immediate "i"))
           (:seq (:alias _int_or_float rational) (:token-immediate "ri")))
  rational (:seq _int_or_float (:token-immediate "r"))
  super "super"
  self "self"
  (:ref "true") "true"
  (:ref "false") "false"
  (:ref "nil") "nil"
  constant (:token
            (:seq
             (:pattern "[A-Z]")
             (:pattern "[^\\x00-\\x1F\\s:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]*")))
  constant_suffix (:choice
                   (:token
                    (:seq
                     (:pattern "[A-Z]")
                     (:pattern "[^\\x00-\\x1F\\s:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]*")
                     (:pattern "[?]")))
                   _constant_suffix)
  identifier (:token
              (:seq
               (:pattern "[^\\x00-\\x1F\\sA-Z0-9:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]")
               (:pattern "[^\\x00-\\x1F\\s:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]*")))
  identifier_suffix (:choice
                     (:token
                      (:seq
                       (:pattern "[^\\x00-\\x1F\\sA-Z0-9:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]")
                       (:pattern "[^\\x00-\\x1F\\s:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]*")
                       (:pattern "[?]")))
                     _identifier_suffix)
  instance_variable (:token
                     (:seq
                      "@"
                      (:pattern "[^\\x00-\\x1F\\s0-9:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]")
                      (:pattern "[^\\x00-\\x1F\\s:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]*")))
  class_variable (:token
                  (:seq
                   "@@"
                   (:pattern "[^\\x00-\\x1F\\s0-9:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]")
                   (:pattern "[^\\x00-\\x1F\\s:;`\"'@$#.,|^&<=>+\\-*/\\\\%?!~()\\[\\]{}]*")))
  global_variable (:pattern "\\$(-[a-zA-Z0-9_]|[!@&`'+~=/\\\\,;.<>*$?:\"]|[0-9]+|[a-zA-Z_][a-zA-Z0-9_]*)")
  chained_string (:seq string (:repeat1 string))
  character (:pattern "\\?(\\\\\\S(\\{[0-9A-Fa-f]*\\}|[0-9A-Fa-f]*|-\\S([MC]-\\S)?)?|\\S)")
  interpolation (:choice
                 (:seq "#{" (:choice _statements :blank) "}")
                 (:seq _short_interpolation _nonlocal_variable))
  string (:seq
          (:alias _string_start "\"")
          (:choice _literal_contents :blank)
          (:alias _string_end "\""))
  subshell (:seq
            (:alias _subshell_start "`")
            (:choice _literal_contents :blank)
            (:alias _string_end "`"))
  string_array (:seq
                (:alias _string_array_start "%w(")
                (:choice (:pattern "\\s+") :blank)
                (:choice
                 (:seq
                  (:alias _literal_contents bare_string)
                  (:repeat (:seq (:pattern "\\s+") (:alias _literal_contents bare_string))))
                 :blank)
                (:choice (:pattern "\\s+") :blank)
                (:alias _string_end ")"))
  symbol_array (:seq
                (:alias _symbol_array_start "%i(")
                (:choice (:pattern "\\s+") :blank)
                (:choice
                 (:seq
                  (:alias _literal_contents bare_symbol)
                  (:repeat (:seq (:pattern "\\s+") (:alias _literal_contents bare_symbol))))
                 :blank)
                (:choice (:pattern "\\s+") :blank)
                (:alias _string_end ")"))
  delimited_symbol (:seq
                    (:alias _symbol_start ":\"")
                    (:choice _literal_contents :blank)
                    (:alias _string_end "\""))
  regex (:seq (:alias _regex_start "/") (:choice _literal_contents :blank) (:alias _string_end "/"))
  heredoc_body (:seq
                _heredoc_body_start
                (:repeat (:choice heredoc_content interpolation escape_sequence))
                heredoc_end)
  _literal_contents (:repeat1 (:choice string_content interpolation escape_sequence))
  escape_sequence (:token
                   (:seq
                    "\\"
                    (:choice
                     (:pattern "[^ux0-7]")
                     (:pattern "x[0-9a-fA-F]{1,2}")
                     (:pattern "[0-7]{1,3}")
                     (:pattern "u[0-9a-fA-F]{4}")
                     (:pattern "u\\{[0-9a-fA-F ]+\\}"))))
  array (:seq "[" (:choice _argument_list_with_trailing_comma :blank) "]")
  hash (:seq
        "{"
        (:choice
         (:seq
          (:seq
           (:choice pair hash_splat_argument)
           (:repeat (:seq "," (:choice pair hash_splat_argument))))
          (:choice "," :blank))
         :blank)
        "}")
  pair (:prec-right 0
        (:choice
         (:seq (:field :key _arg) "=>" (:field :value _arg))
         (:seq (:field :key (:choice string)) (:token-immediate ":") (:field :value _arg))
         (:seq
          (:field :key
           (:choice
            hash_key_symbol
            (:alias identifier hash_key_symbol)
            (:alias constant hash_key_symbol)
            (:alias identifier_suffix hash_key_symbol)
            (:alias constant_suffix hash_key_symbol)))
          (:token-immediate ":")
          (:choice (:field :value (:choice _arg :blank)) _no_line_break))))
  lambda (:seq
          "->"
          (:field :parameters
           (:choice
            (:choice
             (:alias parameters lambda_parameters)
             (:alias bare_parameters lambda_parameters))
            :blank))
          (:field :body (:choice block do_block)))
  empty_statement (:prec -1 ";")
  _terminator (:choice _line_break ";")}}
