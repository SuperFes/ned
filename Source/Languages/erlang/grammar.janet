# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "erlang"
 :word atom
 :extras [(:pattern "[\\x01-\\x20\\x80-\\xA0]") comment]
 :conflicts [[_record_expr_base _map_expr_base]
             [macro_call_expr macro_call_none]
             [_expr_max _concatable]
             [_expr_max _name]
             [_function_or_macro_clause _cr_clause_or_macro _macro_body_expr]
             [_macro_def_replacement replacement_guard_or]
             [_macro_def_replacement replacement_guard_and]
             [fun_type expr_args]
             [list list_comprehension]
             [map_comprehension map_expr]
             [_expr _map_expr_base _record_expr_base]
             [_expr _map_expr_base]
             [_expr _record_expr_base]
             [_function_or_macro_clause _macro_body_expr]]
 :precedences []
 :externals [_tq_string _tq_sigil_string error_sentinel]
 :inline [_fun_expr _map_expr _record_expr _exprs _catch_clauses]
 :supertypes [_form
              _preprocessor_directive
              _include_detail
              _function_or_macro_clause
              _macro_def_replacement
              _arity_value
              _concatable
              _name
              _macro_name
              _lc_expr
              _cr_clause_or_macro
              _bit_type
              _bit_expr
              _map_expr_base
              _record_expr_base
              _expr
              _expr_max
              _catch_pat
              _deprecated_details
              _deprecated_fun_arity
              _desc
              _string_like]
 :rules
 {source_file (:choice
               (:field :forms_only (:repeat _form))
               (:field :exprs (:repeat (:seq _expr (:choice "." :blank)))))
  _form (:choice
         module_attribute
         behaviour_attribute
         export_attribute
         import_attribute
         import_record_attribute
         export_type_attribute
         export_record_attribute
         optional_callbacks_attribute
         compile_options_attribute
         feature_attribute
         file_attribute
         deprecated_attribute
         record_decl
         type_alias
         nominal
         opaque
         spec
         callback
         wild_attribute
         fun_decl
         _preprocessor_directive
         ssr_definition
         shebang)
  ssr_definition (:prec-right 0
                  (:seq
                   "ssr"
                   ":"
                   (:field :lhs _expr)
                   (:field :rhs (:choice ssr_replacement :blank))
                   (:field :where (:choice ssr_where :blank))
                   "."))
  ssr_replacement (:seq "==>>" (:field :expr _expr))
  ssr_where (:seq "where" (:field :guard guard))
  _preprocessor_directive (:choice
                           pp_include
                           pp_include_lib
                           pp_undef
                           pp_ifdef
                           pp_ifndef
                           pp_else
                           pp_endif
                           pp_if
                           pp_elif
                           pp_define)
  pp_include (:seq
              "-"
              (:choice "include" (:alias "'include'" "include"))
              "("
              (:field :file (:repeat1 _include_detail))
              ")"
              ".")
  pp_include_lib (:seq
                  "-"
                  (:choice "include_lib" (:alias "'include_lib'" "include_lib"))
                  "("
                  (:field :file (:repeat1 _include_detail))
                  ")"
                  ".")
  pp_undef (:seq
            "-"
            (:choice "undef" (:alias "'undef'" "undef"))
            "("
            (:field :name _macro_name)
            ")"
            ".")
  pp_ifdef (:seq
            "-"
            (:choice "ifdef" (:alias "'ifdef'" "ifdef"))
            "("
            (:field :name _macro_name)
            ")"
            ".")
  pp_ifndef (:seq
             "-"
             (:choice "ifndef" (:alias "'ifndef'" "ifndef"))
             "("
             (:field :name _macro_name)
             ")"
             ".")
  pp_else (:seq "-" (:choice "else" (:alias "'else'" "else")) ".")
  pp_endif (:seq "-" (:choice "endif" (:alias "'endif'" "endif")) ".")
  pp_if (:prec 20 (:seq "-" (:choice "if" (:alias "'if'" "if")) (:field :cond _expr) "."))
  pp_elif (:seq "-" (:choice "elif" (:alias "'elif'" "elif")) (:field :cond _expr) ".")
  pp_define (:seq
             "-"
             (:choice "define" (:alias "'define'" "define"))
             "("
             (:field :lhs macro_lhs)
             ","
             (:field :replacement (:choice _macro_def_replacement :blank))
             ")"
             ".")
  _include_detail (:choice string macro_call_expr)
  module_attribute (:seq
                    "-"
                    (:choice "module" (:alias "'module'" "module"))
                    "("
                    (:field :name _name)
                    ")"
                    ".")
  behaviour_attribute (:seq
                       "-"
                       (:choice
                        (:choice "behaviour" (:alias "'behaviour'" "behaviour"))
                        (:choice "behavior" (:alias "'behavior'" "behavior")))
                       "("
                       (:field :name _name)
                       ")"
                       ".")
  export_attribute (:seq
                    "-"
                    (:choice "export" (:alias "'export'" "export"))
                    "("
                    "["
                    (:choice
                     (:seq
                      (:field :funs fa)
                      (:repeat (:seq (:choice "," :blank) (:field :funs fa))))
                     :blank)
                    "]"
                    ")"
                    ".")
  import_attribute (:seq
                    "-"
                    (:choice "import" (:alias "'import'" "import"))
                    "("
                    (:field :module _name)
                    ","
                    "["
                    (:choice
                     (:seq
                      (:field :funs fa)
                      (:repeat (:seq (:choice "," :blank) (:field :funs fa))))
                     :blank)
                    "]"
                    ")"
                    ".")
  import_record_attribute (:seq
                           "-"
                           (:choice "import_record" (:alias "'import_record'" "import_record"))
                           "("
                           (:field :module _name)
                           ","
                           (:field :records import_record_names)
                           ")"
                           ".")
  import_record_names (:seq
                       "["
                       (:choice
                        (:seq
                         (:field :names _name)
                         (:repeat (:seq (:choice "," :blank) (:field :names _name))))
                        :blank)
                       "]")
  optional_callbacks_attribute (:seq
                                "-"
                                (:choice
                                 "optional_callbacks"
                                 (:alias "'optional_callbacks'" "optional_callbacks"))
                                "("
                                "["
                                (:choice
                                 (:seq
                                  (:field :callbacks fa)
                                  (:repeat (:seq (:choice "," :blank) (:field :callbacks fa))))
                                 :blank)
                                "]"
                                ")"
                                ".")
  fa (:seq (:field :fun _name) (:field :arity arity))
  export_type_attribute (:seq
                         "-"
                         (:choice "export_type" (:alias "'export_type'" "export_type"))
                         "("
                         "["
                         (:choice
                          (:seq
                           (:field :types fa)
                           (:repeat (:seq (:choice "," :blank) (:field :types fa))))
                          :blank)
                         "]"
                         ")"
                         ".")
  export_record_attribute (:seq
                           "-"
                           (:choice "export_record" (:alias "'export_record'" "export_record"))
                           "("
                           "["
                           (:choice
                            (:seq
                             (:field :records atom)
                             (:repeat (:seq (:choice "," :blank) (:field :records atom))))
                            :blank)
                           "]"
                           ")"
                           ".")
  compile_options_attribute (:seq
                             "-"
                             (:choice "compile" (:alias "'compile'" "compile"))
                             "("
                             (:field :options _expr)
                             ")"
                             ".")
  file_attribute (:seq
                  "-"
                  (:choice "file" (:alias "'file'" "file"))
                  "("
                  (:field :original_file string)
                  ","
                  (:field :original_line integer)
                  ")"
                  ".")
  deprecated_attribute (:seq
                        "-"
                        (:choice "deprecated" (:alias "'deprecated'" "deprecated"))
                        "("
                        (:field :attr _deprecated_details)
                        ")"
                        ".")
  feature_attribute (:seq
                     "-"
                     (:choice "feature" (:alias "'feature'" "feature"))
                     "("
                     (:field :feature _expr)
                     ","
                     (:field :flag _expr)
                     ")"
                     ".")
  _deprecated_details (:choice deprecated_module deprecated_fa deprecated_fas)
  deprecated_module (:field :module atom)
  deprecated_fas (:seq
                  "["
                  (:seq (:field :fa deprecated_fa) (:repeat (:seq "," (:field :fa deprecated_fa))))
                  "]")
  deprecated_fa (:seq
                 "{"
                 (:field :fun atom)
                 ","
                 (:field :arity _deprecated_fun_arity)
                 (:field :desc (:choice deprecation_desc :blank))
                 "}")
  deprecation_desc (:seq "," (:field :desc _desc))
  _desc (:choice (:field :atom atom) (:field :comment multi_string))
  multi_string (:prec-right 0 (:field :elems (:repeat1 _string_like)))
  _string_like (:choice string _macro_body_expr)
  _deprecated_fun_arity (:choice integer deprecated_wildcard)
  deprecated_wildcard "'_'"
  type_alias (:seq "-" (:choice "type" (:alias "'type'" "type")) _type_def ".")
  nominal (:seq "-" (:choice "nominal" (:alias "'nominal'" "nominal")) _type_def ".")
  opaque (:seq "-" (:choice "opaque" (:alias "'opaque'" "opaque")) _type_def ".")
  _type_def (:choice
             (:seq (:field :name type_name) "::" (:field :ty _expr))
             (:seq "(" (:field :name type_name) "::" (:field :ty _expr) ")"))
  type_name (:seq (:field :name _name) (:field :args var_args))
  record_decl (:choice
               (:seq
                "-"
                (:choice "record" (:alias "'record'" "record"))
                "("
                (:field :name _name)
                (:choice "," :blank)
                _record_tuple
                ")"
                ".")
               (:seq
                "-"
                (:choice "record" (:alias "'record'" "record"))
                "#"
                (:field :name _name)
                _record_tuple
                ".")
               (:seq
                "-"
                (:choice "record" (:alias "'record'" "record"))
                "("
                "#"
                (:field :name _name)
                _record_tuple
                ")"
                "."))
  spec (:seq "-" (:choice "spec" (:alias "'spec'" "spec")) _spec_def ".")
  callback (:seq "-" (:choice "callback" (:alias "'callback'" "callback")) _spec_def ".")
  _spec_def (:choice
             (:seq
              _spec_fun
              (:seq (:field :sigs type_sig) (:repeat (:seq ";" (:field :sigs type_sig)))))
             (:seq
              "("
              _spec_fun
              (:seq (:field :sigs type_sig) (:repeat (:seq ";" (:field :sigs type_sig))))
              ")"))
  _spec_fun (:seq (:field :module (:choice module :blank)) (:field :fun _name))
  module (:seq (:field :name _name) ":")
  wild_attribute (:seq (:field :name attr_name) (:field :value _expr) ".")
  attr_name (:prec 20 (:seq "-" (:field :name _name)))
  fun_decl (:prec-right 0
            (:seq (:field :clause _function_or_macro_clause) (:choice _fun_clause_separator :blank)))
  _fun_clause_separator (:choice ";" ".")
  type_sig (:seq
            (:field :args expr_args)
            "->"
            (:field :ty _expr)
            (:choice (:field :guard type_guards) :blank))
  type_guards (:seq
               "when"
               (:seq (:field :guards ann_type) (:repeat (:seq "," (:field :guards ann_type)))))
  ann_type (:prec 1 (:seq (:field :var ann_var) (:field :ty _expr)))
  ann_var (:prec 1 (:seq (:field :var var) (:choice "::" ":>")))
  pipe (:prec-right 2 (:seq (:field :lhs _expr) "|" (:field :rhs _expr)))
  fun_type (:seq "fun" "(" (:field :sig (:choice fun_type_sig :blank)) ")")
  fun_type_sig (:seq (:field :args expr_args) "->" (:field :ty _expr))
  range_type (:prec-left 6 (:seq (:field :lhs _expr) ".." (:field :rhs _expr)))
  _function_or_macro_clause (:choice function_clause macro_call_expr)
  function_clause (:seq
                   (:field :name _name)
                   (:field :args expr_args)
                   (:choice _clause_guard :blank)
                   (:field :body clause_body))
  _clause_guard (:seq "when" (:field :guard guard))
  clause_body (:prec-right 0
               (:seq "->" (:seq (:field :exprs _expr) (:repeat (:seq "," (:field :exprs _expr))))))
  _expr (:choice
         ann_type
         pipe
         dotdotdot
         range_type
         catch_expr
         binary_op_expr
         match_expr
         unary_op_expr
         _map_expr
         call
         _record_expr
         remote
         _expr_max
         cond_match_expr)
  dotdotdot "..."
  catch_expr (:prec 3 (:seq "catch" (:field :expr _expr)))
  match_expr (:prec-right 5 (:seq (:field :lhs _expr) "=" (:field :rhs _expr)))
  cond_match_expr (:prec-right 4
                   (:seq (:field :lhs _expr) "?=" (:field :rhs (:prec-right 0 _expr))))
  binary_op_expr (:choice
                  (:prec-right 13 (:seq (:field :lhs _expr) "!" (:field :rhs _expr)))
                  (:prec-right 14 (:seq (:field :lhs _expr) "orelse" (:field :rhs _expr)))
                  (:prec-right 15 (:seq (:field :lhs _expr) "andalso" (:field :rhs _expr)))
                  (:prec-left 17 (:seq (:field :lhs _expr) _comp_op (:field :rhs _expr)))
                  (:prec-right 18 (:seq (:field :lhs _expr) _list_op (:field :rhs _expr)))
                  (:prec-left 19 (:seq (:field :lhs _expr) _add_op (:field :rhs _expr)))
                  (:prec-left 20 (:seq (:field :lhs _expr) _mult_op (:field :rhs _expr))))
  unary_op_expr (:prec 21 (:seq _prefix_op (:field :operand _expr)))
  _expr_max (:choice
             char
             integer
             atom
             float
             string
             concatables
             _macro_body_expr
             var
             list
             binary
             list_comprehension
             binary_comprehension
             map_comprehension
             tuple
             paren_expr
             block_expr
             if_expr
             case_expr
             receive_expr
             _fun_expr
             try_expr
             maybe_expr)
  remote (:prec-right 22 (:seq (:field :module remote_module) (:field :fun _expr)))
  remote_module (:prec 22 (:seq (:field :module _expr) ":"))
  paren_expr (:seq "(" (:field :expr _expr) ")")
  block_expr (:seq
              "begin"
              (:seq (:field :exprs _expr) (:repeat (:seq "," (:field :exprs _expr))))
              "end")
  list (:seq
        "["
        (:choice (:seq (:field :exprs _expr) (:repeat (:seq "," (:field :exprs _expr)))) :blank)
        "]")
  binary (:seq
          "<<"
          (:choice
           (:seq (:field :elements bin_element) (:repeat (:seq "," (:field :elements bin_element))))
           :blank)
          ">>")
  bin_element (:seq
               (:field :element _bit_expr)
               (:field :size (:choice bit_size_expr :blank))
               (:field :types (:choice bit_type_list :blank)))
  bit_size_expr (:seq ":" (:field :size _bit_expr))
  bit_type_list (:seq
                 "/"
                 (:seq (:field :types _bit_type) (:repeat (:seq "-" (:field :types _bit_type)))))
  _bit_expr (:prec 2
             (:choice
              (:alias unary_op_expr_max unary_op_expr)
              (:alias binary_op_expr_max binary_op_expr)
              _expr_max))
  unary_op_expr_max (:prec 21 (:seq _prefix_op (:field :operand _expr_max)))
  binary_op_expr_max (:prec 20 (:seq (:field :lhs _expr_max) "*" (:field :rhs _expr_max)))
  _bit_type (:choice _name bit_type_unit)
  bit_type_unit (:seq "unit" ":" (:field :size _arity_value))
  list_comprehension (:seq
                      "["
                      (:seq (:field :exprs _expr) (:repeat (:seq "," (:field :exprs _expr))))
                      (:field :lc_exprs lc_exprs)
                      "]")
  binary_comprehension (:seq "<<" (:field :expr _expr_max) (:field :lc_exprs lc_exprs) ">>")
  map_comprehension (:seq
                     "#"
                     "{"
                     (:seq (:field :exprs map_field) (:repeat (:seq "," (:field :exprs map_field))))
                     (:field :lc_exprs lc_exprs)
                     "}")
  lc_exprs (:seq
            "||"
            (:seq (:field :exprs lc_or_zc_expr) (:repeat (:seq "," (:field :exprs lc_or_zc_expr)))))
  lc_or_zc_expr (:seq (:field :exprs _lc_expr) (:repeat (:seq "&&" (:field :exprs _lc_expr))))
  _lc_expr (:choice _expr generator b_generator map_generator)
  generator (:seq (:field :lhs _expr) _generator_op (:field :rhs _expr))
  b_generator (:seq (:field :lhs _expr) _b_generator_op (:field :rhs _expr))
  map_generator (:seq (:field :lhs map_field) _generator_op (:field :rhs _expr))
  _generator_op (:choice "<-" "<:-")
  _b_generator_op (:choice "<=" "<:=")
  tuple (:seq
         "{"
         (:choice (:seq (:field :expr _expr) (:repeat (:seq "," (:field :expr _expr)))) :blank)
         "}")
  _map_expr (:choice map_expr map_expr_update)
  map_expr_update (:prec-right 0
                   (:seq
                    (:field :expr _map_expr_base)
                    "#"
                    "{"
                    (:choice
                     (:seq
                      (:field :fields map_field)
                      (:repeat (:seq "," (:field :fields map_field))))
                     :blank)
                    "}"))
  map_expr (:seq
            "#"
            "{"
            (:choice
             (:seq (:field :fields map_field) (:repeat (:seq "," (:field :fields map_field))))
             :blank)
            "}")
  _map_expr_base (:choice _expr_max _map_expr)
  map_field (:prec-left 7 (:seq (:field :key _expr) _map_field_op (:field :value _expr)))
  _map_field_op (:choice "=>" ":=")
  _record_expr (:choice
                record_index_expr
                record_field_expr
                record_update_expr
                record_expr
                qualified_record_expr
                qualified_record_field_expr
                qualified_record_update_expr
                anon_record_expr
                anon_record_field_expr
                anon_record_update_expr)
  record_index_expr (:seq (:field :name record_name) (:field :field record_field_name))
  record_field_expr (:prec-right 0
                     (:seq
                      (:field :expr _record_expr_base)
                      (:field :name record_name)
                      (:field :field record_field_name)))
  record_update_expr (:prec-right 0
                      (:seq
                       (:field :expr _record_expr_base)
                       (:field :name record_name)
                       _record_tuple))
  record_expr (:seq (:field :name record_name) _record_tuple)
  record_name (:seq "#" (:field :name _name))
  record_field_name (:seq "." (:field :name _name))
  qualified_record_name (:seq "#" (:field :module module) (:field :name _name))
  qualified_record_expr (:seq (:field :name qualified_record_name) _record_tuple)
  qualified_record_update_expr (:prec-right 0
                                (:seq
                                 (:field :expr _record_expr_base)
                                 (:field :name qualified_record_name)
                                 _record_tuple))
  qualified_record_field_expr (:prec-right 0
                               (:seq
                                (:field :expr _record_expr_base)
                                (:field :name qualified_record_name)
                                (:field :field record_field_name)))
  anon_record_expr (:seq (:token "#_") _record_tuple)
  anon_record_update_expr (:prec-right 0
                           (:seq (:field :expr _record_expr_base) (:token "#_") _record_tuple))
  anon_record_field_expr (:prec-right 0
                          (:seq
                           (:field :expr _record_expr_base)
                           (:token "#_")
                           (:field :field record_field_name)))
  _record_expr_base (:choice _expr_max _record_expr)
  _record_tuple (:seq
                 "{"
                 (:choice
                  (:seq
                   (:field :fields record_field)
                   (:repeat (:seq "," (:field :fields record_field))))
                  :blank)
                 "}")
  record_field (:seq
                (:field :name _name)
                (:choice (:field :expr field_expr) :blank)
                (:choice (:field :ty field_type) :blank))
  field_expr (:seq "=" (:field :expr _expr))
  field_type (:seq "::" (:field :expr _expr))
  call (:prec 80 (:seq (:field :expr _expr) (:field :args expr_args)))
  if_expr (:seq
           "if"
           (:choice
            (:seq (:field :clauses if_clause) (:repeat (:seq ";" (:field :clauses if_clause))))
            :blank)
           "end")
  if_clause (:seq (:field :guard guard) (:field :body clause_body))
  case_expr (:seq "case" (:field :expr _expr) "of" (:choice _cr_clauses :blank) "end")
  _cr_clauses (:seq
               (:field :clauses _cr_clause_or_macro)
               (:repeat (:seq ";" (:field :clauses _cr_clause_or_macro))))
  _cr_clause_or_macro (:choice cr_clause macro_call_expr)
  cr_clause (:seq (:field :pat _expr) (:choice _clause_guard :blank) (:field :body clause_body))
  receive_expr (:seq
                "receive"
                (:choice _cr_clauses :blank)
                (:choice (:field :after receive_after) :blank)
                "end")
  receive_after (:seq "after" (:field :expr _expr) (:field :body clause_body))
  _fun_expr (:choice internal_fun external_fun anonymous_fun fun_type)
  internal_fun (:seq "fun" (:field :fun _name) (:field :arity arity))
  external_fun (:seq "fun" (:field :module module) (:field :fun _name) (:field :arity arity))
  anonymous_fun (:seq
                 "fun"
                 (:seq
                  (:field :clauses fun_clause)
                  (:repeat (:seq ";" (:field :clauses fun_clause))))
                 "end")
  _macro_name (:choice atom var)
  _name (:choice atom var (:alias macro_call_none macro_call_expr))
  arity (:seq "/" (:field :value _arity_value))
  _arity_value (:choice integer var macro_call_expr)
  fun_clauses (:seq fun_clause (:repeat (:seq ";" fun_clause)))
  fun_clause (:seq
              (:field :name (:choice var :blank))
              (:field :args expr_args)
              (:choice _clause_guard :blank)
              (:field :body clause_body))
  try_expr (:choice (:seq "try" _exprs "of" _cr_clauses _try_catch) (:seq "try" _exprs _try_catch))
  _try_catch (:choice
              (:seq "catch" (:choice _catch_clauses :blank) "end")
              (:seq "catch" (:choice _catch_clauses :blank) (:field :after try_after) "end")
              (:seq (:field :after try_after) "end"))
  try_after (:seq "after" _exprs)
  _catch_clauses (:seq
                  (:field :catch catch_clause)
                  (:repeat (:seq ";" (:field :catch catch_clause))))
  catch_clause (:seq
                (:choice
                 (:field :pat _catch_pat)
                 (:seq (:field :class try_class) (:field :pat _catch_pat))
                 (:seq (:field :class try_class) (:field :pat _catch_pat) (:field :stack try_stack)))
                (:choice _clause_guard :blank)
                (:field :body clause_body))
  try_class (:seq (:field :class _name) ":")
  try_stack (:seq ":" (:field :class var))
  _catch_pat (:choice
              (:alias binary_op_catch_pat binary_op_expr)
              (:alias match_catch_pat match_expr)
              unary_op_expr
              map_expr
              record_index_expr
              record_expr
              qualified_record_expr
              anon_record_expr
              _expr_max)
  match_catch_pat (:prec-right 5 (:seq (:field :lhs _catch_pat) "=" (:field :rhs _catch_pat)))
  binary_op_catch_pat (:choice
                       (:prec-left 17
                        (:seq (:field :lhs _catch_pat) _comp_op (:field :rhs _catch_pat)))
                       (:prec-right 18
                        (:seq (:field :lhs _catch_pat) _list_op (:field :rhs _catch_pat)))
                       (:prec-left 19
                        (:seq (:field :lhs _catch_pat) _add_op (:field :rhs _catch_pat)))
                       (:prec-left 20
                        (:seq (:field :lhs _catch_pat) _mult_op (:field :rhs _catch_pat))))
  maybe_expr (:choice
              (:seq
               "maybe"
               (:seq (:field :exprs _expr) (:repeat (:seq "," (:field :exprs _expr))))
               "end")
              (:seq
               "maybe"
               (:seq (:field :exprs _expr) (:repeat (:seq "," (:field :exprs _expr))))
               _maybe_else_clause))
  _maybe_else_clause (:seq "else" (:choice _cr_clauses :blank) "end")
  _macro_def_replacement (:choice
                          (:prec-dynamic 6 _expr)
                          (:prec-dynamic 2 replacement_function_clauses)
                          (:prec-dynamic 1 replacement_cr_clauses)
                          (:prec-dynamic 3 replacement_guard_or)
                          (:prec-dynamic 4 replacement_guard_and)
                          (:prec-dynamic 5 replacement_expr_guard)
                          replacement_parens)
  replacement_function_clauses (:seq
                                (:field :clauses _function_or_macro_clause)
                                (:repeat (:seq ";" (:field :clauses _function_or_macro_clause))))
  replacement_cr_clauses (:seq
                          (:field :clauses _cr_clause_or_macro)
                          (:repeat (:seq ";" (:field :clauses _cr_clause_or_macro))))
  replacement_guard_or (:seq
                        (:field :guard replacement_guard_and)
                        (:repeat (:seq ";" (:field :guard replacement_guard_and))))
  replacement_guard_and (:seq (:field :guard _expr) (:repeat (:seq "," (:field :guard _expr))))
  replacement_expr_guard (:seq (:field :expr _expr) _clause_guard)
  replacement_parens (:seq "(" ")")
  macro_lhs (:seq (:field :name _macro_name) (:field :args (:choice var_args :blank)))
  _macro_body_expr (:choice macro_string macro_call_expr)
  macro_call_expr (:prec-right 0
                   (:seq
                    "?"
                    (:field :name _macro_name)
                    (:field :args (:choice macro_call_args :blank))))
  macro_call_args (:seq
                   "("
                   (:choice
                    (:seq (:field :args macro_expr) (:repeat (:seq "," (:field :args macro_expr))))
                    :blank)
                   ")")
  macro_call_none (:seq "?" (:field :name _macro_name))
  macro_string (:seq "?" "?" (:field :name _macro_name))
  macro_expr (:choice (:field :expr _expr) (:seq (:field :expr _expr) "when" (:field :guard _expr)))
  _exprs (:seq (:field :exprs _expr) (:repeat (:seq "," (:field :exprs _expr))))
  expr_args (:seq
             "("
             (:choice (:seq (:field :args _expr) (:repeat (:seq "," (:field :args _expr)))) :blank)
             ")")
  var_args (:seq
            "("
            (:choice
             (:seq (:field :args var) (:repeat (:seq (:choice "," :blank) (:field :args var))))
             :blank)
            ")")
  guard (:prec-right 0
         (:seq (:field :clauses guard_clause) (:repeat (:seq ";" (:field :clauses guard_clause)))))
  guard_clause (:prec-right 0
                (:seq (:field :exprs _expr) (:repeat (:seq "," (:field :exprs _expr)))))
  concatables (:prec-right 0 (:field :elems (:seq _concatable (:repeat1 _concatable))))
  _concatable (:choice string var _macro_body_expr)
  _prefix_op (:choice "+" "-" "bnot" "not")
  _mult_op (:choice "/" "*" "div" "rem" "band" "and")
  _add_op (:choice "+" "-" "bor" "bxor" "bsl" "bsr" "or" "xor")
  _list_op (:choice "++" "--")
  _comp_op (:choice "==" "/=" "=<" "<" ">=" ">" "=:=" "=/=")
  string (:choice _sq_string _tq_string _tq_sigil_string _sigil_verbatim_string _sigil_string)
  shebang (:token (:pattern "#!.*"))
  var (:token
       (:pattern "[_A-Z\\xC0-\\xD6\\xD8-\\xDE][_@a-zA-Z0-9\\xC0-\\xD6\\xD8-\\xDE\\xDF-\\xF6\\xF8-\\xFF]*"))
  integer (:token
           (:choice (:pattern "\\d(_?\\d)*#[0-9a-zA-Z](_?[0-9a-zA-Z])*") (:pattern "\\d(_?\\d)*")))
  float (:token
         (:choice
          (:pattern "\\d(_?\\d)*\\.\\d(_?\\d)*([eE][+-]?\\d(_?\\d)*)?")
          (:pattern "\\d(_?\\d)*#[0-9a-zA-Z](_?[0-9a-zA-Z])*\\.(_?[0-9a-zA-Z])+(#[eE][+-]?\\d(_?\\d)*)?")))
  _sq_string (:token
              (:seq
               (:pattern "\"")
               (:pattern "([^\"\\\\]|\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.))*")
               (:pattern "\"")))
  _sigil_verbatim_string (:token
                          (:choice
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\(")
                            (:pattern "([^\\)]|\\\\\\))*")
                            (:pattern "\\)"))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\[")
                            (:pattern "([^\\]]|\\\\\\])*")
                            (:pattern "\\]"))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\{")
                            (:pattern "([^\\}]|\\\\\\})*")
                            (:pattern "\\}"))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "<")
                            (:pattern "([^>]|\\\\>)*")
                            (:pattern ">"))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\/")
                            (:pattern "([^\\/]|\\\\\\/)*")
                            (:pattern "\\/"))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\|")
                            (:pattern "([^\\|]|\\\\\\|)*")
                            (:pattern "\\|"))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\'")
                            (:pattern "([^\\']|\\\\\\')*")
                            (:pattern "\\'"))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\\"")
                            (:pattern "([^\\\"]|\\\\\\\")*")
                            (:pattern "\\\""))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\`")
                            (:pattern "([^\\`]|\\\\\\`)*")
                            (:pattern "\\`"))
                           (:seq
                            (:pattern "~[BS]")
                            (:pattern "\\#")
                            (:pattern "([^\\#]|\\\\\\#)*")
                            (:pattern "\\#"))))
  _sigil_string (:token
                 (:choice
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\(")
                   (:repeat
                    (:choice
                     (:pattern "[^\\)]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\)"))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\[")
                   (:repeat
                    (:choice
                     (:pattern "[^\\]]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\]"))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\{")
                   (:repeat
                    (:choice
                     (:pattern "[^\\}]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\}"))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "<")
                   (:repeat
                    (:choice
                     (:pattern "[^>]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern ">"))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\/")
                   (:repeat
                    (:choice
                     (:pattern "[^\\/]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\/"))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\|")
                   (:repeat
                    (:choice
                     (:pattern "[^\\|]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\|"))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\'")
                   (:repeat
                    (:choice
                     (:pattern "[^\\']")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\'"))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\\"")
                   (:repeat
                    (:choice
                     (:pattern "[^\\\"]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\\""))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\`")
                   (:repeat
                    (:choice
                     (:pattern "[^\\`]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\`"))
                  (:seq
                   (:pattern "~[bs]?")
                   (:pattern "\\#")
                   (:repeat
                    (:choice
                     (:pattern "[^\\#]")
                     (:pattern "\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.)")))
                   (:pattern "\\#"))))
  char (:token
        (:pattern "\\$([^\\\\]|\\\\([0-7]{1,3}|x[0-9a-fA-F]{2}|x[0-9a-fA-F]+|\\^.|\\\\n|\\\\\\\\|.))"))
  atom (:token
        (:pattern "([a-z\\xDF-\\xF6\\xF8-\\xFF][_@a-zA-Z0-9\\xC0-\\xD6\\xD8-\\xDE\\xDF-\\xF6\\xF8-\\xFF]*)|('([^'\\\\]|\\\\([^x\\^]|[0-7]{1,3}|x[0-9a-fA-F]{2}|x\\{[0-9a-fA-F]+\\}|\\^.))*')"))
  comment (:token (:pattern "%[^\\n]*"))}}
