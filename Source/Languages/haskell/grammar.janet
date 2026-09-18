# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "haskell"
 :word variable
 :extras [(:pattern "\\p{Zs}") (:pattern "\\n") (:pattern "\\r") cpp comment haddock pragma]
 :conflicts [[_function_name pattern]
             [_function_name pattern expression]
             [pattern expression]
             [signature pattern]
             [_operator_hash_head _unboxed_open]]
 :precedences [["projection"
                "record"
                "prefix"
                "apply"
                "negation"
                "infix"
                "implicit"
                "fun"
                "annotated"
                quantified_type]
               [_pat_negation literal]
               ["bind" "pat-name"]
               ["qualifying-module" "qualified-id"]
               ["qualifying-module" "con"]
               ["qualifying-module" "type-name"]
               [operator _type_star]
               [_type_wildcard _type_param_wildcard]
               [_constructor_ticked _tycon_ticked]
               ["qtype-single" "qtype-curried"]
               ["patterns" "apply"]]
 :externals [error_sentinel
             _cond_layout_semicolon
             _cmd_layout_start
             _cmd_layout_start_do
             _cmd_layout_start_case
             _cmd_layout_start_if
             _cmd_layout_start_let
             _cmd_layout_start_quote
             _cmd_layout_start_explicit
             _cond_layout_end
             _cond_layout_end_explicit
             _cmd_brace_open
             _cmd_brace_close
             _cmd_texp_start
             _cmd_texp_end
             _phantom_where
             _phantom_in
             _phantom_arrow
             _phantom_bar
             _phantom_deriving
             comment
             haddock
             cpp
             pragma
             _cond_quote_start
             quasiquote_body
             _cond_splice
             _cond_qual_dot
             _cond_tight_dot
             _cond_prefix_dot
             _cond_dotdot
             _cond_tight_at
             _cond_prefix_at
             _cond_tight_bang
             _cond_prefix_bang
             _cond_tight_tilde
             _cond_prefix_tilde
             _cond_prefix_percent
             _cond_qualified_op
             _cond_left_section_op
             _cond_no_section_op
             _cond_minus
             _cond_context
             _cond_infix
             _cond_data_infix
             _cond_assoc_tyinst
             _varsym
             _consym
             (:pattern "\\n")]
 :inline [_var
          _vars
          _varids
          _varids_ticked
          _varop
          _constructor
          _con
          _qcon
          _cons
          _conids
          _conids_ticked
          _conop
          _op_ticked
          _modid
          _qvarsym
          _qconsym
          _sym
          _qsym
          _pqsym
          _any_prefix_dot
          _any_tight_dot
          _unboxed_bar
          _exp_name
          _exp_greedy
          _let
          _pat_apply_arg
          _pat_name
          _pat_texp
          _tyconid
          _tyconids
          _tycon
          _qtycon
          _tycons
          _tyconops
          _tyops
          _type_name
          _forall
          _type_apply_arg
          _parameter_type
          _field_type
          _type_head
          _type_instance_head
          _type_annotation
          _kind_annotation
          _number
          _stringly
          _unit_cons
          _tuple_cons
          _universal
          _function_head_patterns
          _function_head]
 :supertypes [expression
              pattern
              type
              quantified_type
              constraint
              constraints
              type_param
              declaration
              decl
              class_decl
              instance_decl
              statement
              qualifier
              guard]
 :rules
 {haskell (:seq (:choice header :blank) (:choice _body :blank))
  generator (:seq (:field :pattern _pat) (:field :arrow _larrow) (:field :expression _exp))
  _let_binds (:seq
              (:choice _cmd_layout_start_let (:alias _cmd_layout_start_explicit "{"))
              (:choice
               (:seq
                (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                (:seq
                 (:field :decl decl)
                 (:repeat
                  (:seq (:choice (:repeat1 ";") _cond_layout_semicolon) (:field :decl decl))))
                (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
               :blank)
              _layout_end)
  _let (:seq "let" (:choice (:field :binds (:alias _let_binds local_binds)) :blank))
  let _let
  guard (:choice (:alias generator pattern_guard) let (:alias _exp boolean))
  guards (:seq (:field :guard guard) (:repeat (:seq "," (:field :guard guard))))
  _guards (:seq _bar _cmd_texp_start (:field :guards guards))
  _universal (:choice splice quasiquote literal _unit_cons _tuple_cons)
  _inferred_tyvar (:seq "{" _cmd_brace_open _ktype_param "}" _cmd_brace_close)
  _type_param_parens (:seq _paren_open _ktype_param _paren_close)
  _type_param_wildcard "_"
  _type_param_annotated (:prec "annotated" (:seq type_param _kind_annotation))
  _type_param_invisible (:prec "prefix" (:seq _prefix_at (:field :bind type_param)))
  type_param (:choice
              (:alias _type_param_wildcard wildcard)
              (:alias _type_param_invisible invisible)
              (:alias _type_param_parens parens)
              (:field :bind variable))
  _ktype_param (:choice type_param (:alias _type_param_annotated annotated))
  type_params (:repeat1 (:prec "patterns" type_param))
  quantified_variables (:repeat1 (:choice type_param (:alias _inferred_tyvar inferred)))
  _type_parens (:seq _paren_open (:field :type _ktype) _paren_close)
  _type_tuple_elems (:seq (:field :element _ktype) (:repeat1 (:seq "," (:field :element _ktype))))
  _type_tuple (:seq _paren_open _type_tuple_elems _paren_close)
  _type_unboxed_tuple (:seq
                       _unboxed_open
                       (:seq (:field :element _ktype) (:repeat (:seq "," (:field :element _ktype))))
                       _unboxed_close)
  _type_unboxed_sum (:seq
                     _unboxed_open
                     (:seq
                      (:field :element _ktype)
                      (:repeat1 (:seq _unboxed_bar (:field :element _ktype))))
                     _unboxed_close)
  _type_list (:seq
              _bracket_open
              (:seq (:field :element _ktype) (:repeat (:seq "," (:field :element _ktype))))
              _bracket_close)
  _type_promoted (:seq
                  "'"
                  (:choice
                   (:alias _plist empty_list)
                   (:alias _type_tuple tuple)
                   (:alias _type_list list)
                   prefix_tuple
                   unit))
  _type_name (:choice variable _promoted_tycons (:prec "type-name" _tycons))
  _type_star (:choice "*" "★")
  _type_wildcard "_"
  _at_type (:prec "prefix" (:seq _prefix_at (:field :type type)))
  _type_apply_arg (:choice type (:alias _at_type kind_application))
  _type_apply (:prec-left "apply"
               (:seq (:field :constructor type) (:field :argument _type_apply_arg)))
  _type_infix (:prec-right "infix"
               (:seq
                (:field :left_operand type)
                (:field :operator _tyops)
                (:field :right_operand type)))
  type (:choice
        _type_name
        (:alias _type_star star)
        (:alias _type_wildcard wildcard)
        (:alias _type_parens parens)
        (:alias _type_promoted promoted)
        (:alias _type_list list)
        (:alias _plist prefix_list)
        (:alias _type_unboxed_tuple unboxed_tuple)
        (:alias _type_unboxed_sum unboxed_sum)
        (:alias _type_tuple tuple)
        (:alias _type_infix infix)
        (:alias _type_apply apply)
        _universal)
  _forall_keyword (:choice "forall" "∀")
  _forall_body (:seq
                (:field :quantifier _forall_keyword)
                (:choice (:field :variables quantified_variables) :blank))
  forall (:prec "qtype-single" (:seq _forall_body "."))
  forall_required (:prec "qtype-single" (:seq _forall_body _arrow))
  _forall (:choice forall forall_required)
  _qtype_forall (:prec-right "qtype-curried" (:seq _forall_body "." (:field :type quantified_type)))
  _qtype_forall_required (:prec-right "qtype-curried"
                          (:seq _forall_body _arrow (:field :type quantified_type)))
  _fun_arrow (:seq (:choice _phantom_arrow :blank) (:field :arrow _arrow))
  modifier (:prec "prefix" (:seq _prefix_percent type))
  _linear_fun_arrow (:choice
                     (:seq (:field :multiplicity modifier) _fun_arrow)
                     (:seq (:choice _phantom_arrow :blank) (:field :arrow _linear_arrow)))
  strict_field (:prec "prefix" (:seq _any_prefix_bang (:field :type type)))
  lazy_field (:prec "prefix" (:seq _any_prefix_tilde (:field :type type)))
  _parameter_type (:field :parameter (:choice strict_field lazy_field quantified_type))
  _qtype_function (:prec-right 0 (:seq _parameter_type _fun_arrow (:field :result quantified_type)))
  _qtype_linear_function (:prec-right 0
                          (:seq _parameter_type _linear_fun_arrow (:field :result quantified_type)))
  _qtype_context (:prec-right "qtype-curried" (:seq _context_inline (:field :type quantified_type)))
  quantified_type (:choice
                   (:alias _qtype_function function)
                   (:alias _qtype_linear_function linear_function)
                   (:alias _qtype_forall forall)
                   (:alias _qtype_forall_required forall_required)
                   (:alias _qtype_context context)
                   implicit_parameter
                   (:prec-right 0 type))
  _type_annotation (:seq _colon2 (:field :type quantified_type))
  _kind_annotation (:seq _colon2 (:field :kind quantified_type))
  _type_signature (:prec-right "annotated" (:seq (:field :type quantified_type) _kind_annotation))
  _ktype (:choice (:alias _type_signature signature) quantified_type)
  _type_head_name (:field :name (:choice _tycon unit (:alias _plist prefix_list)))
  _type_head_parens (:seq _paren_open (:choice _type_head _type_head_params) _paren_close)
  _type_head_params (:choice _type_head_name (:alias _type_head_parens parens))
  _type_head_infix (:prec "infix"
                    (:seq
                     (:field :left_operand type_param)
                     (:field :operator _tyconops)
                     (:field :right_operand type_param)))
  _type_head (:choice
              (:seq _type_head_params (:choice (:field :patterns type_params) :blank))
              (:alias _type_head_infix infix))
  _type_instance_head_parens (:seq
                              _paren_open
                              (:choice _type_instance_head _type_instance_head_params)
                              (:choice _kind_annotation :blank)
                              _paren_close)
  _type_instance_head_params (:choice
                              (:field :name _tycons)
                              (:alias _type_instance_head_parens parens))
  type_patterns (:repeat1 (:prec "patterns" _type_apply_arg))
  _type_instance_head (:choice
                       (:seq
                        _type_instance_head_params
                        (:choice (:field :patterns type_patterns) :blank))
                       (:seq _cond_infix (:alias _type_infix infix)))
  type_synomym (:seq "type" _type_head "=" (:field :type _ktype))
  kind_signature (:seq "type" _type_head _kind_annotation)
  _type_instance_common (:seq _type_instance_head "=" quantified_type)
  _type_instance (:seq (:choice (:field :forall _forall) :blank) _type_instance_common)
  type_instance (:seq "type" "instance" _type_instance)
  type_family_result (:seq "=" (:field :result quantified_type))
  type_family_injectivity (:seq
                           _bar
                           (:field :result variable)
                           _arrow
                           (:field :determined (:repeat1 variable)))
  _tyfam_inj (:seq type_family_result (:choice type_family_injectivity :blank))
  _tyfam (:seq _type_head (:choice (:choice _kind_annotation _tyfam_inj) :blank))
  _tyfam_equations (:seq
                    (:choice _cmd_layout_start (:alias _cmd_layout_start_explicit "{"))
                    (:choice
                     (:seq
                      (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                      (:seq
                       (:field :equation (:alias _type_instance equation))
                       (:repeat
                        (:seq
                         (:choice (:repeat1 ";") _cond_layout_semicolon)
                         (:field :equation (:alias _type_instance equation)))))
                      (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                     :blank)
                    _layout_end)
  abstract_family (:seq
                   (:choice _cmd_layout_start (:alias _cmd_layout_start_explicit "{"))
                   ".."
                   _layout_end)
  type_family (:seq
               "type"
               "family"
               _tyfam
               (:choice
                (:seq
                 _where
                 (:choice
                  (:field :closed_family
                   (:choice (:alias _tyfam_equations equations) abstract_family))
                  :blank))
                :blank))
  type_role (:choice "representational" "nominal" "phantom" "_")
  role_annotation (:seq "type" "role" (:field :type _tycons) (:repeat1 (:field :role type_role)))
  _class_apply (:prec-left "apply"
                (:seq (:field :constructor constraint) (:field :argument _type_apply_arg)))
  _class_infix (:prec-right "infix"
                (:seq
                 _cond_infix
                 (:field :left_operand type)
                 (:field :operator _tyops)
                 (:field :right_operand type)))
  _ctr_parens (:seq _paren_open constraints _paren_close)
  _ctr_tuple (:seq _paren_open (:seq constraints (:repeat1 (:seq "," constraints))) _paren_close)
  implicit_parameter (:prec-left 0 (:seq (:field :name implicit_variable) _type_annotation))
  constraint (:choice
              _type_name
              (:alias _class_infix infix)
              (:alias _class_apply apply)
              (:alias _ctr_parens parens)
              (:alias _ctr_tuple tuple)
              (:alias _type_wildcard wildcard)
              _universal)
  _ctr_forall (:prec "fun" (:seq _forall_body "." (:field :constraint constraints)))
  _ctr_context (:prec "fun" (:seq _context_inline (:field :constraint constraints)))
  _ctr_signature (:prec "annotated" (:seq (:field :constraint constraints) _kind_annotation))
  constraints (:choice
               constraint
               (:alias _ctr_context context)
               (:alias _ctr_forall forall)
               implicit_parameter
               (:alias _ctr_signature signature))
  _context_inline (:seq _cond_context (:field :context constraint) (:field :arrow _carrow))
  context (:prec "qtype-single" _context_inline)
  _exp_name (:choice _cons _vars variable implicit_variable label)
  _exp_th_quoted_name (:choice
                       (:seq "'" (:field :name (:choice _vars _cons)))
                       (:prec "prefix" (:seq "''" (:field :type type))))
  _exp_parens (:seq _paren_open (:field :expression _exp) _paren_close)
  _exp_tuple_elems (:seq
                    (:choice
                     (:seq (:repeat1 ",") (:field :element _exp))
                     (:seq (:field :element _exp) "," (:choice (:field :element _exp) :blank)))
                    (:repeat (:seq "," (:choice (:field :element _exp) :blank))))
  _exp_tuple (:seq _paren_open _exp_tuple_elems _paren_close)
  _exp_unboxed_tuple (:seq
                      _unboxed_open
                      (:repeat ",")
                      (:field :element _exp)
                      (:repeat (:seq "," (:choice (:field :element _exp) :blank)))
                      _unboxed_close)
  _exp_unboxed_sum (:seq
                    _unboxed_open
                    (:choice
                     (:seq (:repeat1 _unboxed_bar) (:field :element _exp))
                     (:seq (:field :element _exp) _unboxed_bar))
                    (:repeat _unboxed_bar)
                    _unboxed_close)
  _exp_list (:seq
             _bracket_open
             (:seq (:field :element _exp) (:repeat (:seq "," (:field :element _exp))))
             (:choice "," :blank)
             _bracket_close)
  _exp_arithmetic_sequence (:seq
                            _bracket_open
                            (:field :from _exp)
                            (:choice (:seq "," (:field :step _exp)) :blank)
                            _dotdot
                            (:choice (:field :to _exp) :blank)
                            _bracket_close)
  group (:seq
         "then"
         "group"
         (:choice
          (:seq "by" (:field :key (:choice (:alias _exp_signature signature) expression)))
          :blank)
         "using"
         (:field :classifier _exp))
  transform (:seq
             "then"
             (:field :transformation _exp)
             (:choice (:seq "by" (:field :key _exp)) :blank))
  qualifier (:choice generator let transform group (:alias _exp boolean))
  qualifiers (:seq
              (:seq
               (:field :qualifier qualifier)
               (:repeat (:seq "," (:field :qualifier qualifier)))))
  _exp_list_comprehension (:seq
                           _bracket_open
                           (:field :expression _exp)
                           (:repeat1 (:seq "|" (:field :qualifiers qualifiers)))
                           _bracket_close)
  _exp_lambda (:seq "\\" (:field :patterns patterns) _arrow (:field :expression _exp))
  _exp_let_in (:seq _let (:choice _phantom_in :blank) "in" (:field :expression _exp))
  _exp_conditional (:seq
                    "if"
                    (:field :if _exp)
                    (:repeat ";")
                    "then"
                    (:field :then _exp)
                    (:repeat ";")
                    "else"
                    (:field :else _exp))
  _exp_greedy (:choice
               (:alias _exp_lambda lambda)
               (:alias _exp_let_in let_in)
               (:alias _exp_conditional conditional))
  _exp_statement _exp
  statement (:choice (:alias _exp_statement exp) (:alias generator bind) let rec)
  _statements (:seq
               (:choice _cmd_layout_start_do (:alias _cmd_layout_start_explicit "{"))
               (:choice
                (:seq
                 (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                 (:seq
                  (:field :statement statement)
                  (:repeat
                   (:seq
                    (:choice (:repeat1 ";") _cond_layout_semicolon)
                    (:field :statement statement))))
                 (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                :blank)
               _layout_end)
  rec (:seq "rec" _statements)
  _do_keyword (:choice "mdo" "do")
  do_module (:field :qualified_do
             (:prec "qualified-id"
              (:seq (:field :module (:alias _qualifying_module module)) (:field :id _do_keyword))))
  _do (:choice do_module _do_keyword)
  _exp_do (:seq _do _statements)
  match (:seq
         _guards
         (:choice _phantom_arrow :blank)
         _arrow
         _cmd_texp_end
         (:field :expression _exp))
  _simple_match (:seq _arrow (:field :expression _exp))
  _matches (:field :match (:choice (:alias _simple_match match) (:repeat1 match)))
  alternative (:seq (:field :pattern _pat) _matches (:choice _where_binds :blank))
  _nalt (:seq (:field :patterns patterns) _matches (:choice _where_binds :blank))
  alternatives (:seq
                (:choice _cmd_layout_start_case (:alias _cmd_layout_start_explicit "{"))
                (:choice
                 (:seq
                  (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                  (:seq
                   (:field :alternative alternative)
                   (:repeat
                    (:seq
                     (:choice (:repeat1 ";") _cond_layout_semicolon)
                     (:field :alternative alternative))))
                  (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                 :blank)
                _layout_end)
  _nalts (:seq
          (:choice _cmd_layout_start_case (:alias _cmd_layout_start_explicit "{"))
          (:choice
           (:seq
            (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
            (:seq
             (:field :alternative (:alias _nalt alternative))
             (:repeat
              (:seq
               (:choice (:repeat1 ";") _cond_layout_semicolon)
               (:field :alternative (:alias _nalt alternative)))))
            (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
           :blank)
          _layout_end)
  _exp_case (:seq "case" _exp "of" (:choice (:field :alternatives alternatives) :blank))
  _exp_lambda_case (:seq "\\" "case" (:choice (:field :alternatives alternatives) :blank))
  _exp_lambda_cases (:seq
                     "\\"
                     "cases"
                     (:choice (:field :alternatives (:alias _nalts alternatives)) :blank))
  _exp_multi_way_if (:seq
                     "if"
                     _cmd_layout_start_if
                     (:repeat (:field :match match))
                     _cond_layout_end)
  field_update (:choice
                (:alias ".." wildcard)
                (:seq
                 (:field :field _field_spec)
                 (:choice (:seq "=" (:field :expression _exp)) :blank)))
  _exp_record (:prec "record"
               (:seq
                (:field :expression expression)
                (:seq
                 "{"
                 _cmd_brace_open
                 (:choice
                  (:seq
                   (:field :field field_update)
                   (:repeat (:seq "," (:field :field field_update))))
                  :blank)
                 "}"
                 _cmd_brace_close)))
  _exp_projection_selector (:seq
                            _paren_open
                            _any_prefix_dot
                            (:field :field variable)
                            (:repeat (:seq _tight_dot (:field :field variable)))
                            _paren_close)
  _exp_projection (:seq
                   (:prec "projection" (:seq (:field :expression expression) _tight_dot))
                   (:field :field field_name))
  explicit_type (:seq _paren_open "type" (:field :type type) _paren_close)
  _exp_apply (:prec-left "apply"
              (:seq
               (:field :function expression)
               (:field :argument
                (:choice expression (:alias _at_type type_application) explicit_type))))
  _exp_op (:choice _sym _op_ticked (:alias _prefix_dot operator))
  _exp_section_left (:seq
                     _paren_open
                     (:field :left_operand expression)
                     _cond_left_section_op
                     (:field :operator (:choice _exp_op _operator_minus _qsym))
                     _paren_close)
  _exp_section_right (:seq
                      _paren_open
                      (:choice (:alias _operator_qual_dot_head operator) _ops)
                      (:field :right_operand expression)
                      _paren_close)
  _exp_negation (:seq (:field :minus "-") (:prec "negation" (:field :expression expression)))
  _exp_infix (:prec-right "infix"
              (:seq
               (:field :left_operand expression)
               (:choice _cond_no_section_op :blank)
               (:field :operator
                (:choice (:seq _cond_minus _operator_minus) _exp_op (:seq _cond_qualified_op _qsym)))
               (:field :right_operand expression)))
  expression (:choice
              (:alias _exp_infix infix)
              (:alias _exp_negation negation)
              (:alias _exp_apply apply)
              (:alias _exp_record record)
              (:alias _exp_projection projection)
              (:alias _exp_arithmetic_sequence arithmetic_sequence)
              (:alias _exp_list_comprehension list_comprehension)
              (:alias _exp_unboxed_tuple unboxed_tuple)
              (:alias _exp_unboxed_sum unboxed_sum)
              (:alias _exp_projection_selector projection_selector)
              (:alias _exp_quote quote)
              (:alias _exp_typed_quote typed_quote)
              (:alias _exp_th_quoted_name th_quoted_name)
              (:alias _exp_lambda_case lambda_case)
              (:alias _exp_lambda_cases lambda_cases)
              (:alias _exp_do do)
              (:alias _exp_parens parens)
              (:alias _exp_tuple tuple)
              (:alias _exp_list list)
              (:alias _plist list)
              (:alias _exp_section_left left_section)
              (:alias _exp_section_right right_section)
              _exp_greedy
              (:alias _exp_case case)
              (:alias _exp_multi_way_if multi_way_if)
              _exp_name
              _universal)
  _exp_signature (:prec-right "annotated" (:seq (:field :expression expression) _type_annotation))
  _exp (:choice (:alias _exp_signature signature) (:prec-right 0 expression))
  _pat_parens (:seq _paren_open (:field :pattern _pat_texp) _paren_close)
  _pat_tuple_elems (:seq
                    (:field :element _pat_texp)
                    (:repeat1 (:seq "," (:field :element _pat_texp))))
  _pat_tuple (:seq _paren_open _pat_tuple_elems _paren_close)
  _pat_unboxed_tuple (:seq
                      _unboxed_open
                      (:seq
                       (:field :element _pat_texp)
                       (:repeat (:seq "," (:field :element _pat_texp))))
                      _unboxed_close)
  _pat_unboxed_sum (:seq
                    _unboxed_open
                    (:choice
                     (:seq (:repeat1 _unboxed_bar) (:field :element _pat_texp))
                     (:seq (:field :element _pat_texp) _unboxed_bar))
                    (:repeat _unboxed_bar)
                    _unboxed_close)
  _pat_list (:seq
             _bracket_open
             (:seq (:field :element _pat_texp) (:repeat (:seq "," (:field :element _pat_texp))))
             (:choice "," :blank)
             _bracket_close)
  field_pattern (:choice
                 (:alias ".." wildcard)
                 (:seq
                  (:field :field _field_names)
                  (:choice (:seq "=" (:field :pattern _pat_texp)) :blank)))
  _pat_record (:prec "record"
               (:seq
                (:field :constructor pattern)
                (:seq
                 "{"
                 _cmd_brace_open
                 (:choice
                  (:seq
                   (:field :field field_pattern)
                   (:repeat (:seq "," (:field :field field_pattern))))
                  :blank)
                 "}"
                 _cmd_brace_close)))
  _pat_name (:choice (:prec "pat-name" (:prec-dynamic -1000 _var)) _cons)
  _pat_as (:prec "prefix" (:seq (:field :bind variable) _tight_at (:field :pattern pattern)))
  _pat_wildcard "_"
  _pat_strict (:prec "prefix" (:seq _any_prefix_bang (:field :pattern pattern)))
  _pat_irrefutable (:prec "prefix" (:seq _any_prefix_tilde (:field :pattern pattern)))
  _pat_apply_arg (:choice pattern (:alias _at_type type_binder) explicit_type)
  _pat_apply (:prec-left "apply"
              (:seq (:field :function pattern) (:field :argument _pat_apply_arg)))
  _pat_negation (:seq "-" (:field :number _number))
  _pat_infix (:prec-right "infix"
              (:seq
               (:field :left_operand pattern)
               (:choice _cond_no_section_op :blank)
               (:field :operator
                (:choice constructor_operator _conids_ticked (:seq _cond_qualified_op _qconsym)))
               (:field :right_operand pattern)))
  pattern (:choice
           (:alias _pat_infix infix)
           (:alias _pat_negation negation)
           (:alias _pat_apply apply)
           _pat_name
           (:alias _pat_as as)
           (:alias _pat_record record)
           (:alias _pat_wildcard wildcard)
           (:alias _pat_parens parens)
           (:alias _pat_tuple tuple)
           (:alias _pat_unboxed_tuple unboxed_tuple)
           (:alias _pat_unboxed_sum unboxed_sum)
           (:alias _pat_list list)
           (:alias _plist list)
           (:alias _pat_strict strict)
           (:alias _pat_irrefutable irrefutable)
           _universal)
  patterns (:repeat1 (:prec "patterns" _pat_apply_arg))
  _pat_signature (:prec-right "annotated" (:seq (:field :pattern pattern) _type_annotation))
  _pat (:choice (:alias _pat_signature signature) (:prec-right 0 pattern))
  view_pattern (:seq (:field :expression _exp) _arrow (:field :pattern _pat_texp))
  _pat_texp (:choice view_pattern _pat)
  _modid (:alias name module_id)
  _modid_prefix (:prec "qualifying-module" (:seq _modid _any_tight_dot))
  _qualifying_module (:repeat1 _modid_prefix)
  module (:seq (:repeat _modid_prefix) _modid)
  namespace (:choice "pattern" "type")
  _child_type (:seq (:field :namespace "type") (:field :type _tyconids))
  _child (:choice (:alias _child_type associated_type) _qname)
  children (:seq
            _paren_open
            (:choice
             (:seq
              (:field :element (:choice (:alias ".." all_names) _child))
              (:repeat (:seq "," (:field :element (:choice (:alias ".." all_names) _child)))))
             :blank)
            _paren_close)
  _ie_entity (:seq
              (:choice (:field :namespace namespace) :blank)
              (:choice
               (:field :variable _varids)
               (:field :type _tyconids)
               (:field :operator (:choice _sym_prefix _pqsym)))
              (:choice (:field :children children) :blank))
  import_list (:seq
               _paren_open
               (:choice
                (:seq
                 (:field :name (:alias _ie_entity import_name))
                 (:repeat (:seq "," (:field :name (:alias _ie_entity import_name)))))
                :blank)
               (:choice "," :blank)
               _paren_close)
  import (:seq
          "import"
          (:choice "qualified" :blank)
          (:choice (:field :package (:alias string import_package)) :blank)
          (:field :module module)
          (:choice "qualified" :blank)
          (:choice (:seq "as" (:field :alias module)) :blank)
          (:choice (:seq (:choice "hiding" :blank) (:field :names import_list)) :blank))
  module_export (:seq "module" (:field :module module))
  exports (:seq
           _paren_open
           (:choice
            (:seq
             (:choice (:field :export (:alias _ie_entity export)) module_export)
             (:repeat
              (:seq "," (:choice (:field :export (:alias _ie_entity export)) module_export))))
            :blank)
           (:choice "," :blank)
           _paren_close)
  header (:seq "module" (:field :module module) (:field :exports (:choice exports :blank)) _where)
  imports (:seq
           (:seq
            (:field :import import)
            (:repeat (:seq (:choice (:repeat1 ";") _cond_layout_semicolon) (:field :import import))))
           (:choice (:repeat1 ";") _cond_layout_semicolon))
  declarations (:seq
                declaration
                (:repeat
                 (:seq (:choice (:repeat1 ";") _cond_layout_semicolon) (:choice declaration import)))
                (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
  _body (:seq
         (:choice _cmd_layout_start (:alias _cmd_layout_start_explicit "{"))
         (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
         (:field :imports (:choice imports :blank))
         (:field :declarations (:choice declarations :blank))
         _layout_end)
  _layout_end (:choice _cond_layout_end (:alias _cond_layout_end_explicit "}"))
  declaration (:choice
               decl
               type_synomym
               kind_signature
               type_family
               type_instance
               role_annotation
               data_type
               newtype
               data_family
               data_instance
               class
               instance
               default_types
               deriving_instance
               pattern_synonym
               foreign_import
               foreign_export
               fixity
               top_splice)
  field_name variable
  _qfield_name (:prec "qualified-id"
                (:seq (:field :module (:alias _qualifying_module module)) (:field :id field_name)))
  _field_names (:choice field_name (:alias _qfield_name qualified))
  field_path (:seq
              (:field :field _field_names)
              (:repeat1 (:seq _tight_dot (:field :subfield field_name))))
  _field_spec (:choice _field_names field_path)
  field (:prec "annotated"
         (:seq
          (:seq (:field :name field_name) (:repeat (:seq "," (:field :name field_name))))
          _colon2
          (:field :type _parameter_type)))
  _record_fields (:seq
                  "{"
                  _cmd_brace_open
                  (:choice
                   (:seq (:field :field field) (:repeat (:seq "," (:field :field field))))
                   :blank)
                  (:choice "," :blank)
                  "}"
                  _cmd_brace_close)
  via (:seq "via" (:field :type quantified_type))
  deriving_strategy (:choice "stock" "newtype" "anyclass")
  deriving (:seq
            (:choice _phantom_deriving :blank)
            "deriving"
            (:choice (:field :strategy deriving_strategy) :blank)
            (:field :classes constraint)
            (:choice (:field :via via) :blank))
  _gadt_con_prefix (:field :type quantified_type)
  _gadt_con_record (:seq
                    (:field :fields (:alias _record_fields fields))
                    (:field :arrow _fun_arrow)
                    (:field :type quantified_type))
  gadt_constructor (:seq
                    (:choice
                     (:field :name _con)
                     (:field :names (:alias _con_binding_list binding_list)))
                    _colon2
                    (:choice (:field :forall _forall) :blank)
                    (:choice (:field :context context) :blank)
                    (:field :type
                     (:choice (:alias _gadt_con_prefix prefix) (:alias _gadt_con_record record))))
  gadt_constructors (:seq
                     (:choice _cmd_layout_start (:alias _cmd_layout_start_explicit "{"))
                     (:choice
                      (:seq
                       (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                       (:seq
                        (:field :constructor gadt_constructor)
                        (:repeat
                         (:seq
                          (:choice (:repeat1 ";") _cond_layout_semicolon)
                          (:field :constructor gadt_constructor))))
                       (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                      :blank)
                     _layout_end)
  _gadt (:seq
         (:choice _kind_annotation :blank)
         _where
         (:choice (:field :constructors gadt_constructors) :blank))
  _field_type (:choice strict_field lazy_field type)
  _datacon_prefix (:seq
                   (:field :name _con)
                   (:repeat (:prec "patterns" (:field :field _field_type))))
  _datacon_infix (:prec "infix"
                  (:seq
                   _cond_data_infix
                   (:field :left_operand _field_type)
                   (:field :operator _conop)
                   (:field :right_operand _field_type)))
  _datacon_record (:seq (:field :name _constructor) (:field :fields (:alias _record_fields fields)))
  _datacon_unboxed_sum (:seq
                        _unboxed_open
                        (:choice
                         (:seq (:repeat1 _unboxed_bar) (:field :element quantified_type))
                         (:seq (:field :element quantified_type) _unboxed_bar))
                        (:repeat _unboxed_bar)
                        _unboxed_close)
  _datacon_special (:choice
                    unit
                    unboxed_unit
                    (:alias _plist empty_list)
                    (:alias _type_tuple tuple)
                    (:alias _type_unboxed_tuple unboxed_tuple)
                    (:alias _datacon_unboxed_sum unboxed_sum))
  data_constructor (:seq
                    (:choice (:field :forall _forall) :blank)
                    (:choice (:field :context context) :blank)
                    (:field :constructor
                     (:choice
                      (:alias _datacon_prefix prefix)
                      (:alias _datacon_infix infix)
                      (:alias _datacon_record record)
                      (:alias _datacon_special special))))
  data_constructors (:seq
                     (:field :constructor data_constructor)
                     (:repeat (:seq _bar (:field :constructor data_constructor))))
  _data_rhs (:choice _kind_annotation (:seq "=" (:field :constructors data_constructors)) _gadt)
  _data (:seq
         (:choice (:field :context context) :blank)
         _type_head
         (:choice _data_rhs :blank)
         (:repeat (:field :deriving deriving)))
  data_type (:seq (:choice "type" :blank) "data" _data)
  _newtype_con_field type
  newtype_constructor (:seq
                       (:field :name _con)
                       (:field :field
                        (:choice (:alias _newtype_con_field field) (:alias _record_fields record))))
  _newtype (:seq
            (:choice (:seq "=" (:field :constructor newtype_constructor)) _gadt)
            (:repeat (:field :deriving deriving)))
  newtype (:seq "newtype" (:choice (:field :context context) :blank) _type_head _newtype)
  _datafam (:seq _type_head (:choice _kind_annotation :blank))
  data_family (:seq "data" "family" _datafam)
  _inst_adt (:seq
             (:choice (:field :forall _forall) :blank)
             (:choice (:field :context context) :blank)
             _type_instance_head
             (:choice _data_rhs :blank)
             (:repeat (:field :deriving deriving)))
  decl_inst_adt (:seq "data" "instance" _inst_adt)
  _inst_newtype (:seq
                 (:choice (:field :forall _forall) :blank)
                 (:choice (:field :context context) :blank)
                 _type_instance_head
                 _newtype)
  decl_inst_newtype (:seq "newtype" "instance" _inst_newtype)
  data_instance (:choice (:alias decl_inst_adt data_type) (:alias decl_inst_newtype newtype))
  _assoc_tyfam (:seq
                "type"
                (:choice "family" :blank)
                _type_head
                (:choice
                 (:choice _kind_annotation (:seq type_family_result type_family_injectivity))
                 :blank))
  _assoc_tyinst (:seq
                 "type"
                 (:choice "instance" :blank)
                 (:choice (:field :forall _forall) :blank)
                 _cond_assoc_tyinst
                 _type_instance_common)
  _assoc_datafam (:seq "data" (:choice "family" :blank) _datafam)
  _assoc_datainst_adt (:seq "data" (:choice "instance" :blank) _inst_adt)
  _assoc_datainst_newtype (:seq "newtype" (:choice "instance" :blank) _inst_newtype)
  _assoc_datainst (:choice
                   (:alias _assoc_datainst_adt data_type)
                   (:alias _assoc_datainst_newtype newtype))
  default_signature (:seq "default" (:field :signature signature))
  class_decl (:choice
              _local_decl
              default_signature
              (:alias _assoc_tyfam type_family)
              (:alias _assoc_tyinst type_instance)
              (:alias _assoc_datafam data_family))
  fundep (:seq
          (:field :matched (:repeat1 variable))
          _arrow
          (:field :determined (:repeat1 variable)))
  fundeps (:seq _bar (:seq (:field :fundep fundep) (:repeat (:seq "," (:field :fundep fundep)))))
  class_declarations (:seq
                      (:choice _cmd_layout_start (:alias _cmd_layout_start_explicit "{"))
                      (:choice
                       (:seq
                        (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                        (:seq
                         (:field :declaration class_decl)
                         (:repeat
                          (:seq
                           (:choice (:repeat1 ";") _cond_layout_semicolon)
                           (:field :declaration class_decl))))
                        (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                       :blank)
                      _layout_end)
  class (:seq
         "class"
         (:choice (:field :context context) :blank)
         _type_head
         (:field :fundeps (:choice fundeps :blank))
         (:choice (:seq _where (:choice (:field :declarations class_declarations) :blank)) :blank))
  instance_decl (:choice
                 decl
                 (:alias _assoc_datainst data_instance)
                 (:alias _assoc_tyinst type_instance))
  instance_declarations (:seq
                         (:choice _cmd_layout_start (:alias _cmd_layout_start_explicit "{"))
                         (:choice
                          (:seq
                           (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                           (:seq
                            (:field :declaration instance_decl)
                            (:repeat
                             (:seq
                              (:choice (:repeat1 ";") _cond_layout_semicolon)
                              (:field :declaration instance_decl))))
                           (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                          :blank)
                         _layout_end)
  _instance (:seq
             "instance"
             (:choice (:field :forall _forall) :blank)
             (:choice (:field :context context) :blank)
             _type_instance_head)
  instance (:seq
            _instance
            (:choice
             (:seq _where (:choice (:field :declarations instance_declarations) :blank))
             :blank))
  deriving_instance (:seq
                     (:choice _phantom_deriving :blank)
                     "deriving"
                     (:choice
                      (:choice (:field :strategy deriving_strategy) (:field :via via))
                      :blank)
                     _instance)
  _fun_arrow_prec (:seq "-" "1")
  _fun_arrow_fixity (:seq
                     (:field :associativity "infixr")
                     (:field :precedence (:alias _fun_arrow_prec integer))
                     (:field :operator (:alias "->" operator)))
  fixity (:choice
          _fun_arrow_fixity
          (:seq
           (:field :associativity (:choice "infixl" "infixr" "infix"))
           (:field :precedence (:choice integer :blank))
           (:field :operator
            (:seq
             (:choice _operator_minus _varop _conop)
             (:repeat (:seq "," (:choice _operator_minus _varop _conop)))))))
  _con_binding_list (:seq (:field :name _con) (:repeat1 (:seq "," (:field :name _con))))
  _var_binding_list (:seq (:field :name _var) (:repeat1 (:seq "," (:field :name _var))))
  signature (:seq
             (:choice (:field :name _var) (:field :names (:alias _var_binding_list binding_list)))
             _type_annotation)
  _simple_bind_match (:seq "=" (:field :expression _exp))
  _bind_match (:seq _guards "=" _cmd_texp_end (:field :expression _exp))
  _bind_matches (:seq
                 (:choice
                  (:field :match (:alias _simple_bind_match match))
                  (:repeat1 (:field :match (:alias _bind_match match))))
                 (:choice _where_binds :blank))
  _function_name (:field :name _var)
  function_head_parens (:seq
                        _paren_open
                        (:choice _function_head _function_head_patterns)
                        _paren_close)
  _function_head_patterns (:choice _function_name (:field :parens function_head_parens))
  _function_head_infix (:seq
                        (:field :left_operand pattern)
                        (:choice _cond_no_section_op :blank)
                        (:field :operator (:choice (:seq _cond_minus _operator_minus) _varop))
                        (:field :right_operand pattern))
  _function_head (:choice
                  (:seq _function_head_patterns (:field :patterns patterns))
                  (:alias _function_head_infix infix))
  function (:seq _function_head _bind_matches)
  bind (:prec "bind"
        (:seq
         (:choice (:field :pattern _pat) (:field :name _var) (:field :implicit implicit_variable))
         _bind_matches))
  decl (:choice signature function bind)
  _local_decl (:choice fixity decl)
  local_binds (:seq
               (:choice _cmd_layout_start (:alias _cmd_layout_start_explicit "{"))
               (:choice
                (:seq
                 (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                 (:seq
                  (:field :decl _local_decl)
                  (:repeat
                   (:seq (:choice (:repeat1 ";") _cond_layout_semicolon) (:field :decl _local_decl))))
                 (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                :blank)
               _layout_end)
  _where_binds (:seq _where (:choice (:field :binds local_binds) :blank))
  calling_convention (:token
                      (:choice "ccall" "stdcall" "capi" "prim" "javascript" (:pattern "[A-Z_]+")))
  safety (:token (:choice "unsafe" "safe" "interruptible"))
  entity string
  foreign_import (:seq
                  "foreign"
                  "import"
                  (:field :calling_convention calling_convention)
                  (:choice (:field :safety safety) :blank)
                  (:choice (:field :entity entity) :blank)
                  (:field :signature signature))
  foreign_export (:seq
                  "foreign"
                  "export"
                  (:field :calling_convention calling_convention)
                  (:choice (:field :entity entity) :blank)
                  (:field :signature signature))
  default_types (:seq
                 "default"
                 (:seq
                  _paren_open
                  (:choice
                   (:seq (:field :type _ktype) (:repeat (:seq "," (:field :type _ktype))))
                   :blank)
                  _paren_close))
  _patsyn_signature (:seq
                     (:field :synonym (:choice _con (:alias _con_binding_list binding_list)))
                     _colon2
                     (:field :type quantified_type))
  _patsyn_cons (:seq
                (:choice _cmd_layout_start (:alias _cmd_layout_start_explicit "{"))
                (:choice
                 (:seq
                  (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                  (:seq
                   (:alias bind constructor_synonym)
                   (:repeat
                    (:seq
                     (:choice (:repeat1 ";") _cond_layout_semicolon)
                     (:alias bind constructor_synonym))))
                  (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                 :blank)
                _layout_end)
  _patsyn_equation (:seq
                    (:field :synonym pattern)
                    (:choice "=" _larrow)
                    (:field :pattern _pat)
                    (:choice
                     (:seq
                      _where
                      (:choice
                       (:field :constructors (:alias _patsyn_cons constructor_synonyms))
                       :blank))
                     :blank))
  pattern_synonym (:seq
                   "pattern"
                   (:choice (:alias _patsyn_signature signature) (:alias _patsyn_equation equation)))
  _splice_exp (:choice _exp_name (:alias _exp_parens parens) literal)
  _splice_dollars (:seq _cond_splice (:choice "$" "$$"))
  splice (:seq _splice_dollars (:field :expression _splice_exp))
  top_splice expression
  quoter _varids
  quasiquote (:seq
              (:seq _cond_quote_start "[" (:field :quoter quoter) "|")
              (:choice (:field :body quasiquote_body) :blank)
              (:choice (:token "|]") "⟧"))
  quoted_decls (:seq
                (:choice _cmd_layout_start_quote (:alias _cmd_layout_start_explicit "{"))
                (:choice
                 (:seq
                  (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank)
                  (:seq
                   (:field :declaration declaration)
                   (:repeat
                    (:seq
                     (:choice (:repeat1 ";") _cond_layout_semicolon)
                     (:field :declaration declaration))))
                  (:choice (:choice (:repeat1 ";") _cond_layout_semicolon) :blank))
                 :blank)
                _layout_end)
  _exp_quote (:seq
              (:choice
               (:seq
                (:choice
                 "⟦"
                 (:seq _cond_quote_start "[" (:field :quoter (:choice "e" :blank)) "|"))
                (:choice (:alias _exp quoted_expression) :blank))
               (:seq
                (:seq _cond_quote_start "[" (:field :quoter "t") "|")
                (:choice (:alias _ktype quoted_type) :blank))
               (:seq
                (:seq _cond_quote_start "[" (:field :quoter "p") "|")
                (:choice (:alias _pat quoted_pattern) :blank))
               (:seq
                (:seq _cond_quote_start "[" (:field :quoter "d") "|")
                (:choice quoted_decls :blank)))
              (:choice (:token "|]") "⟧"))
  _exp_typed_quote (:seq
                    _cond_quote_start
                    "["
                    (:choice "e" :blank)
                    "||"
                    (:choice (:alias _exp quoted_expression) :blank)
                    (:token "||]"))
  float (:token
         (:seq
          (:seq
           (:pattern "[0-9][0-9_]*")
           (:choice
            (:seq (:pattern "\\.[0-9_]+") (:choice (:pattern "[eE][+-]?[0-9_]+") :blank))
            (:pattern "[eE][+-]?[0-9_]+")))
          (:choice (:token-immediate (:pattern "##?")) :blank)))
  char (:token
        (:seq
         (:choice (:pattern "'[^']'") (:pattern "'\\\\[^ ]*'"))
         (:choice (:token-immediate (:pattern "##?")) :blank)))
  string (:token
          (:seq
           (:seq
            "\""
            (:repeat
             (:choice
              (:pattern "[^\\\\\"\\n]")
              (:pattern "\\\\(\\^)?.")
              (:pattern "\\\\\\n\\s*\\\\")))
            "\"")
           (:choice (:token-immediate (:pattern "##?")) :blank)))
  _integer_literal (:token
                    (:seq
                     (:pattern "[0-9][0-9_]*")
                     (:choice (:token-immediate (:pattern "##?")) :blank)))
  _binary_literal (:token
                   (:seq
                    (:pattern "0[bB][01_]+")
                    (:choice (:token-immediate (:pattern "##?")) :blank)))
  _octal_literal (:token
                  (:seq
                   (:pattern "0[oO][0-7]+")
                   (:choice (:token-immediate (:pattern "##?")) :blank)))
  _hex_literal (:token
                (:seq
                 (:seq
                  (:pattern "0[xX][0-9a-fA-F_]+")
                  (:choice (:pattern "\\.[0-9a-fA-F_]+") :blank)
                  (:choice (:pattern "[pP][+-]?[0-9a-fA-F_]+") :blank))
                 (:choice (:token-immediate (:pattern "##?")) :blank)))
  integer (:choice _binary_literal _integer_literal _octal_literal _hex_literal)
  _stringly (:choice string char)
  _number (:choice integer float)
  _plist (:seq _bracket_open _bracket_close)
  unit (:seq _paren_open _paren_close)
  unboxed_unit (:seq _unboxed_open _unboxed_close)
  prefix_tuple (:seq _paren_open (:repeat1 ",") _paren_close)
  prefix_unboxed_tuple (:seq _unboxed_open (:repeat1 ",") _unboxed_close)
  prefix_unboxed_sum (:seq _unboxed_open (:repeat1 _unboxed_bar) _unboxed_close)
  literal (:choice _stringly _number)
  _unit_cons (:choice unit unboxed_unit)
  _tuple_cons (:choice prefix_tuple prefix_unboxed_tuple prefix_unboxed_sum)
  _qualified_variable (:prec "qualified-id"
                       (:seq
                        (:field :module (:alias _qualifying_module module))
                        (:field :id variable)))
  _qvarid (:alias _qualified_variable qualified)
  _varids (:choice _qvarid variable)
  _var (:choice variable _pvarsym)
  _qvar (:choice _qvarid _pqvarsym)
  _vars (:choice _var _qvar)
  _variable_ticked (:seq "`" variable "`")
  _varop (:choice operator (:alias _variable_ticked infix_id))
  _qvariable_ticked (:seq "`" _qvarid "`")
  _varids_ticked (:alias (:choice _variable_ticked _qvariable_ticked) infix_id)
  _constructor (:alias name constructor)
  _qualified_constructor (:prec "qualified-id"
                          (:seq
                           (:field :module (:alias _qualifying_module module))
                           (:field :id _constructor)))
  _qconid (:alias _qualified_constructor qualified)
  _conids (:choice _qconid _constructor)
  _con (:choice _constructor _pconsym)
  _qcon (:choice _qconid _pqconsym)
  _cons (:choice (:prec "con" _con) _qcon)
  _constructor_ticked (:seq "`" _constructor "`")
  _conop (:choice _constructor_operator_alias (:alias _constructor_ticked infix_id))
  _qconstructor_ticked (:seq "`" _qconid "`")
  _conids_ticked (:alias (:choice _constructor_ticked _qconstructor_ticked) infix_id)
  _tyconid name
  _qualified_type (:prec "qualified-id"
                   (:seq (:field :module (:alias _qualifying_module module)) (:field :id _tyconid)))
  _qtyconid (:alias _qualified_type qualified)
  _tyconids (:choice _qtyconid _tyconid)
  _tycon_arrow (:seq _paren_open (:alias _arrow operator) _paren_close)
  _qualified_arrow (:prec "qualified-id"
                    (:seq
                     (:field :module (:alias _qualifying_module module))
                     (:field :id (:alias _arrow operator))))
  _qtycon_arrow (:seq _paren_open (:alias _qualified_arrow qualified) _paren_close)
  _tycon (:choice _tyconid _pvarsym (:alias _tycon_arrow prefix_id) _pconsym)
  _qtycon (:choice _qtyconid (:alias _qtycon_arrow prefix_id) _pqsym)
  _tycons (:choice _tycon _qtycon)
  _promoted_tycons_alias (:seq "'" _cons)
  _promoted_tycons (:alias _promoted_tycons_alias promoted)
  _tycon_ticked (:seq "`" _tyconid "`")
  _qtycon_ticked (:seq "`" _qtyconid "`")
  _tyconids_ticked (:alias (:choice _tycon_ticked _qtycon_ticked) infix_id)
  _tyconops (:choice _sym _qsym _operator_minus _tyconids_ticked)
  _promoted_tyconops_alias (:seq "'" _tyconops)
  _promoted_tyconops (:alias _promoted_tyconops_alias promoted)
  _tyops (:choice _tyconops _promoted_tyconops)
  _op_ticked (:choice _varids_ticked _conids_ticked)
  _ops (:choice operator _qvarsym constructor_operator _qconsym _op_ticked)
  _name (:choice _var _con)
  _qname (:choice _vars _cons)
  _operator_qual_dot_head (:seq _cond_qual_dot _varsym)
  _operator_hash_head (:seq
                       (:choice "#" (:token-immediate "#"))
                       (:choice (:choice (:token-immediate "#") (:token-immediate "|")) :blank))
  operator (:choice (:seq (:choice _cond_prefix_dot :blank) _varsym) _operator_hash_head "*")
  _operator_alias operator
  _operator_minus (:alias "-" operator)
  _varsym_prefix (:seq
                  _paren_open
                  (:choice operator _operator_minus (:alias _operator_qual_dot_head operator))
                  _paren_close)
  _pvarsym (:alias _varsym_prefix prefix_id)
  _qualified_varsym (:prec "qualified-id"
                     (:seq
                      (:field :module (:alias _qualifying_module module))
                      (:field :id (:choice operator _operator_minus))))
  _qvarsym (:alias _qualified_varsym qualified)
  _qvarsym_prefix (:seq _paren_open _qvarsym _paren_close)
  _pqvarsym (:alias _qvarsym_prefix prefix_id)
  constructor_operator _consym
  _constructor_operator_alias constructor_operator
  _consym_prefix (:seq _paren_open constructor_operator _paren_close)
  _pconsym (:alias _consym_prefix prefix_id)
  _qualified_consym (:prec "qualified-id"
                     (:seq
                      (:field :module (:alias _qualifying_module module))
                      (:field :id constructor_operator)))
  _qconsym (:alias _qualified_consym qualified)
  _qconsym_prefix (:seq _paren_open _qconsym _paren_close)
  _pqconsym (:alias _qconsym_prefix prefix_id)
  _sym (:choice _operator_alias _constructor_operator_alias)
  _sym_prefix (:choice _pvarsym _pconsym)
  _qsym (:choice _qvarsym _qconsym)
  _pqsym (:choice _pqvarsym _pqconsym)
  variable (:token
            (:seq (:pattern "[_\\p{Ll}\\p{Lo}]") (:pattern "[\\pL\\p{Mn}\\pN_']*") (:pattern "#*")))
  implicit_variable (:token
                     (:seq "?" (:pattern "[_\\p{Ll}\\p{Lo}]") (:pattern "[\\pL\\p{Mn}\\pN_']*")))
  name (:token
        (:seq (:pattern "[\\p{Lu}\\p{Lt}]") (:pattern "[\\pL\\p{Mn}\\pN_']*") (:pattern "#*")))
  label (:token (:seq "#" (:pattern "[_\\p{Ll}\\p{Lo}]") (:pattern "[\\pL\\p{Mn}\\pN_']*")))
  _carrow (:choice "=>" "⇒")
  _arrow (:choice "->" "→")
  _linear_arrow (:choice "->." "⊸")
  _larrow (:choice "<-" "←")
  _colon2 (:choice "::" "∷")
  _promote "'"
  _qual_dot (:seq _cond_qual_dot ".")
  _tight_dot (:seq _cond_tight_dot ".")
  _any_tight_dot (:choice _qual_dot _tight_dot)
  _prefix_dot (:seq _cond_prefix_dot ".")
  _any_prefix_dot (:choice _qual_dot _prefix_dot)
  _tight_at (:seq _cond_tight_at "@")
  _prefix_at (:seq _cond_prefix_at "@")
  _prefix_bang (:seq _cond_prefix_bang "!")
  _tight_bang (:seq _cond_tight_bang "!")
  _any_prefix_bang (:choice _prefix_bang _tight_bang)
  _prefix_tilde (:seq _cond_prefix_tilde "~")
  _tight_tilde (:seq _cond_tight_tilde "~")
  _any_prefix_tilde (:choice _prefix_tilde _tight_tilde)
  _prefix_percent (:seq _cond_prefix_percent "%")
  _dotdot (:seq _cond_dotdot "..")
  _paren_open (:seq (:alias (:pattern "\\(") "(") _cmd_texp_start)
  _paren_close (:seq (:alias (:pattern "\\)") ")") _cmd_texp_end)
  _bracket_open (:seq "[" _cmd_texp_start)
  _bracket_close (:seq "]" _cmd_texp_end)
  _unboxed_open (:alias (:seq _paren_open (:token-immediate "#")) "(#")
  _unboxed_close (:seq "#)" _cmd_texp_end)
  _unboxed_bar (:choice "|" (:token-immediate "|"))
  _where (:seq (:choice _phantom_where :blank) "where")
  _bar (:seq (:choice _phantom_bar :blank) "|")}}
