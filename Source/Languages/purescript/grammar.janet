# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "purescript"
 :word _varid
 :extras [(:pattern "\\p{Zs}") (:pattern "\\n") (:pattern "\\r") comment]
 :conflicts [[_field_name_ty _tyvar_no_annotation]
             [_record_update_lhs _aexp_projection]
             [_record_update_lhs exp_name]
             [_data_type_signature _newkind_type_signature]
             [type_infix _type]
             [exp_section_left pat_wildcard]
             [exp_ticked]
             [_fun_name pat_name]
             [signature pat_name]
             [exp_name _pat_constructor]
             [exp_name pat_name]
             [_aexp_projection _apat]
             [pat_name _q_op]
             [exp_array pat_array]
             [_exp_apply _fexp]
             [_exp_apply]
             [pat_apply _apat]
             [pat_apply]
             [type_apply _btype]
             [type_apply]
             [type_name class_head]
             [type_name class_name]
             [operator type_operator]
             [_type]
             [_btype]
             [exp_ado]]
 :precedences [["infix-type" "btype"] ["function-type" "type"]]
 :externals [_layout_semicolon
             _layout_start
             _layout_end
             _dot
             where
             _tyconsym
             comment
             comma
             "@"
             "="
             "|"
             "in"
             (:pattern "\\n")
             empty_file]
 :inline [_stringly
          _qvarid
          _var
          _qvar
          _tyvar
          _qconid
          _con
          _tyconid
          _qtyconid
          _qtyconsym
          _qtycon
          _gtycon
          _simple_tycon
          _simple_qtyconop
          _quantifiers
          _qualifying_module]
 :supertypes []
 :rules
 {purescript (:choice
              empty_file
              _decl_module
              (:seq
               (:seq
                _topdecl
                (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) _topdecl)))
               (:choice (:choice ";" _layout_semicolon) :blank)))
  _topdecl (:choice
            (:alias decl_type type_alias)
            type_role_declaration
            (:alias decl_data data)
            (:alias decl_newtype newtype)
            (:alias decl_import import)
            class_declaration
            class_instance
            _decl_foreign
            (:alias decl_derive derive_declaration)
            _decl
            kind_declaration
            kind_value_declaration
            (:alias decl_pattern pattern_synonym))
  number (:token
          (:seq
           (:choice "0" (:pattern "[1-9][0-9_]*"))
           (:choice
            (:seq (:pattern "\\.[0-9][0-9_]*") (:choice (:pattern "e[+-]?[1-9_]+") :blank))
            (:pattern "e[+-]?[1-9_]+"))))
  char (:token (:choice (:pattern "'[^']'") (:pattern "'\\\\[^ ]*'")))
  string (:token
          (:seq
           "\""
           (:repeat
            (:choice
             (:pattern "[^\\\\\"\\n]")
             (:pattern "\\\\(\\^)?.")
             (:pattern "\\\\\\n\\s*\\\\")))
           "\""))
  triple_quote_string (:token (:seq "\"\"\"" (:pattern "\"{0,2}([^\"]+\"{1,2})*[^\"]*") "\"\"\""))
  _integer_literal (:token (:choice "0" (:pattern "[1-9][0-9_]*")))
  _hex_literal (:token (:pattern "0x[0-9a-fA-F_]+"))
  integer (:choice _integer_literal _hex_literal)
  _stringly (:choice string triple_quote_string char)
  _numeric (:choice integer number)
  _literal (:choice _stringly _numeric)
  _rcarrow (:choice "⇒" "=>")
  _lcarrow (:choice "⇐" "<=")
  _arrow (:choice "→" "->")
  _larrow (:choice "←" "<-")
  _colon2 (:choice "∷" "::")
  wildcard "_"
  hole (:pattern "\\?[\\p{L}0-9_']+")
  _immediate_dot (:token-immediate ".")
  _varid (:pattern "[\\p{Ll}_][\\p{L}0-9_']*")
  _immediate_varid (:token-immediate (:pattern "[\\p{Ll}_][\\p{L}0-9_']*"))
  variable _varid
  _immediate_variable (:alias _immediate_varid variable)
  qualified_variable (:seq _qualifying_module variable)
  _qvarid (:choice qualified_variable variable)
  _operator (:pattern "(?:[:!#$%&*+./<=>?@\\\\^|~-]|\\p{S})+")
  operator _operator
  _minus (:alias "-" operator)
  _operator_or_minus (:choice operator _minus)
  qualified_operator (:seq _qualifying_module _operator_or_minus)
  _q_op (:choice qualified_operator _operator_or_minus)
  _q_op_nominus (:choice qualified_operator operator)
  _var (:choice variable (:seq "(" _operator_or_minus ")"))
  _qvar (:choice _qvarid (:seq "(" _q_op ")"))
  _conid (:pattern "[\\p{Lu}_][\\p{L}0-9_']*")
  constructor _conid
  qualified_constructor (:seq _qualifying_module constructor)
  _qconid (:choice qualified_constructor constructor)
  _con constructor
  _qcon _qconid
  _tyconid (:alias constructor type)
  qualified_type (:seq _qualifying_module _tyconid)
  _qtyconid (:choice qualified_type _tyconid)
  _type_operator (:alias _tyconsym type_operator)
  qualified_type_operator (:seq _qualifying_module type_operator)
  _qualified_type_operator qualified_type_operator
  _qtyconsym (:choice _qualified_type_operator _type_operator)
  _simple_tycon (:choice _tyconid (:seq "(" _type_operator ")"))
  _simple_qtyconop (:choice _qtyconid (:seq "(" _qtyconsym ")"))
  tycon_arrow (:seq "(" _arrow ")")
  type_literal (:choice integer string triple_quote_string)
  _qtycon (:choice _qtyconid (:seq "(" _qtyconsym ")"))
  _gtycon (:choice _qtycon tycon_arrow)
  literal _literal
  _name (:choice _var _con)
  _qname (:choice _qvar _qcon)
  _field_name_ty (:choice
                  (:alias (:prec-dynamic 0 type_variable) field_name)
                  (:alias (:choice string triple_quote_string) field_name))
  row_field (:seq _field_name_ty _colon2 _type)
  _row_variable (:prec 1 (:seq "|" (:choice _type type_variable)))
  row_type (:prec-dynamic 1
            (:seq
             "("
             (:seq
              (:choice (:seq row_field (:repeat (:seq comma row_field))) :blank)
              (:choice _row_variable :blank))
             ")"))
  record_type_literal (:seq
                       "{"
                       (:seq
                        (:choice (:seq row_field (:repeat (:seq comma row_field))) :blank)
                        (:choice _row_variable :blank))
                       "}")
  _field_name (:alias (:choice string triple_quote_string variable) field_name)
  field_wildcard (:prec 1 wildcard)
  record_field (:choice
                (:prec 1 (:alias variable field_pun))
                (:seq _field_name ":" (:choice field_wildcard (:alias _exp field_value))))
  record_literal (:prec 1
                  (:seq
                   "{"
                   (:choice (:seq record_field (:repeat (:seq comma record_field))) :blank)
                   "}"))
  _record_field_update (:seq
                        _field_name
                        (:choice
                         (:seq "=" (:choice wildcard _exp))
                         (:seq
                          "{"
                          (:choice
                           (:seq
                            (:alias _record_field_update record_update)
                            (:repeat (:seq comma (:alias _record_field_update record_update))))
                           :blank)
                          "}")))
  _record_update_lhs (:choice wildcard hole _qvarid record_literal exp_record_access exp_parens)
  record_update (:seq
                 _record_update_lhs
                 (:seq
                  "{"
                  (:choice
                   (:seq _record_field_update (:repeat (:seq comma _record_field_update)))
                   :blank)
                  "}"))
  type_variable _varid
  _visible_type_variable (:seq "@" (:alias _immediate_varid type_variable))
  _tyvar_no_annotation (:choice type_variable _visible_type_variable)
  _tyvar_annotated (:seq "(" (:seq _tyvar_no_annotation _type_annotation) ")")
  _tyvar (:choice _tyvar_no_annotation (:alias _tyvar_annotated annotated_type_variable))
  _forall_kw (:choice "forall" "∀")
  _quantifiers (:seq _forall_kw (:repeat1 _tyvar) ".")
  forall (:prec-left 0 (:repeat1 _quantifiers))
  type_name (:prec-dynamic 0 (:choice _tyvar _qtyconid))
  type_wildcard "_"
  type_operator _operator
  _type_qoperator (:choice type_operator qualified_type_operator)
  captured_type_operator (:seq "(" _q_op ")")
  type_parens (:seq "(" (:seq (:choice forall :blank) _type) ")")
  _type_annotation (:seq _colon2 (:choice forall :blank) _type)
  _fun_signature (:seq (:field :name _varid) _type_annotation)
  _atype (:choice
          hole
          type_wildcard
          row_type
          record_type_literal
          type_name
          type_literal
          type_parens
          captured_type_operator)
  type_apply (:seq _atype (:repeat1 _atype))
  _btype (:seq (:choice forall :blank) (:choice _atype type_apply))
  type_infix (:seq _btype _type_qoperator _type)
  _type (:seq (:choice forall :blank) (:choice type_infix _btype))
  _simpletype (:seq (:field :name _tyconid) (:repeat _tyvar))
  _role (:choice "nominal" "representational" "phantom")
  type_role_declaration (:seq
                         "type"
                         "role"
                         _tyconid
                         (:repeat1 (:field :role (:alias _role type_role))))
  _type_type_signature (:seq "type" _tyconid _type_annotation)
  decl_type (:seq
             (:choice (:alias _type_type_signature type_signature) :blank)
             "type"
             _simpletype
             "="
             _type)
  exp_name (:choice _qvar _qcon)
  exp_ticked (:seq "`" _exp_infix "`")
  exp_negation (:seq "-" _aexp)
  exp_parens (:seq "(" _exp ")")
  exp_type_application (:seq "@" _atype)
  exp_array (:seq "[" (:choice (:seq _exp (:repeat (:seq comma _exp))) :blank) "]")
  exp_section_left (:seq "(" wildcard (:choice _q_op exp_ticked) _exp_infix ")")
  exp_section_right (:seq "(" _exp_infix (:choice _q_op exp_ticked) wildcard ")")
  _record_access_field (:choice _immediate_variable string triple_quote_string)
  record_accessor (:prec-left 0
                   (:seq
                    wildcard
                    (:repeat1 (:seq _immediate_dot (:field :field _record_access_field)))))
  exp_record_access (:prec 1
                     (:seq
                      (:choice hole record_literal exp_parens _qvarid)
                      (:repeat1 (:seq _immediate_dot (:field :field _record_access_field)))))
  exp_if (:seq
          "if"
          (:field :if (:choice wildcard _exp))
          "then"
          (:field :then (:choice wildcard _exp))
          "else"
          (:field :else (:choice wildcard _exp)))
  pattern_guard (:seq _pat _larrow _exp_infix)
  guard (:choice pattern_guard _exp_infix)
  guards (:seq "|" (:seq guard (:repeat (:seq comma guard))))
  gdpat (:seq guards _arrow _exp)
  _alt_variants (:choice (:seq _arrow (:field :exp _exp)) (:repeat1 gdpat))
  alt (:seq
       (:seq (:field :pat _pat) (:repeat (:seq comma (:field :pat _pat))))
       _alt_variants
       (:choice (:seq where declarations) :blank))
  alts (:choice
        (:seq
         _layout_start
         (:choice
          (:seq
           (:seq alt (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) alt)))
           (:choice (:choice ";" _layout_semicolon) :blank))
          :blank)
         _layout_end))
  _exp_case_slots (:seq
                   (:field :condition (:choice wildcard _exp))
                   (:repeat (:seq comma (:field :condition (:choice wildcard _exp)))))
  exp_case (:seq "case" _exp_case_slots "of" alts)
  _let_decls (:choice
              (:seq
               _layout_start
               (:choice
                (:seq
                 (:seq
                  _decl
                  (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) _decl)))
                 (:choice (:choice ";" _layout_semicolon) :blank))
                :blank)))
  exp_let_in (:seq "let" (:alias _let_decls declarations) "in" _exp)
  exp_lambda (:seq "\\" (:repeat1 _apat) _arrow _exp)
  _statement_lexp (:choice exp_if exp_case exp_negation exp_lambda _fexp)
  __statement_exp_infix (:seq _statement_exp_infix (:choice _q_op exp_ticked) _lexp)
  _statement_exp_infix (:choice (:alias __statement_exp_infix exp_infix) _statement_lexp)
  _statement_exp (:prec-right 0 (:seq _statement_exp_infix (:choice _type_annotation :blank)))
  bind_pattern (:seq _typed_pat _larrow _exp)
  let (:seq "let" declarations)
  statement (:choice _statement_exp bind_pattern let)
  _do_kw "do"
  _do (:choice "do" (:seq _qualifying_module _do_kw))
  exp_do (:seq
          _do
          (:choice
           (:seq
            _layout_start
            (:choice
             (:seq
              (:seq
               statement
               (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) statement)))
              (:choice (:choice ";" _layout_semicolon) :blank))
             :blank)
            _layout_end)))
  _ado_kw "ado"
  _ado (:choice "ado" (:seq _qualifying_module _ado_kw))
  _ado_in (:seq
           "in"
           (:field :in
            (:choice
             (:seq
              _layout_start
              (:choice
               (:seq
                (:seq _exp (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) _exp)))
                (:choice (:choice ";" _layout_semicolon) :blank))
               :blank)
              _layout_end))))
  exp_ado (:seq
           _ado
           _layout_start
           (:choice
            (:seq
             (:seq
              statement
              (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) statement)))
             (:choice (:choice ";" _layout_semicolon) :blank))
            :blank)
           _ado_in
           (:choice _layout_end :blank))
  _do_or_ado_block (:choice exp_do exp_ado)
  _aexp_projection (:choice
                    hole
                    exp_name
                    exp_parens
                    exp_array
                    record_literal
                    record_update
                    record_accessor
                    exp_record_access
                    exp_section_left
                    exp_section_right
                    (:alias literal exp_literal))
  _aexp (:choice _aexp_projection exp_type_application _do_or_ado_block)
  _exp_apply (:choice
              _aexp
              (:seq _aexp _exp_apply)
              (:seq _aexp exp_lambda)
              (:seq _aexp exp_if)
              (:seq _aexp exp_case)
              (:seq _aexp exp_let_in))
  _fexp (:choice _aexp (:alias _exp_apply exp_apply))
  _lexp (:choice exp_if exp_case exp_negation exp_lambda _fexp exp_let_in)
  exp_infix (:seq _exp_infix (:choice _q_op exp_ticked) _lexp)
  _exp_infix (:choice exp_infix _lexp)
  _exp (:prec-right 0 (:seq _exp_infix (:choice _type_annotation :blank)))
  pat_field (:seq _field_name (:choice (:seq ":" _typed_pat) :blank))
  pat_fields (:seq "{" (:choice (:seq pat_field (:repeat (:seq comma pat_field))) :blank) "}")
  pat_name _var
  pat_as (:seq (:field :var variable) (:token-immediate "@") (:field :pat _apat))
  _pat_constructor (:alias _qcon pat_name)
  pat_record (:field :fields pat_fields)
  pat_wildcard (:alias wildcard pat_wildcard)
  pat_parens (:seq "(" _typed_pat ")")
  pat_array (:seq "[" (:choice (:seq _typed_pat (:repeat (:seq comma _typed_pat))) :blank) "]")
  _apat (:choice
         pat_name
         pat_as
         _pat_constructor
         pat_record
         (:alias literal pat_literal)
         pat_wildcard
         pat_parens
         pat_array)
  pat_negation (:seq "-" _apat)
  pat_apply (:seq _pat_constructor (:repeat1 _apat))
  _lpat (:choice _apat pat_negation pat_apply)
  pat_infix (:seq _lpat _q_op _pat)
  _pat (:choice (:prec 2 pat_infix) (:prec 1 _lpat))
  pat_typed (:seq (:field :pattern _pat) _type_annotation)
  _typed_pat (:choice _pat pat_typed)
  _import_name (:choice _con _var)
  import_con_names (:seq
                    "("
                    (:choice
                     (:choice
                      (:alias ".." all_names)
                      (:seq _import_name (:repeat (:seq comma _import_name))))
                     :blank)
                    ")")
  import_item (:choice class_import type_operator_import type_import var_import)
  class_import (:seq "class" type_name)
  type_operator_import (:seq "type" (:seq "(" operator ")"))
  type_import (:seq _simple_tycon (:choice import_con_names :blank))
  var_import _var
  import_list (:seq
               (:choice "hiding" :blank)
               (:seq
                "("
                (:choice
                 (:seq (:seq import_item (:repeat (:seq comma import_item))) (:choice comma :blank))
                 :blank)
                ")"))
  decl_import (:seq
               "import"
               (:field :module qualified_module)
               (:field :imports (:choice import_list :blank))
               (:field :import_rename
                (:choice (:seq "as" (:choice _modid (:seq _qualifying_module _modid))) :blank)))
  _modid (:alias constructor module)
  _qualifying_module (:repeat1 (:seq _modid _dot))
  qualified_module (:choice _modid (:seq _qualifying_module _modid))
  export_names (:seq
                "("
                (:choice
                 (:choice
                  (:alias ".." all_names)
                  (:choice (:seq _name (:repeat (:seq comma _name))) :blank))
                 :blank)
                ")")
  export (:choice
          _qvar
          (:seq _qtycon (:choice export_names :blank))
          (:seq "type" (:seq "(" _q_op ")"))
          (:seq "class" class_name)
          (:seq "module" (:field :module qualified_module)))
  exports (:seq "(" (:seq export (:repeat (:seq comma export))) ")")
  _decl_module (:seq
                "module"
                (:field :name qualified_module)
                (:field :exports (:choice exports :blank))
                (:seq
                 where
                 (:choice
                  (:choice
                   (:seq
                    _layout_start
                    (:choice
                     (:seq
                      (:seq
                       _topdecl
                       (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) _topdecl)))
                      (:choice (:choice ";" _layout_semicolon) :blank))
                     :blank)
                    _layout_end))
                  :blank)))
  _data_type_signature (:seq "data" _tyconid _type_annotation)
  decl_data (:seq
             (:choice (:alias _data_type_signature type_signature) :blank)
             "data"
             _simpletype
             "="
             (:seq
              (:seq constructor (:repeat _type))
              (:repeat (:seq "|" (:seq constructor (:repeat _type))))))
  newtype_constructor (:seq constructor _atype)
  _newtype_type_signature (:seq "newtype" _tyconid _type_annotation)
  decl_newtype (:seq
                (:choice (:alias _newtype_type_signature type_signature) :blank)
                "newtype"
                _simpletype
                "="
                newtype_constructor)
  class_name _qtyconid
  constraint (:seq class_name (:repeat _type))
  constraints (:choice
               constraint
               (:seq "(" (:seq constraint (:repeat (:seq comma constraint))) ")"))
  _cdecl (:choice _gendecl function)
  fundep (:seq (:repeat type_variable) _arrow (:repeat1 type_variable))
  fundeps (:seq "|" (:seq fundep (:repeat (:seq comma fundep))))
  class_body (:seq
              where
              (:choice
               (:choice
                (:seq
                 _layout_start
                 (:choice
                  (:seq
                   (:seq
                    _cdecl
                    (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) _cdecl)))
                   (:choice (:choice ";" _layout_semicolon) :blank))
                  :blank)
                 _layout_end))
               :blank))
  _class_kind_declaration (:seq "class" (:alias _tyconid class_name) _type_annotation)
  class_head (:seq
              (:choice (:seq constraints _lcarrow) :blank)
              class_name
              (:repeat _tyvar)
              (:choice fundeps :blank))
  class_declaration (:seq
                     (:choice (:alias _class_kind_declaration kind_declaration) :blank)
                     "class"
                     class_head
                     (:choice class_body :blank))
  _idecl (:choice function signature)
  instance_head (:seq (:choice (:seq constraints _rcarrow) :blank) class_name (:repeat _type))
  _instance_name (:seq (:alias _varid instance_name) _colon2)
  class_instance (:seq
                  (:choice "else" :blank)
                  "instance"
                  (:choice _instance_name :blank)
                  instance_head
                  (:choice
                   (:seq
                    where
                    (:choice
                     (:choice
                      (:seq
                       _layout_start
                       (:choice
                        (:seq
                         (:seq
                          _idecl
                          (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) _idecl)))
                         (:choice (:choice ";" _layout_semicolon) :blank))
                        :blank)
                       _layout_end))
                     :blank))
                   :blank))
  _funpat (:seq (:field :pattern _typed_pat) _funrhs)
  _fun_name (:field :name _var)
  guard_equation (:seq guards "=" _exp)
  _fun_guards (:repeat1 guard_equation)
  _funrhs (:seq
           (:choice (:seq "=" (:field :rhs _exp)) _fun_guards)
           (:choice (:seq where declarations) :blank))
  _fun_patterns (:repeat1 _apat)
  _funvar (:seq _fun_name (:field :patterns (:choice (:alias _fun_patterns patterns) :blank)))
  _funlhs (:prec-dynamic 2 _funvar)
  function (:seq _funlhs _funrhs)
  operator_declaration (:seq
                        (:choice "infixl" "infixr" "infix")
                        (:field :precedence integer)
                        (:choice "type" :blank)
                        (:choice _qtyconid _qvarid)
                        "as"
                        operator)
  signature (:seq (:field :name _var) _type_annotation)
  _gendecl (:choice signature operator_declaration)
  _decl_fun (:choice function (:prec-dynamic 1 (:alias _funpat function)))
  _decl (:choice _gendecl _decl_fun)
  declarations (:choice
                (:seq
                 _layout_start
                 (:choice
                  (:seq
                   (:seq
                    _decl
                    (:repeat (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) _decl)))
                   (:choice (:choice ";" _layout_semicolon) :blank))
                  :blank)
                 _layout_end))
  decl_foreign_import (:seq "foreign" "import" _fun_name _type_annotation)
  _decl_foreign (:alias decl_foreign_import foreign_import)
  _newkind_type_signature (:prec-dynamic 1 (:seq "data" _tyconid _type_annotation))
  kind_declaration (:seq
                    (:choice (:alias _newkind_type_signature kind_signature) :blank)
                    "data"
                    _simpletype)
  kind_value_declaration (:seq "foreign" "import" "data" _simpletype _type_annotation)
  decl_derive (:prec 1
               (:seq
                "derive"
                (:choice "newtype" :blank)
                "instance"
                (:choice (:seq (:alias _varid instance_name) _colon2) :blank)
                (:choice (:seq constraints _rcarrow) :blank)
                type_name
                (:repeat _atype)))
  _pattern_type (:seq _con _type_annotation)
  _pattern_equals (:seq (:field :lhs _pat) "=" (:field :rhs _pat))
  _pattern_decl (:seq _pat _funrhs)
  _pattern_arrow (:seq
                  (:field :lhs _pat)
                  _larrow
                  (:field :rhs _pat)
                  (:choice
                   (:seq
                    where
                    (:choice
                     (:seq
                      _layout_start
                      (:choice
                       (:seq
                        (:seq
                         _pattern_decl
                         (:repeat
                          (:seq (:prec-dynamic 1 (:choice ";" _layout_semicolon)) _pattern_decl)))
                        (:choice (:choice ";" _layout_semicolon) :blank))
                       :blank)
                      _layout_end)))
                   :blank))
  decl_pattern (:seq
                "pattern"
                (:choice
                 (:alias _pattern_type signature)
                 (:alias _pattern_equals equation)
                 (:alias _pattern_arrow equation)))}}
