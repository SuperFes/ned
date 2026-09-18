# Language grammar as data -- read by Editor/Grammar/Compile/GrammarFile.h;
# the form vocabulary is documented there. Rule order is meaning: the first
# rule is the start rule.
{:name "verilog"
 :word simple_identifier
 :extras [(:pattern "\\s") comment]
 :conflicts [[constant_primary primary]
             [implicit_class_handle primary]
             [param_expression primary]
             [primary queue_dimension]
             [_checker_or_generate_item _module_common_item]
             [_checker_generate_item _module_common_item]
             [dpi_function_import_property dpi_task_import_property]
             [checker_or_generate_item_declaration package_or_generate_item_declaration]
             [_module_or_generate_item_declaration checker_or_generate_item_declaration]
             [interface_or_generate_item module_or_generate_item]
             [array_method_name method_call_body]
             [constraint_set empty_unpacked_array_concatenation]
             [_non_port_interface_item interface_declaration]
             [non_port_program_item program_declaration]
             [list_of_port_declarations list_of_ports]
             [expression_or_dist mintypmax_expression]
             [class_constructor_declaration implicit_class_handle]
             [action_block statement_or_null]
             [ansi_port_declaration port_reference]
             [ansi_port_declaration port]
             [net_port_header1 variable_port_header]
             [_variable_dimension ansi_port_declaration]
             [_non_port_module_item module_declaration]
             [_expression_or_cond_pattern tagged_union_expression]
             [_covergroup_expression mintypmax_expression]
             [_covergroup_expression concatenation]
             [delay2 delay_control]
             [delay3 delay_control]
             [delay_control param_expression]
             [delay2 delay_control param_expression]
             [property_expr property_spec]
             [property_expr sequence_expr]
             [nonrange_select1 select1]
             [class_method constraint_prototype_qualifier]
             [class_method method_qualifier]
             [bind_target_instance bind_target_scope]
             [class_type package_scope]
             [_var_data_type data_type_or_implicit1]
             [list_of_port_identifiers list_of_variable_identifiers]
             [list_of_port_identifiers list_of_variable_port_identifiers]
             [class_type data_type tf_port_item1]
             [class_type data_type interface_port_header net_port_type1]
             [class_type data_type net_port_type1]
             [_variable_dimension list_of_port_identifiers]
             [_sequence_actual_arg property_expr]
             [_hierarchical_event_identifier _sequence_identifier event_control]
             [_hierarchical_event_identifier event_control]
             [let_list_of_arguments sequence_list_of_arguments]
             [input_identifier output_identifier]
             [constant_primary path_delay_expression]
             [scalar_timing_check_condition unary_operator]
             [mintypmax_expression scalar_timing_check_condition]
             [delayed_data delayed_reference]
             [list_of_arguments_parent system_tf_call]
             [class_item_qualifier lifetime]
             [_property_qualifier method_qualifier]
             [class_property data_type_or_implicit1]
             [list_of_arguments_parent mintypmax_expression]
             [module_path_primary tf_call]
             [_package_item package_declaration]
             [concurrent_assertion_item deferred_immediate_assertion_item generate_block_identifier]
             [clockvar variable_lvalue]
             [_seq_input_list combinational_entry]
             [constant_primary primary]
             [let_expression primary]
             [constant_primary let_expression primary]
             [primary tf_call]
             [let_expression primary tf_call]
             [constant_primary let_expression primary tf_call]
             [let_expression primary select_expression tf_call]
             [constant_primary let_expression primary select_expression tf_call]
             [constant_primary primary]
             [primary variable_lvalue]
             [constant_primary net_lvalue]
             [net_lvalue variable_lvalue]
             [constant_primary port_reference]
             [let_expression primary]
             [constant_primary let_expression primary]
             [let_expression primary variable_lvalue]
             [constant_primary let_expression primary variable_lvalue]
             [primary tf_call]
             [primary tf_call variable_lvalue]
             [net_lvalue primary tf_call variable_lvalue]
             [primary sequence_instance tf_call]
             [net_lvalue primary sequence_instance tf_call]
             [let_expression primary tf_call]
             [constant_primary let_expression primary tf_call]
             [let_expression primary tf_call variable_lvalue]
             [constant_primary let_expression primary tf_call variable_lvalue]
             [let_expression port_reference primary tf_call]
             [constant_primary let_expression port_reference primary tf_call variable_lvalue]
             [constant_primary generate_block_identifier]
             [constant_primary generate_block_identifier primary sequence_instance tf_call]
             [constant_primary
              generate_block_identifier
              primary
              sequence_instance
              tf_call
              variable_lvalue]
             [constant_primary
              generate_block_identifier
              port_reference
              primary
              sequence_instance
              tf_call
              variable_lvalue]
             [_sequence_identifier let_expression]
             [_sequence_identifier let_expression primary]
             [_sequence_identifier constant_primary let_expression primary]
             [_sequence_identifier let_expression primary variable_lvalue]
             [_sequence_identifier let_expression sequence_instance tf_call]
             [_sequence_identifier let_expression primary sequence_instance tf_call]
             [_sequence_identifier
              constant_primary
              let_expression
              primary
              sequence_instance
              tf_call]
             [_sequence_identifier
              generate_block_identifier
              let_expression
              primary
              sequence_instance
              tf_call]
             [_sequence_identifier
              generate_block_identifier
              let_expression
              primary
              sequence_instance
              tf_call
              variable_lvalue]
             [_sequence_identifier
              generate_block_identifier
              let_expression
              net_lvalue
              primary
              sequence_instance
              tf_call
              variable_lvalue]
             [_assignment_pattern_expression_type variable_lvalue]
             [_assignment_pattern_expression_type let_expression primary]
             [_assignment_pattern_expression_type let_expression primary tf_call]
             [list_of_arguments_parent sequence_instance]
             [let_expression list_of_arguments_parent]
             [let_expression list_of_arguments_parent sequence_instance]
             [module_path_primary primary]
             [module_path_primary tf_call]
             [constant_primary module_path_primary tf_call]
             [constant_primary let_expression module_path_primary primary tf_call]
             [constant_primary let_expression module_path_primary primary tf_call variable_lvalue]
             [constant_primary data_type]
             [constant_primary data_type generate_block_identifier]
             [constant_primary
              data_type
              generate_block_identifier
              primary
              sequence_instance
              tf_call
              variable_lvalue]
             [class_type data_type]
             [class_type constant_primary data_type]
             [class_type data_type let_expression primary]
             [class_type constant_primary data_type let_expression primary]
             [class_type data_type let_expression primary tf_call]
             [class_type constant_primary data_type let_expression primary tf_call]
             [let_expression primary]
             [primary tf_call]
             [primary sequence_instance tf_call]
             [let_expression primary tf_call]
             [let_expression primary terminal_identifier tf_call]
             [_sequence_identifier let_expression]
             [_sequence_identifier let_expression primary]
             [_sequence_identifier let_expression sequence_instance tf_call]
             [_sequence_identifier let_expression primary sequence_instance tf_call]
             [_sequence_identifier let_expression sequence_instance terminal_identifier tf_call]
             [net_lvalue variable_lvalue]
             [_simple_type constant_primary]
             [constant_primary primary]
             [constant_primary generate_block_identifier]
             [interface_instantiation program_instantiation]
             [interface_instantiation module_instantiation program_instantiation]
             [data_type net_type_declaration]
             [class_type data_type]
             [class_type data_type net_declaration]
             [class_type data_type net_type_declaration]
             [checker_instantiation class_type data_type]
             [checker_instantiation class_type data_type net_declaration]
             [checker_instantiation class_type data_type interface_port_declaration net_declaration]
             [checker_instantiation
              class_type
              data_type
              interface_instantiation
              net_declaration
              program_instantiation]
             [checker_instantiation
              class_type
              data_type
              interface_instantiation
              interface_port_declaration
              net_declaration
              program_instantiation]
             [checker_instantiation
              class_type
              data_type
              interface_instantiation
              module_instantiation
              net_declaration
              program_instantiation
              udp_instantiation]
             [checker_instantiation
              class_type
              data_type
              interface_instantiation
              interface_port_declaration
              module_instantiation
              net_declaration
              program_instantiation
              udp_instantiation]
             [nonrange_variable_lvalue variable_lvalue]
             [_method_call_root class_qualifier]
             [_variable_dimension variable_decl_assignment]
             [_variable_dimension packed_dimension]
             [_variable_dimension packed_dimension variable_decl_assignment]
             [_simple_type constant_primary]
             [_assignment_pattern_expression_type _simple_type class_qualifier constant_primary]
             [constant_primary data_type]
             [_assignment_pattern_expression_type
              _simple_type
              class_qualifier
              constant_primary
              data_type]
             [constant_select1 unpacked_dimension]
             [packed_dimension unpacked_dimension]
             [_constant_part_select_range packed_dimension]
             [_constant_part_select_range packed_dimension unpacked_dimension]
             [_part_select_range packed_dimension]
             [_part_select_range packed_dimension unpacked_dimension]
             [_constant_part_select_range _part_select_range]
             [_constant_part_select_range _part_select_range packed_dimension]
             [inout_port_identifier input_port_identifier]
             [inout_port_identifier output_port_identifier]
             [inout_port_identifier input_port_identifier output_port_identifier]
             [checker_instantiation named_port_connection]
             [checker_instantiation hierarchical_instance]
             [_sequence_actual_arg event_expression]
             [event_expression expression_or_dist]
             [event_expression expression_or_dist named_port_connection]
             [event_expression expression_or_dist ordered_port_connection]
             [event_expression expression_or_dist let_actual_arg]
             [module_path_primary primary]
             [module_path_primary primary_literal]]
 :precedences []
 :externals []
 :inline [hierarchical_identifier
          _hierarchical_net_identifier
          _hierarchical_variable_identifier
          _hierarchical_tf_identifier
          _hierarchical_sequence_identifier
          _hierarchical_property_identifier
          _hierarchical_block_identifier
          _hierarchical_task_identifier
          ps_or_hierarchical_net_identifier
          ps_or_hierarchical_tf_identifier
          ps_or_hierarchical_sequence_identifier
          ps_or_hierarchical_property_identifier
          ps_class_identifier
          ps_covergroup_identifier
          ps_parameter_identifier
          ps_type_identifier
          ps_checker_identifier
          parameter_identifier
          class_identifier
          covergroup_identifier
          enum_identifier
          formal_port_identifier
          genvar_identifier
          specparam_identifier
          tf_identifier
          _type_identifier
          _net_type_identifier
          _variable_identifier
          _udp_identifier
          package_identifier
          dynamic_array_variable_identifier
          class_variable_identifier
          interface_instance_identifier
          interface_identifier
          _module_identifier
          let_identifier
          _net_identifier
          program_identifier
          checker_identifier
          member_identifier
          port_identifier
          _block_identifier
          instance_identifier
          property_identifier
          cover_point_identifier
          cross_identifier]
 :supertypes []
 :rules
 {source_file (:repeat _description)
  double_quoted_string (:seq "\"" (:token-immediate (:prec 1 (:pattern "[^\\\\\"\\n]+"))) "\"")
  include_compiler_directive_standard (:seq
                                       "<"
                                       (:token-immediate (:prec 1 (:pattern "[^\\\\>\\n]+")))
                                       ">")
  include_compiler_directive (:seq
                              (:alias (:pattern "`include") "directive_include")
                              (:choice double_quoted_string include_compiler_directive_standard))
  default_text (:pattern "\\w+")
  macro_text (:pattern "(\\\\(.|\\r?\\n)|[^\\\\\\n])*")
  text_macro_name (:seq
                   text_macro_identifier
                   (:choice (:prec-left 0 (:seq "(" list_of_formal_arguments ")")) :blank))
  list_of_formal_arguments (:prec-left 0
                            (:seq
                             formal_argument
                             (:repeat (:prec-left 0 (:seq "," formal_argument)))))
  formal_argument (:seq simple_identifier (:choice (:prec-left 0 (:seq "=" default_text)) :blank))
  text_macro_identifier _identifier
  text_macro_definition (:seq
                         (:alias (:pattern "`define") "directive_define")
                         text_macro_name
                         (:choice macro_text :blank)
                         "\n")
  text_macro_usage (:seq
                    "`"
                    text_macro_identifier
                    (:choice (:prec-left 0 (:seq "(" list_of_actual_arguments ")")) :blank))
  simple_text_macro_usage (:seq "`" text_macro_identifier)
  id_directive (:seq
                (:choice
                 (:alias (:pattern "`ifdef") "directive_ifdef")
                 (:alias (:pattern "`ifndef") "directive_ifndef")
                 (:alias (:pattern "`elsif") "directive_elsif")
                 (:alias (:pattern "`undef") "directive_undef"))
                text_macro_identifier)
  zero_directive (:choice
                  (:alias (:pattern "`resetall") "directive_resetall")
                  (:alias (:pattern "`undefineall") "directive_undefineall")
                  (:alias (:pattern "`endif") "directive_endif")
                  (:alias (:pattern "`else") "directive_else")
                  (:alias (:pattern "`nounconnected_drive") "directive_nounconnected_drive")
                  (:alias (:pattern "`celldefine") "directive_celldefine")
                  (:alias (:pattern "`endcelldefine") "directive_endcelldefine")
                  (:alias (:pattern "`end_keywords") "directive_end_keywords"))
  timescale_compiler_directive (:seq
                                (:alias (:pattern "`timescale") "directive_timescale")
                                time_literal
                                "/"
                                time_literal
                                "\n")
  default_nettype_compiler_directive (:seq
                                      (:alias
                                       (:pattern "`default_nettype")
                                       "directive_default_nettype")
                                      default_nettype_value
                                      "\n")
  default_nettype_value (:choice
                         "wire"
                         "tri"
                         "tri0"
                         "tri1"
                         "wand"
                         "triand"
                         "wor"
                         "trior"
                         "trireg"
                         "uwire"
                         "none")
  unconnected_drive (:seq
                     (:alias (:pattern "`unconnected_drive") "directive_unconnected_drive")
                     (:choice "pull0" "pull1")
                     "\n")
  line_compiler_directive (:seq
                           (:alias (:pattern "`line") "directive_line")
                           unsigned_number
                           double_quoted_string
                           unsigned_number
                           "\n")
  begin_keywords (:seq
                  (:alias (:pattern "`begin_keywords") "directive_begin_keywords")
                  double_quoted_string)
  _directives (:choice
               line_compiler_directive
               include_compiler_directive
               text_macro_definition
               text_macro_usage
               id_directive
               zero_directive
               timescale_compiler_directive
               default_nettype_compiler_directive
               unconnected_drive
               begin_keywords)
  list_of_actual_arguments (:prec-left 0
                            (:seq
                             _actual_argument
                             (:repeat (:prec-left 0 (:seq "," _actual_argument)))))
  _actual_argument expression
  _description (:choice
                _directives
                module_declaration
                udp_declaration
                interface_declaration
                program_declaration
                package_declaration
                (:seq (:repeat attribute_instance) _package_item)
                (:seq (:repeat attribute_instance) bind_directive))
  module_header (:seq
                 (:repeat attribute_instance)
                 module_keyword
                 (:choice lifetime :blank)
                 _module_identifier)
  module_nonansi_header (:seq
                         (:repeat package_import_declaration)
                         (:choice parameter_port_list :blank)
                         list_of_ports)
  module_ansi_header (:seq
                      (:repeat package_import_declaration)
                      (:choice
                       (:seq parameter_port_list (:choice list_of_port_declarations :blank))
                       list_of_port_declarations))
  module_declaration (:choice
                      (:seq
                       module_header
                       (:choice
                        (:choice module_nonansi_header module_ansi_header (:seq "(" ".*" ")"))
                        :blank)
                       ";"
                       (:choice timeunits_declaration :blank)
                       (:repeat _module_item)
                       "endmodule"
                       (:choice (:prec-left 0 (:seq ":" _module_identifier)) :blank))
                      (:seq
                       "extern"
                       module_header
                       (:choice module_nonansi_header module_ansi_header)))
  module_keyword (:choice "module" "macromodule")
  interface_declaration (:choice
                         (:seq
                          interface_nonansi_header
                          (:choice timeunits_declaration :blank)
                          (:repeat interface_item)
                          "endinterface"
                          (:choice (:prec-left 0 (:seq ":" interface_identifier)) :blank))
                         (:seq
                          interface_ansi_header
                          (:choice timeunits_declaration :blank)
                          (:repeat _non_port_interface_item)
                          "endinterface"
                          (:choice (:prec-left 0 (:seq ":" interface_identifier)) :blank))
                         (:seq
                          (:repeat attribute_instance)
                          "interface"
                          interface_identifier
                          "("
                          ".*"
                          ")"
                          ";"
                          (:choice timeunits_declaration :blank)
                          (:repeat interface_item)
                          "endinterface"
                          (:choice (:prec-left 0 (:seq ":" interface_identifier)) :blank))
                         (:seq "extern" interface_nonansi_header)
                         (:seq "extern" interface_ansi_header))
  interface_nonansi_header (:seq
                            (:repeat attribute_instance)
                            "interface"
                            (:choice lifetime :blank)
                            interface_identifier
                            (:repeat package_import_declaration)
                            (:choice parameter_port_list :blank)
                            list_of_ports
                            ";")
  interface_ansi_header (:seq
                         (:repeat attribute_instance)
                         "interface"
                         (:choice lifetime :blank)
                         interface_identifier
                         (:repeat package_import_declaration)
                         (:choice parameter_port_list :blank)
                         (:choice list_of_port_declarations :blank)
                         ";")
  program_declaration (:choice
                       (:seq
                        program_nonansi_header
                        (:choice timeunits_declaration :blank)
                        (:repeat program_item)
                        "endprogram"
                        (:choice (:prec-left 0 (:seq ":" program_identifier)) :blank))
                       (:seq
                        program_ansi_header
                        (:choice timeunits_declaration :blank)
                        (:repeat non_port_program_item)
                        "endprogram"
                        (:choice (:prec-left 0 (:seq ":" program_identifier)) :blank))
                       (:seq
                        (:repeat attribute_instance)
                        "program"
                        program_identifier
                        "("
                        ".*"
                        ")"
                        ";"
                        (:choice timeunits_declaration :blank)
                        (:repeat program_item)
                        "endprogram"
                        (:choice (:prec-left 0 (:seq ":" program_identifier)) :blank))
                       (:seq "extern" program_nonansi_header)
                       (:seq "extern" program_ansi_header))
  program_nonansi_header (:seq
                          (:repeat attribute_instance)
                          "program"
                          (:choice lifetime :blank)
                          program_identifier
                          (:repeat package_import_declaration)
                          (:choice parameter_port_list :blank)
                          list_of_ports
                          ";")
  program_ansi_header (:seq
                       (:repeat attribute_instance)
                       "program"
                       (:choice lifetime :blank)
                       program_identifier
                       (:repeat package_import_declaration)
                       (:choice parameter_port_list :blank)
                       (:choice list_of_port_declarations :blank)
                       ";")
  checker_declaration (:seq
                       "checker"
                       checker_identifier
                       (:choice
                        (:prec-left 0 (:seq "(" (:choice checker_port_list :blank) ")"))
                        :blank)
                       ";"
                       (:repeat
                        (:prec-left 0 (:seq (:repeat attribute_instance) _checker_or_generate_item)))
                       "endchecker"
                       (:choice (:prec-left 0 (:seq ":" checker_identifier)) :blank))
  class_declaration (:seq
                     (:choice "virtual" :blank)
                     "class"
                     (:choice lifetime :blank)
                     class_identifier
                     (:choice parameter_port_list :blank)
                     (:choice
                      (:prec-left 0
                       (:seq "extends" class_type (:choice list_of_arguments_parent :blank)))
                      :blank)
                     (:choice
                      (:prec-left 0
                       (:seq
                        "implements"
                        (:prec-left 0
                         (:seq
                          interface_class_type
                          (:repeat (:prec-left 0 (:seq "," interface_class_type)))))))
                      :blank)
                     ";"
                     (:repeat class_item)
                     "endclass"
                     (:choice (:prec-left 0 (:seq ":" class_identifier)) :blank))
  interface_class_type (:seq ps_class_identifier (:choice parameter_value_assignment :blank))
  interface_class_declaration (:seq
                               "interface"
                               "class"
                               class_identifier
                               (:choice parameter_port_list :blank)
                               (:choice
                                (:prec-left 0
                                 (:seq
                                  "extends"
                                  (:choice
                                   (:prec-left 0
                                    (:seq
                                     interface_class_type
                                     (:repeat (:prec-left 0 (:seq "," interface_class_type)))))
                                   :blank)
                                  ";"))
                                :blank)
                               (:repeat interface_class_item)
                               "endclass"
                               (:choice (:prec-left 0 (:seq ":" class_identifier)) :blank))
  interface_class_item (:choice
                        type_declaration
                        (:seq (:repeat attribute_instance) interface_class_method)
                        (:seq _any_parameter_declaration ";")
                        ";")
  interface_class_method (:seq "pure" "virtual" _method_prototype ";")
  package_declaration (:seq
                       (:repeat attribute_instance)
                       "package"
                       (:choice lifetime :blank)
                       package_identifier
                       ";"
                       (:choice timeunits_declaration :blank)
                       (:repeat (:prec-left 0 (:seq (:repeat attribute_instance) _package_item)))
                       "endpackage"
                       (:choice (:prec-left 0 (:seq ":" package_identifier)) :blank))
  timeunits_declaration (:choice
                         (:prec-left 0
                          (:seq
                           "timeunit"
                           time_literal
                           (:choice (:prec-left 0 (:seq "/" time_literal)) :blank)
                           ";"))
                         (:prec-left 0 (:seq "timeprecision" time_literal ";"))
                         (:prec-left 0
                          (:seq "timeunit" time_literal ";" "timeprecision" time_literal ";"))
                         (:prec-left 0
                          (:seq "timeprecision" time_literal ";" "timeunit" time_literal ";")))
  parameter_port_list (:seq
                       "#"
                       "("
                       (:choice
                        (:choice
                         (:seq
                          list_of_param_assignments
                          (:repeat (:prec-left 0 (:seq "," parameter_port_declaration))))
                         (:prec-left 0
                          (:seq
                           parameter_port_declaration
                           (:repeat (:prec-left 0 (:seq "," parameter_port_declaration))))))
                        :blank)
                       ")")
  parameter_port_declaration (:choice
                              _any_parameter_declaration
                              (:seq data_type list_of_param_assignments)
                              (:seq "type" list_of_type_assignments))
  list_of_ports (:seq
                 "("
                 (:choice
                  (:prec-left 0
                   (:seq
                    (:seq
                     (:choice line_compiler_directive :blank)
                     port
                     (:choice line_compiler_directive :blank))
                    (:repeat
                     (:prec-left 0
                      (:seq
                       ","
                       (:seq
                        (:choice line_compiler_directive :blank)
                        port
                        (:choice line_compiler_directive :blank)))))))
                  :blank)
                 ")")
  list_of_port_declarations (:seq
                             "("
                             (:choice
                              (:prec-left 0
                               (:seq
                                (:seq (:repeat attribute_instance) ansi_port_declaration)
                                (:repeat
                                 (:prec-left 0
                                  (:seq
                                   ","
                                   (:seq (:repeat attribute_instance) ansi_port_declaration))))))
                              :blank)
                             ")")
  port_declaration (:seq
                    (:repeat attribute_instance)
                    (:choice
                     inout_declaration
                     input_declaration
                     output_declaration
                     ref_declaration
                     interface_port_declaration))
  port (:choice
        _port_expression
        (:seq "." port_identifier "(" (:choice _port_expression :blank) ")"))
  _port_expression (:choice
                    port_reference
                    (:seq
                     "{"
                     (:prec-left 0
                      (:seq port_reference (:repeat (:prec-left 0 (:seq "," port_reference)))))
                     "}"))
  port_reference (:seq port_identifier (:choice constant_select1 :blank))
  port_direction (:choice "input" "output" "inout" "ref")
  net_port_header1 (:choice (:seq (:choice port_direction :blank) net_port_type1) port_direction)
  variable_port_header (:seq (:choice port_direction :blank) _variable_port_type)
  interface_port_header (:seq
                         (:choice interface_identifier "interface")
                         (:choice (:prec-left 0 (:seq "." modport_identifier)) :blank))
  ansi_port_declaration (:choice
                         (:seq
                          (:choice (:choice net_port_header1 interface_port_header) :blank)
                          port_identifier
                          (:repeat unpacked_dimension)
                          (:choice (:prec-left 0 (:seq "=" constant_expression)) :blank))
                         (:seq
                          (:choice variable_port_header :blank)
                          port_identifier
                          (:repeat _variable_dimension)
                          (:choice (:prec-left 0 (:seq "=" constant_expression)) :blank))
                         (:seq
                          (:choice port_direction :blank)
                          "."
                          port_identifier
                          "("
                          (:choice expression :blank)
                          ")"))
  elaboration_system_task (:choice
                           (:seq
                            "$fatal"
                            (:choice
                             (:prec-left 0
                              (:seq
                               "("
                               finish_number
                               (:choice (:prec-left 0 (:seq "," list_of_arguments)) :blank)
                               ")"))
                             :blank)
                            ";")
                           (:seq
                            (:choice "$error" "$warning" "$info")
                            (:choice list_of_arguments_parent :blank)
                            ";"))
  finish_number (:choice "0" "1" "2")
  _module_common_item (:choice
                       _module_or_generate_item_declaration
                       interface_instantiation
                       program_instantiation
                       _assertion_item
                       bind_directive
                       continuous_assign
                       net_alias
                       initial_construct
                       final_construct
                       always_construct
                       loop_generate_construct
                       _conditional_generate_construct
                       elaboration_system_task)
  _module_item (:choice (:seq port_declaration ";") _non_port_module_item)
  module_or_generate_item (:seq
                           (:repeat attribute_instance)
                           (:choice
                            parameter_override
                            gate_instantiation
                            udp_instantiation
                            module_instantiation
                            _module_common_item))
  _module_or_generate_item_declaration (:choice
                                        package_or_generate_item_declaration
                                        genvar_declaration
                                        clocking_declaration
                                        (:seq "default" "clocking" clocking_identifier ";")
                                        (:seq "default" "disable" "iff" expression_or_dist ";"))
  _non_port_module_item (:choice
                         _directives
                         generate_region
                         module_or_generate_item
                         specify_block
                         (:seq (:repeat attribute_instance) specparam_declaration)
                         program_declaration
                         module_declaration
                         interface_declaration
                         timeunits_declaration)
  parameter_override (:seq "defparam" list_of_defparam_assignments ";")
  bind_directive (:seq
                  "bind"
                  (:choice
                   (:seq
                    bind_target_scope
                    (:choice (:prec-left 0 (:seq ":" bind_target_instance_list)) :blank))
                   bind_target_instance)
                  _bind_instantiation
                  ";")
  bind_target_scope (:choice _module_identifier)
  bind_target_instance (:seq hierarchical_identifier (:choice constant_bit_select1 :blank))
  bind_target_instance_list (:prec-left 0
                             (:seq
                              bind_target_instance
                              (:repeat (:prec-left 0 (:seq "," bind_target_instance)))))
  _bind_instantiation (:choice
                       program_instantiation
                       module_instantiation
                       interface_instantiation
                       checker_instantiation)
  config_declaration (:seq
                      "config"
                      config_identifier
                      ";"
                      (:repeat (:prec-left 0 (:seq local_parameter_declaration ";")))
                      design_statement
                      (:repeat config_rule_statement)
                      "endconfig"
                      (:choice (:prec-left 0 (:seq ":" config_identifier)) :blank))
  design_statement (:seq
                    "design"
                    (:repeat
                     (:prec-left 0
                      (:seq
                       (:choice (:prec-left 0 (:seq library_identifier ".")) :blank)
                       cell_identifier)))
                    ";")
  config_rule_statement (:choice
                         (:seq default_clause liblist_clause ";")
                         (:seq inst_clause liblist_clause ";")
                         (:seq inst_clause use_clause ";")
                         (:seq cell_clause liblist_clause ";")
                         (:seq cell_clause use_clause ";"))
  default_clause "default"
  inst_clause (:seq "instance" inst_name)
  inst_name (:seq topmodule_identifier (:repeat (:prec-left 0 (:seq "." instance_identifier))))
  cell_clause (:seq
               "cell"
               (:choice (:prec-left 0 (:seq library_identifier ".")) :blank)
               cell_identifier)
  liblist_clause (:seq "liblist" (:repeat library_identifier))
  use_clause (:seq
              "use"
              (:choice
               (:prec-left 0
                (:seq
                 named_parameter_assignment
                 (:repeat (:prec-left 0 (:seq "," named_parameter_assignment)))))
               (:seq
                (:choice (:prec-left 0 (:seq library_identifier ".")) :blank)
                cell_identifier
                (:choice
                 (:prec-left 0
                  (:seq
                   named_parameter_assignment
                   (:repeat (:prec-left 0 (:seq "," named_parameter_assignment)))))
                 :blank)))
              (:choice (:prec-left 0 (:seq ":" "config")) :blank))
  interface_or_generate_item (:choice
                              (:seq (:repeat attribute_instance) _module_common_item)
                              (:seq (:repeat attribute_instance) extern_tf_declaration))
  extern_tf_declaration (:choice
                         (:seq "extern" _method_prototype ";")
                         (:seq "extern" "forkjoin" task_prototype ";"))
  interface_item (:choice (:seq port_declaration ";") _non_port_interface_item)
  _non_port_interface_item (:choice
                            generate_region
                            interface_or_generate_item
                            program_declaration
                            modport_declaration
                            interface_declaration
                            timeunits_declaration)
  program_item (:choice (:seq port_declaration ";") non_port_program_item)
  non_port_program_item (:choice
                         (:seq (:repeat attribute_instance) continuous_assign)
                         (:seq (:repeat attribute_instance) _module_or_generate_item_declaration)
                         (:seq (:repeat attribute_instance) initial_construct)
                         (:seq (:repeat attribute_instance) final_construct)
                         (:seq (:repeat attribute_instance) concurrent_assertion_item)
                         timeunits_declaration
                         _program_generate_item)
  _program_generate_item (:choice
                          loop_generate_construct
                          _conditional_generate_construct
                          generate_region
                          elaboration_system_task)
  checker_port_list (:prec-left 0
                     (:seq checker_port_item (:repeat (:prec-left 0 (:seq "," checker_port_item)))))
  checker_port_item (:seq
                     (:repeat attribute_instance)
                     (:choice checker_port_direction :blank)
                     (:choice property_formal_type1 :blank)
                     formal_port_identifier
                     (:repeat _variable_dimension)
                     (:choice (:prec-left 0 (:seq "=" _property_actual_arg)) :blank))
  checker_port_direction (:choice "input" "output")
  _checker_or_generate_item (:choice
                             checker_or_generate_item_declaration
                             initial_construct
                             always_construct
                             final_construct
                             _assertion_item
                             continuous_assign
                             _checker_generate_item)
  checker_or_generate_item_declaration (:choice
                                        (:seq (:choice "rand" :blank) data_declaration)
                                        function_declaration
                                        checker_declaration
                                        _assertion_item_declaration
                                        covergroup_declaration
                                        genvar_declaration
                                        clocking_declaration
                                        (:seq "default" "clocking" clocking_identifier ";")
                                        (:prec-right 11
                                         (:seq "default" "disable" "iff" expression_or_dist ";"))
                                        ";")
  _checker_generate_item (:choice
                          loop_generate_construct
                          _conditional_generate_construct
                          generate_region
                          elaboration_system_task)
  class_item (:choice
              _directives
              (:seq (:repeat attribute_instance) class_property)
              (:seq (:repeat attribute_instance) class_method)
              (:seq (:repeat attribute_instance) _class_constraint)
              (:seq (:repeat attribute_instance) class_declaration)
              (:seq (:repeat attribute_instance) covergroup_declaration)
              (:seq _any_parameter_declaration ";")
              ";")
  class_property (:choice
                  (:seq (:repeat _property_qualifier) data_declaration)
                  (:seq
                   "const"
                   (:repeat class_item_qualifier)
                   data_type
                   const_identifier
                   (:choice (:prec-left 0 (:seq "=" constant_expression)) :blank)
                   ";"))
  class_method (:choice
                (:seq (:repeat method_qualifier) task_declaration)
                (:seq (:repeat method_qualifier) function_declaration)
                (:seq "pure" "virtual" (:repeat class_item_qualifier) _method_prototype ";")
                (:seq "extern" (:repeat method_qualifier) _method_prototype ";")
                (:seq (:repeat method_qualifier) class_constructor_declaration)
                (:seq "extern" (:repeat method_qualifier) class_constructor_prototype))
  class_constructor_prototype (:seq
                               "function"
                               "new"
                               (:choice
                                (:prec-left 0 (:seq "(" (:choice tf_port_list :blank) ")"))
                                :blank)
                               ";")
  _class_constraint (:choice constraint_prototype constraint_declaration)
  class_item_qualifier (:choice "static" "protected" "local")
  _property_qualifier (:choice random_qualifier class_item_qualifier)
  random_qualifier (:choice "rand" "randc")
  method_qualifier (:choice (:seq (:choice "pure" :blank) "virtual") class_item_qualifier)
  _method_prototype (:choice task_prototype function_prototype)
  class_constructor_declaration (:seq
                                 "function"
                                 (:choice class_scope :blank)
                                 "new"
                                 (:choice
                                  (:prec-left 0 (:seq "(" (:choice tf_port_list :blank) ")"))
                                  :blank)
                                 ";"
                                 (:repeat block_item_declaration)
                                 (:choice
                                  (:prec-left 0
                                   (:seq
                                    "super"
                                    "."
                                    "new"
                                    (:choice list_of_arguments_parent :blank)
                                    ";"))
                                  :blank)
                                 (:repeat function_statement_or_null)
                                 "endfunction"
                                 (:choice (:prec-left 0 (:seq ":" "new")) :blank))
  constraint_declaration (:seq
                          (:choice "static" :blank)
                          "constraint"
                          constraint_identifier
                          constraint_block)
  constraint_block (:seq "{" (:repeat constraint_block_item) "}")
  constraint_block_item (:choice
                         (:seq "solve" solve_before_list "before" solve_before_list ";")
                         constraint_expression)
  solve_before_list (:prec-left 0
                     (:seq
                      constraint_primary
                      (:repeat (:prec-left 0 (:seq "," constraint_primary)))))
  constraint_primary (:seq
                      (:choice (:choice (:seq implicit_class_handle ".") class_scope) :blank)
                      hierarchical_identifier
                      (:choice select1 :blank))
  constraint_expression (:choice
                         (:seq (:choice "soft" :blank) expression_or_dist ";")
                         (:seq uniqueness_constraint ";")
                         (:prec-right 22 (:seq expression "–>" constraint_set))
                         (:prec-left 0
                          (:seq
                           "if"
                           "("
                           expression
                           ")"
                           constraint_set
                           (:choice (:prec-left 0 (:seq "else" constraint_set)) :blank)))
                         (:seq
                          "foreach"
                          "("
                          ps_or_hierarchical_array_identifier
                          "["
                          (:choice loop_variables1 :blank)
                          "]"
                          ")"
                          constraint_set)
                         (:seq "disable" "soft" constraint_primary ";"))
  uniqueness_constraint (:seq "unique" "{" open_range_list "}")
  constraint_set (:choice constraint_expression (:seq "{" (:repeat constraint_expression) "}"))
  dist_list (:prec-left 0 (:seq dist_item (:repeat (:prec-left 0 (:seq "," dist_item)))))
  dist_item (:seq value_range (:choice dist_weight :blank))
  dist_weight (:seq (:choice ":=" ":/") expression)
  constraint_prototype (:seq
                        (:choice constraint_prototype_qualifier :blank)
                        (:choice "static" :blank)
                        "constraint"
                        constraint_identifier
                        ";")
  constraint_prototype_qualifier (:choice "extern" "pure")
  extern_constraint_declaration (:seq
                                 (:choice "static" :blank)
                                 "constraint"
                                 class_scope
                                 constraint_identifier
                                 constraint_block)
  identifier_list (:prec-left 0 (:seq _identifier (:repeat (:prec-left 0 (:seq "," _identifier)))))
  _package_item (:choice
                 package_or_generate_item_declaration
                 anonymous_program
                 package_export_declaration
                 timeunits_declaration)
  package_or_generate_item_declaration (:choice
                                        net_declaration
                                        data_declaration
                                        task_declaration
                                        function_declaration
                                        checker_declaration
                                        dpi_import_export
                                        extern_constraint_declaration
                                        class_declaration
                                        interface_class_declaration
                                        class_constructor_declaration
                                        (:seq _any_parameter_declaration ";")
                                        covergroup_declaration
                                        overload_declaration
                                        _assertion_item_declaration
                                        ";")
  anonymous_program (:seq "program" ";" (:repeat anonymous_program_item) "endprogram")
  anonymous_program_item (:choice
                          task_declaration
                          function_declaration
                          class_declaration
                          covergroup_declaration
                          class_constructor_declaration
                          ";")
  local_parameter_declaration (:seq
                               "localparam"
                               (:choice
                                (:seq
                                 (:choice data_type_or_implicit1 :blank)
                                 list_of_param_assignments)
                                (:seq "type" list_of_type_assignments)))
  parameter_declaration (:seq
                         "parameter"
                         (:choice
                          (:seq (:choice data_type_or_implicit1 :blank) list_of_param_assignments)
                          (:seq "type" list_of_type_assignments)))
  _any_parameter_declaration (:choice local_parameter_declaration parameter_declaration)
  specparam_declaration (:seq
                         "specparam"
                         (:choice packed_dimension :blank)
                         list_of_specparam_assignments
                         ";")
  inout_declaration (:seq "inout" (:choice net_port_type1 :blank) list_of_port_identifiers)
  input_declaration (:seq
                     "input"
                     (:choice
                      (:seq (:choice net_port_type1 :blank) list_of_port_identifiers)
                      (:seq (:choice _variable_port_type :blank) list_of_variable_identifiers)))
  output_declaration (:seq
                      "output"
                      (:choice
                       (:seq (:choice net_port_type1 :blank) list_of_port_identifiers)
                       (:seq (:choice _variable_port_type :blank) list_of_variable_port_identifiers)))
  interface_port_declaration (:seq
                              interface_identifier
                              (:choice (:prec-left 0 (:seq "." modport_identifier)) :blank)
                              list_of_interface_identifiers)
  ref_declaration (:seq "ref" _variable_port_type list_of_variable_identifiers)
  data_declaration (:choice
                    (:seq
                     (:choice "const" :blank)
                     (:choice "var" :blank)
                     (:choice lifetime :blank)
                     (:choice data_type_or_implicit1 :blank)
                     list_of_variable_decl_assignments
                     ";")
                    type_declaration
                    package_import_declaration
                    net_type_declaration)
  package_import_declaration (:seq
                              "import"
                              (:prec-left 0
                               (:seq
                                package_import_item
                                (:repeat (:prec-left 0 (:seq "," package_import_item)))))
                              ";")
  package_import_item (:seq package_identifier "::" (:choice _identifier "*"))
  package_export_declaration (:seq
                              "export"
                              (:choice
                               "*::*"
                               (:prec-left 0
                                (:seq
                                 package_import_item
                                 (:repeat (:prec-left 0 (:seq "," package_import_item))))))
                              ";")
  genvar_declaration (:seq "genvar" list_of_genvar_identifiers ";")
  net_declaration (:choice
                   (:seq
                    net_type
                    (:choice (:choice drive_strength charge_strength) :blank)
                    (:choice (:choice "vectored" "scalared") :blank)
                    (:choice data_type_or_implicit1 :blank)
                    (:choice delay3 :blank)
                    list_of_net_decl_assignments
                    ";")
                   (:seq
                    _net_type_identifier
                    (:choice delay_control :blank)
                    list_of_net_decl_assignments
                    ";")
                   (:seq
                    "interconnect"
                    (:choice implicit_data_type1 :blank)
                    (:choice (:prec-left 0 (:seq "#" delay_value)) :blank)
                    (:prec-left 0
                     (:seq
                      (:seq _net_identifier (:repeat unpacked_dimension))
                      (:repeat
                       (:prec-left 0 (:seq "," (:seq _net_identifier (:repeat unpacked_dimension)))))))
                    ";"))
  type_declaration (:seq
                    "typedef"
                    (:choice
                     (:seq data_type _type_identifier (:repeat _variable_dimension))
                     (:seq
                      interface_instance_identifier
                      (:choice constant_bit_select1 :blank)
                      "."
                      _type_identifier
                      _type_identifier)
                     (:seq
                      (:choice
                       (:choice "enum" "struct" "union" "class" (:seq "interface" "class"))
                       :blank)
                      _type_identifier))
                    ";")
  net_type_declaration (:seq
                        "nettype"
                        (:choice
                         (:seq
                          data_type
                          _net_type_identifier
                          (:choice
                           (:prec-left 0
                            (:seq
                             "with"
                             (:choice (:choice package_scope class_scope) :blank)
                             tf_identifier))
                           :blank))
                         (:seq
                          (:choice (:choice package_scope class_scope) :blank)
                          _net_type_identifier
                          _net_type_identifier))
                        ";")
  lifetime (:choice "static" "automatic")
  casting_type (:choice _simple_type constant_primary _signing "string" "const")
  data_type (:choice
             (:seq integer_vector_type (:choice _signing :blank) (:repeat packed_dimension))
             (:seq integer_atom_type (:choice _signing :blank))
             non_integer_type
             (:seq
              struct_union
              (:choice (:prec-left 0 (:seq "packed" (:choice _signing :blank))) :blank)
              "{"
              (:repeat1 struct_union_member)
              "}"
              (:repeat packed_dimension))
             (:seq
              "enum"
              (:choice enum_base_type :blank)
              "{"
              (:prec-left 0
               (:seq
                enum_name_declaration
                (:repeat (:prec-left 0 (:seq "," enum_name_declaration)))))
              "}"
              (:repeat packed_dimension))
             "string"
             "chandle"
             (:prec-left 0
              (:seq
               "virtual"
               (:choice "interface" :blank)
               interface_identifier
               (:choice parameter_value_assignment :blank)
               (:choice (:prec-left 0 (:seq "." modport_identifier)) :blank)))
             (:seq
              (:choice (:choice class_scope package_scope) :blank)
              _type_identifier
              (:repeat packed_dimension))
             class_type
             "event"
             ps_covergroup_identifier
             type_reference)
  data_type_or_implicit1 (:choice data_type implicit_data_type1)
  implicit_data_type1 (:choice
                       (:seq _signing (:repeat packed_dimension))
                       (:repeat1 packed_dimension))
  enum_base_type (:choice
                  (:seq integer_atom_type (:choice _signing :blank))
                  (:seq
                   integer_vector_type
                   (:choice _signing :blank)
                   (:choice packed_dimension :blank))
                  (:seq _type_identifier (:choice packed_dimension :blank)))
  enum_name_declaration (:seq
                         enum_identifier
                         (:choice
                          (:prec-left 0
                           (:seq
                            "["
                            integral_number
                            (:choice (:prec-left 0 (:seq ":" integral_number)) :blank)
                            "]"))
                          :blank)
                         (:choice (:prec-left 0 (:seq "=" constant_expression)) :blank))
  class_scope (:seq class_type "::")
  class_type (:prec-right 0
              (:seq
               ps_class_identifier
               (:choice parameter_value_assignment :blank)
               (:repeat
                (:prec-left 0
                 (:seq "::" class_identifier (:choice parameter_value_assignment :blank))))))
  _integer_type (:choice integer_vector_type integer_atom_type)
  integer_atom_type (:choice "byte" "shortint" "int" "longint" "integer" "time")
  integer_vector_type (:choice "bit" "logic" "reg")
  non_integer_type (:choice "shortreal" "real" "realtime")
  net_type (:choice
            "supply0"
            "supply1"
            "tri"
            "triand"
            "trior"
            "trireg"
            "tri0"
            "tri1"
            "uwire"
            "wire"
            "wand"
            "wor")
  net_port_type1 (:choice
                  (:prec-left -1 (:seq net_type data_type_or_implicit1))
                  net_type
                  data_type_or_implicit1
                  _net_type_identifier
                  (:seq "interconnect" (:choice implicit_data_type1 :blank)))
  _variable_port_type _var_data_type
  _var_data_type (:prec-left 0
                  (:choice data_type (:seq "var" (:choice data_type_or_implicit1 :blank))))
  _signing (:choice "signed" "unsigned")
  _simple_type (:choice _integer_type non_integer_type ps_type_identifier ps_parameter_identifier)
  struct_union_member (:seq
                       (:repeat attribute_instance)
                       (:choice random_qualifier :blank)
                       data_type_or_void
                       list_of_variable_decl_assignments
                       ";")
  data_type_or_void (:choice data_type "void")
  struct_union (:choice "struct" (:seq "union" (:choice "tagged" :blank)))
  type_reference (:seq "type" "(" (:choice expression data_type) ")")
  drive_strength (:seq
                  "("
                  (:choice
                   (:seq strength0 "," strength1)
                   (:seq strength1 "," strength0)
                   (:seq strength0 "," "highz1")
                   (:seq strength1 "," "highz0")
                   (:seq "highz0" "," strength1)
                   (:seq "highz1" "," strength0))
                  ")")
  strength0 (:choice "supply0" "strong0" "pull0" "weak0")
  strength1 (:choice "supply1" "strong1" "pull1" "weak1")
  charge_strength (:seq "(" (:choice "small" "medium" "large") ")")
  delay3 (:seq
          "#"
          (:choice
           delay_value
           (:seq
            "("
            mintypmax_expression
            (:choice
             (:prec-left 0 (:seq mintypmax_expression (:choice mintypmax_expression :blank)))
             :blank)
            ")")))
  delay2 (:seq
          "#"
          (:choice
           delay_value
           (:seq "(" mintypmax_expression (:choice mintypmax_expression :blank) ")")))
  delay_value (:choice unsigned_number real_number ps_identifier time_literal "1step")
  list_of_defparam_assignments (:prec-left 0
                                (:seq
                                 defparam_assignment
                                 (:repeat (:prec-left 0 (:seq "," defparam_assignment)))))
  list_of_genvar_identifiers (:prec-left 0
                              (:seq
                               genvar_identifier
                               (:repeat (:prec-left 0 (:seq "," genvar_identifier)))))
  list_of_interface_identifiers (:prec-left 0
                                 (:seq
                                  (:seq interface_identifier (:repeat unpacked_dimension))
                                  (:repeat
                                   (:prec-left 0
                                    (:seq
                                     ","
                                     (:seq interface_identifier (:repeat unpacked_dimension)))))))
  list_of_net_decl_assignments (:prec-left 0
                                (:seq
                                 net_decl_assignment
                                 (:repeat (:prec-left 0 (:seq "," net_decl_assignment)))))
  list_of_param_assignments (:prec-left 0
                             (:seq
                              param_assignment
                              (:repeat (:prec-left 0 (:seq "," param_assignment)))))
  list_of_port_identifiers (:prec-left 0
                            (:seq
                             (:seq port_identifier (:repeat unpacked_dimension))
                             (:repeat
                              (:prec-left 0
                               (:seq "," (:seq port_identifier (:repeat unpacked_dimension)))))))
  list_of_udp_port_identifiers (:prec-left 0
                                (:seq
                                 port_identifier
                                 (:repeat (:prec-left 0 (:seq "," port_identifier)))))
  list_of_specparam_assignments (:prec-left 0
                                 (:seq
                                  specparam_assignment
                                  (:repeat (:prec-left 0 (:seq "," specparam_assignment)))))
  list_of_tf_variable_identifiers (:prec-left 0
                                   (:seq
                                    (:seq
                                     port_identifier
                                     (:repeat _variable_dimension)
                                     (:choice (:prec-left 0 (:seq "=" expression)) :blank))
                                    (:repeat
                                     (:prec-left 0
                                      (:seq
                                       ","
                                       (:seq
                                        port_identifier
                                        (:repeat _variable_dimension)
                                        (:choice (:prec-left 0 (:seq "=" expression)) :blank)))))))
  list_of_type_assignments (:prec-left 0
                            (:seq
                             type_assignment
                             (:repeat (:prec-left 0 (:seq "," type_assignment)))))
  list_of_variable_decl_assignments (:prec-left 0
                                     (:seq
                                      variable_decl_assignment
                                      (:repeat (:prec-left 0 (:seq "," variable_decl_assignment)))))
  list_of_variable_identifiers (:prec-left 0
                                (:seq
                                 (:seq _variable_identifier (:repeat _variable_dimension))
                                 (:repeat
                                  (:prec-left 0
                                   (:seq
                                    ","
                                    (:seq _variable_identifier (:repeat _variable_dimension)))))))
  list_of_variable_port_identifiers (:prec-left 0
                                     (:seq
                                      (:seq
                                       port_identifier
                                       (:repeat _variable_dimension)
                                       (:choice
                                        (:prec-left 0 (:seq "=" constant_expression))
                                        :blank))
                                      (:repeat
                                       (:prec-left 0
                                        (:seq
                                         ","
                                         (:seq
                                          port_identifier
                                          (:repeat _variable_dimension)
                                          (:choice
                                           (:prec-left 0 (:seq "=" constant_expression))
                                           :blank)))))))
  defparam_assignment (:seq _hierarchical_parameter_identifier "=" constant_mintypmax_expression)
  net_decl_assignment (:prec-left 21
                       (:seq
                        _net_identifier
                        (:repeat unpacked_dimension)
                        (:choice (:prec-left 0 (:seq "=" expression)) :blank)))
  param_assignment (:seq
                    parameter_identifier
                    (:repeat unpacked_dimension)
                    (:choice (:prec-left 0 (:seq "=" constant_param_expression)) :blank))
  specparam_assignment (:choice
                        (:seq specparam_identifier "=" constant_mintypmax_expression)
                        pulse_control_specparam)
  type_assignment (:seq _type_identifier (:choice (:prec-left 0 (:seq "=" data_type)) :blank))
  pulse_control_specparam (:choice
                           (:seq
                            "PATHPULSE$="
                            "("
                            reject_limit_value
                            (:choice (:prec-left 0 (:seq "," error_limit_value)) :blank)
                            ")"))
  error_limit_value limit_value
  reject_limit_value limit_value
  limit_value constant_mintypmax_expression
  variable_decl_assignment (:choice
                            (:seq
                             _variable_identifier
                             (:repeat _variable_dimension)
                             (:choice (:prec-left 0 (:seq "=" expression)) :blank))
                            (:seq
                             dynamic_array_variable_identifier
                             unsized_dimension
                             (:repeat _variable_dimension)
                             (:choice (:prec-left 0 (:seq "=" dynamic_array_new)) :blank))
                            (:seq
                             class_variable_identifier
                             (:choice (:prec-left 0 (:seq "=" class_new)) :blank)))
  class_new (:choice
             (:seq (:choice class_scope :blank) "new" (:choice list_of_arguments_parent :blank))
             (:seq "new" expression))
  dynamic_array_new (:seq
                     "new"
                     "["
                     expression
                     "]"
                     (:choice (:prec-left 0 (:seq "(" expression ")")) :blank))
  unpacked_dimension (:seq "[" (:choice constant_range constant_expression) "]")
  packed_dimension (:choice (:seq "[" constant_range "]") unsized_dimension)
  associative_dimension (:seq "[" (:choice data_type "*") "]")
  _variable_dimension (:choice
                       unsized_dimension
                       unpacked_dimension
                       associative_dimension
                       queue_dimension)
  queue_dimension (:seq "[" "$" (:choice (:prec-left 0 (:seq ":" constant_expression)) :blank) "]")
  unsized_dimension (:seq "[" "]")
  function_data_type_or_implicit1 (:choice data_type_or_void implicit_data_type1)
  function_declaration (:seq "function" (:choice lifetime :blank) function_body_declaration)
  function_body_declaration (:seq
                             (:choice function_data_type_or_implicit1 :blank)
                             (:choice (:choice (:seq interface_identifier ".") class_scope) :blank)
                             function_identifier
                             (:choice
                              (:seq ";" (:repeat tf_item_declaration))
                              (:seq
                               "("
                               (:choice tf_port_list :blank)
                               ")"
                               ";"
                               (:repeat block_item_declaration)))
                             (:repeat function_statement_or_null)
                             "endfunction"
                             (:choice (:prec-left 0 (:seq ":" function_identifier)) :blank))
  function_prototype (:seq
                      "function"
                      data_type_or_void
                      function_identifier
                      (:choice (:prec-left 0 (:seq "(" (:choice tf_port_list :blank) ")")) :blank))
  dpi_import_export (:choice
                     (:seq
                      "import"
                      dpi_spec_string
                      (:choice dpi_function_import_property :blank)
                      (:choice (:prec-left 0 (:seq c_identifier "=")) :blank)
                      dpi_function_proto
                      ";")
                     (:seq
                      "import"
                      dpi_spec_string
                      (:choice dpi_task_import_property :blank)
                      (:choice (:prec-left 0 (:seq c_identifier "=")) :blank)
                      dpi_task_proto
                      ";")
                     (:seq
                      "export"
                      dpi_spec_string
                      (:choice (:prec-left 0 (:seq c_identifier "=")) :blank)
                      "function"
                      function_identifier
                      ";")
                     (:seq
                      "export"
                      dpi_spec_string
                      (:choice (:prec-left 0 (:seq c_identifier "=")) :blank)
                      "task"
                      task_identifier
                      ";"))
  dpi_spec_string (:choice "\"DPI-C\"" "\"DPI\"")
  dpi_function_import_property (:choice "context" "pure")
  dpi_task_import_property "context"
  dpi_function_proto function_prototype
  dpi_task_proto task_prototype
  task_declaration (:seq "task" (:choice lifetime :blank) task_body_declaration)
  task_body_declaration (:seq
                         (:choice (:choice (:seq interface_identifier ".") class_scope) :blank)
                         task_identifier
                         (:choice
                          (:seq ";" (:repeat tf_item_declaration))
                          (:seq
                           "("
                           (:choice tf_port_list :blank)
                           ")"
                           ";"
                           (:repeat block_item_declaration)))
                         (:repeat statement_or_null)
                         "endtask"
                         (:choice (:prec-left 0 (:seq ":" task_identifier)) :blank))
  tf_item_declaration (:choice block_item_declaration tf_port_declaration)
  tf_port_list (:prec-left 0 (:seq tf_port_item1 (:repeat (:prec-left 0 (:seq "," tf_port_item1)))))
  tf_port_item1 (:seq
                 (:repeat attribute_instance)
                 (:choice tf_port_direction :blank)
                 (:choice "var" :blank)
                 (:choice
                  (:seq
                   data_type_or_implicit1
                   (:choice
                    (:prec-left 0
                     (:seq
                      port_identifier
                      (:repeat _variable_dimension)
                      (:choice (:prec-left 0 (:seq "=" expression)) :blank)))
                    :blank))
                  (:seq
                   port_identifier
                   (:repeat _variable_dimension)
                   (:choice (:prec-left 0 (:seq "=" expression)) :blank))))
  tf_port_direction (:choice port_direction (:seq "const" "ref"))
  tf_port_declaration (:seq
                       (:repeat attribute_instance)
                       tf_port_direction
                       (:choice "var" :blank)
                       (:choice data_type_or_implicit1 :blank)
                       list_of_tf_variable_identifiers
                       ";")
  task_prototype (:seq
                  "task"
                  task_identifier
                  (:choice (:prec-left 0 (:seq "(" (:choice tf_port_list :blank) ")")) :blank))
  block_item_declaration (:seq
                          (:repeat attribute_instance)
                          (:choice
                           data_declaration
                           (:seq _any_parameter_declaration ";")
                           overload_declaration
                           let_declaration))
  overload_declaration (:seq
                        "bind"
                        overload_operator
                        "function"
                        data_type
                        function_identifier
                        "("
                        overload_proto_formals
                        ")"
                        ";")
  overload_operator (:choice
                     "+"
                     "++"
                     "–"
                     "––"
                     "*"
                     "**"
                     "/"
                     "%"
                     "=="
                     "!="
                     "<"
                     "<="
                     ">"
                     ">="
                     "=")
  overload_proto_formals (:prec-left 0
                          (:seq data_type (:repeat (:prec-left 0 (:seq "," data_type)))))
  modport_declaration (:seq
                       "modport"
                       (:prec-left 0
                        (:seq modport_item (:repeat (:prec-left 0 (:seq "," modport_item)))))
                       ";")
  modport_item (:seq
                modport_identifier
                "("
                (:prec-left 0
                 (:seq
                  modport_ports_declaration
                  (:repeat (:prec-left 0 (:seq "," modport_ports_declaration)))))
                ")")
  modport_ports_declaration (:seq
                             (:repeat attribute_instance)
                             (:choice
                              modport_simple_ports_declaration
                              modport_tf_ports_declaration
                              modport_clocking_declaration))
  modport_clocking_declaration (:seq "clocking" clocking_identifier)
  modport_simple_ports_declaration (:seq
                                    port_direction
                                    (:prec-left 0
                                     (:seq
                                      modport_simple_port
                                      (:repeat (:prec-left 0 (:seq "," modport_simple_port))))))
  modport_simple_port (:choice
                       port_identifier
                       (:seq "." port_identifier "(" (:choice expression :blank) ")"))
  modport_tf_ports_declaration (:seq
                                import_export
                                (:prec-left 0
                                 (:seq
                                  _modport_tf_port
                                  (:repeat (:prec-left 0 (:seq "," _modport_tf_port))))))
  _modport_tf_port (:choice _method_prototype tf_identifier)
  import_export (:choice "import" "export")
  concurrent_assertion_item (:choice
                             (:seq
                              (:choice (:prec-left 0 (:seq _block_identifier ":")) :blank)
                              _concurrent_assertion_statement)
                             checker_instantiation)
  _concurrent_assertion_statement (:choice
                                   assert_property_statement
                                   assume_property_statement
                                   cover_property_statement
                                   cover_sequence_statement
                                   restrict_property_statement)
  assert_property_statement (:seq "assert" "property" "(" property_spec ")" action_block)
  assume_property_statement (:seq "assume" "property" "(" property_spec ")" action_block)
  cover_property_statement (:seq "cover" "property" "(" property_spec ")" statement_or_null)
  expect_property_statement (:seq "expect" "(" property_spec ")" action_block)
  cover_sequence_statement (:seq
                            "cover"
                            "sequence"
                            "("
                            (:choice clocking_event :blank)
                            (:choice
                             (:prec-right 11 (:seq "disable" "iff" "(" expression_or_dist ")"))
                             :blank)
                            sequence_expr
                            ")"
                            statement_or_null)
  restrict_property_statement (:seq "restrict" "property" "(" property_spec ")" ";")
  property_instance (:seq
                     ps_or_hierarchical_property_identifier
                     (:choice
                      (:prec-left 0 (:seq "(" (:choice property_list_of_arguments :blank) ")"))
                      :blank))
  property_list_of_arguments (:choice
                              (:seq
                               (:prec-left 0
                                (:seq
                                 (:choice _property_actual_arg :blank)
                                 (:repeat
                                  (:prec-left 0 (:seq "," (:choice _property_actual_arg :blank))))))
                               (:repeat1
                                (:seq
                                 ","
                                 "."
                                 _identifier
                                 "("
                                 (:choice _property_actual_arg :blank)
                                 ")")))
                              (:prec-left 0
                               (:seq
                                (:seq "." _identifier "(" (:choice _property_actual_arg :blank) ")")
                                (:repeat
                                 (:prec-left 0
                                  (:seq
                                   ","
                                   (:seq
                                    "."
                                    _identifier
                                    "("
                                    (:choice _property_actual_arg :blank)
                                    ")")))))))
  _property_actual_arg (:choice property_expr _sequence_actual_arg)
  _assertion_item_declaration (:choice property_declaration sequence_declaration let_declaration)
  property_declaration (:seq
                        "property"
                        property_identifier
                        (:choice
                         (:prec-left 0 (:seq "(" (:choice property_port_list :blank) ")"))
                         :blank)
                        ";"
                        (:repeat assertion_variable_declaration)
                        property_spec
                        (:choice ";" :blank)
                        "endproperty"
                        (:choice (:prec-left 0 (:seq ":" property_identifier)) :blank))
  property_port_list (:prec-left 0
                      (:seq
                       property_port_item
                       (:repeat (:prec-left 0 (:seq "," property_port_item)))))
  property_port_item (:seq
                      (:repeat attribute_instance)
                      (:choice
                       (:prec-left 0 (:seq "local" (:choice property_lvar_port_direction :blank)))
                       :blank)
                      (:choice property_formal_type1 :blank)
                      formal_port_identifier
                      (:repeat _variable_dimension)
                      (:choice (:prec-left 0 (:seq "=" _property_actual_arg)) :blank))
  property_lvar_port_direction "input"
  property_formal_type1 (:choice sequence_formal_type1 "property")
  property_spec (:seq
                 (:choice clocking_event :blank)
                 (:choice (:prec-right 11 (:seq "disable" "iff" "(" expression_or_dist ")")) :blank)
                 property_expr)
  property_expr (:choice
                 sequence_expr
                 (:seq "strong" "(" sequence_expr ")")
                 (:seq "weak" "(" sequence_expr ")")
                 (:prec-left 37 (:seq "(" property_expr ")"))
                 (:prec-left 14 (:seq "not" property_expr))
                 (:prec-left 12 (:seq property_expr "or" property_expr))
                 (:prec-left 13 (:seq property_expr "and" property_expr))
                 (:prec-right 9 (:seq sequence_expr "|->" property_expr))
                 (:prec-right 9 (:seq sequence_expr "|=>" property_expr))
                 (:prec-left 0
                  (:seq
                   "if"
                   "("
                   expression_or_dist
                   ")"
                   property_expr
                   (:choice (:prec-left 0 (:seq "else" property_expr)) :blank)))
                 (:seq "case" "(" expression_or_dist ")" (:repeat1 property_case_item) "endcase")
                 (:prec-right 9 (:seq sequence_expr "#-#" property_expr))
                 (:prec-right 9 (:seq sequence_expr "#=#" property_expr))
                 (:prec-left 14 (:seq "nexttime" property_expr))
                 (:prec-left 14 (:seq "nexttime" "[" constant_expression "]" property_expr))
                 (:prec-left 14 (:seq "s_nexttime" property_expr))
                 (:prec-left 14 (:seq "s_nexttime" "[" constant_expression "]" property_expr))
                 (:prec-left 8 (:seq "always" property_expr))
                 (:prec-left 8
                  (:seq "always" "[" cycle_delay_const_range_expression "]" property_expr))
                 (:prec-left 8 (:seq "s_always" "[" constant_range "]" property_expr))
                 (:prec-left 8 (:seq "s_eventually" property_expr))
                 (:prec-left 8 (:seq "eventually" "[" constant_range "]" property_expr))
                 (:prec-left 8
                  (:seq "s_eventually" "[" cycle_delay_const_range_expression "]" property_expr))
                 (:prec-right 10
                  (:seq
                   property_expr
                   (:choice "until" "s_until" "until_with" "s_until_with" "implies")
                   property_expr))
                 (:prec-right 11 (:seq property_expr "iff" property_expr))
                 (:prec-left 8
                  (:seq
                   (:choice "accept_on" "reject_on" "sync_accept_on" "sync_reject_on")
                   "("
                   expression_or_dist
                   ")"
                   property_expr))
                 (:prec-left 0 (:seq clocking_event property_expr)))
  property_case_item (:choice
                      (:seq
                       (:prec-left 0
                        (:seq
                         expression_or_dist
                         (:repeat (:prec-left 0 (:seq "," expression_or_dist)))))
                       ":"
                       property_expr
                       ";")
                      (:seq "default" (:choice ":" :blank) property_expr ";"))
  sequence_declaration (:seq
                        "sequence"
                        _sequence_identifier
                        (:choice
                         (:prec-left 0 (:seq "(" (:choice sequence_port_list :blank) ")"))
                         :blank)
                        ";"
                        (:repeat assertion_variable_declaration)
                        sequence_expr
                        (:choice ";" :blank)
                        "endsequence"
                        (:choice (:prec-left 0 (:seq ":" _sequence_identifier)) :blank))
  sequence_port_list (:prec-left 0
                      (:seq
                       sequence_port_item
                       (:repeat (:prec-left 0 (:seq "," sequence_port_item)))))
  sequence_port_item (:seq
                      (:repeat attribute_instance)
                      (:choice
                       (:prec-left 0 (:seq "local" (:choice sequence_lvar_port_direction :blank)))
                       :blank)
                      (:choice sequence_formal_type1 :blank)
                      formal_port_identifier
                      (:repeat _variable_dimension)
                      (:choice (:prec-left 0 (:seq "=" _sequence_actual_arg)) :blank))
  sequence_lvar_port_direction (:choice "input" "inout" "output")
  sequence_formal_type1 (:choice data_type_or_implicit1 "sequence" "untyped")
  sequence_expr (:choice
                 (:prec-left 0
                  (:prec-left 0
                   (:seq cycle_delay_range (:repeat (:prec-left 0 (:seq "," cycle_delay_range))))))
                 (:prec-left 18
                  (:seq sequence_expr (:repeat1 (:seq cycle_delay_range sequence_expr))))
                 (:seq expression_or_dist (:choice _boolean_abbrev :blank))
                 (:seq sequence_instance (:choice sequence_abbrev :blank))
                 (:prec-left 0
                  (:seq
                   "("
                   sequence_expr
                   (:repeat (:prec-left 0 (:seq "," _sequence_match_item)))
                   ")"
                   (:choice sequence_abbrev :blank)))
                 (:prec-left 13 (:seq sequence_expr "and" sequence_expr))
                 (:prec-left 15 (:seq sequence_expr "intersect" sequence_expr))
                 (:prec-left 12 (:seq sequence_expr "or" sequence_expr))
                 (:seq
                  "first_match"
                  "("
                  sequence_expr
                  (:repeat (:prec-left 0 (:seq "," _sequence_match_item)))
                  ")")
                 (:prec-right 17 (:seq expression_or_dist "throughout" sequence_expr))
                 (:prec-left 16 (:seq sequence_expr "within" sequence_expr))
                 (:prec-left 0 (:seq clocking_event sequence_expr)))
  cycle_delay_range (:choice
                     (:prec-left 0 (:seq "##" constant_primary))
                     (:prec-left 0 (:seq "##" "[" cycle_delay_const_range_expression "]"))
                     "##[*]"
                     "##[+]")
  sequence_method_call (:seq sequence_instance "." method_identifier)
  _sequence_match_item (:choice operator_assignment inc_or_dec_expression subroutine_call)
  sequence_instance (:seq
                     ps_or_hierarchical_sequence_identifier
                     (:choice
                      (:prec-left 0 (:seq "(" (:choice sequence_list_of_arguments :blank) ")"))
                      :blank))
  sequence_list_of_arguments (:choice
                              (:prec-left 0
                               (:seq
                                (:seq "." _identifier "(" (:choice _sequence_actual_arg :blank) ")")
                                (:repeat
                                 (:prec-left 0
                                  (:seq
                                   ","
                                   (:seq
                                    "."
                                    _identifier
                                    "("
                                    (:choice _sequence_actual_arg :blank)
                                    ")")))))))
  _sequence_actual_arg (:choice event_expression sequence_expr)
  _boolean_abbrev (:choice consecutive_repetition non_consecutive_repetition goto_repetition)
  sequence_abbrev consecutive_repetition
  consecutive_repetition (:choice (:seq "[*" _const_or_range_expression "]") "[*]" "[+]")
  non_consecutive_repetition (:seq "[=" _const_or_range_expression "]")
  goto_repetition (:seq "[->" _const_or_range_expression "]")
  _const_or_range_expression (:choice constant_expression cycle_delay_const_range_expression)
  cycle_delay_const_range_expression (:choice
                                      (:seq constant_expression ":" constant_expression)
                                      (:seq constant_expression ":" "$"))
  expression_or_dist (:seq
                      expression
                      (:choice (:prec-left 31 (:seq "dist" "{" dist_list "}")) :blank))
  assertion_variable_declaration (:seq _var_data_type list_of_variable_decl_assignments ";")
  covergroup_declaration (:seq
                          "covergroup"
                          covergroup_identifier
                          (:choice
                           (:prec-left 0 (:seq "(" (:choice tf_port_list :blank) ")"))
                           :blank)
                          (:choice coverage_event :blank)
                          ";"
                          (:repeat coverage_spec_or_option)
                          "endgroup"
                          (:choice (:prec-left 0 (:seq ":" covergroup_identifier)) :blank))
  coverage_spec_or_option (:choice
                           (:seq (:repeat attribute_instance) _coverage_spec)
                           (:seq (:repeat attribute_instance) coverage_option ";"))
  coverage_option (:choice
                   (:seq "option" "." member_identifier "=" expression)
                   (:seq "type_option" "." member_identifier "=" constant_expression))
  _coverage_spec (:choice cover_point cover_cross)
  coverage_event (:choice
                  clocking_event
                  (:seq "with" "function" "sample" "(" (:choice tf_port_list :blank) ")")
                  (:seq "@@" "(" block_event_expression ")"))
  block_event_expression (:choice
                          (:prec-left 12 (:seq block_event_expression "or" block_event_expression))
                          (:seq "begin" hierarchical_btf_identifier)
                          (:seq "end" hierarchical_btf_identifier))
  hierarchical_btf_identifier (:choice
                               _hierarchical_tf_identifier
                               _hierarchical_block_identifier
                               (:prec-left 37
                                (:seq
                                 (:choice (:seq hierarchical_identifier ".") class_scope)
                                 method_identifier)))
  cover_point (:seq
               (:choice
                (:prec-left 0
                 (:seq (:choice data_type_or_implicit1 :blank) cover_point_identifier ":"))
                :blank)
               "coverpoint"
               expression
               (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank)
               bins_or_empty)
  bins_or_empty (:choice
                 (:seq
                  "{"
                  (:repeat attribute_instance)
                  (:repeat (:prec-left 0 (:seq bins_or_options ";")))
                  "}")
                 ";")
  bins_or_options (:choice
                   coverage_option
                   (:seq
                    "wildcard"
                    bins_keyword
                    _bin_identifier
                    (:choice
                     (:prec-left 0 (:seq "[" (:choice _covergroup_expression :blank) "]"))
                     :blank)
                    "="
                    "{"
                    covergroup_range_list
                    "}"
                    (:choice
                     (:prec-left 0 (:seq "with" "(" _with_covergroup_expression ")"))
                     :blank)
                    (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank))
                   (:seq
                    "wildcard"
                    bins_keyword
                    _bin_identifier
                    (:choice
                     (:prec-left 0 (:seq "[" (:choice _covergroup_expression :blank) "]"))
                     :blank)
                    "="
                    cover_point_identifier
                    "with"
                    "("
                    _with_covergroup_expression
                    ")"
                    (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank))
                   (:seq
                    "wildcard"
                    bins_keyword
                    _bin_identifier
                    (:choice
                     (:prec-left 0 (:seq "[" (:choice _covergroup_expression :blank) "]"))
                     :blank)
                    "="
                    _set_covergroup_expression
                    (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank))
                   (:seq
                    "wildcard"
                    bins_keyword
                    _bin_identifier
                    (:choice (:prec-left 0 (:seq "[" "]")) :blank)
                    "="
                    trans_list
                    (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank))
                   (:seq
                    bins_keyword
                    _bin_identifier
                    (:choice
                     (:prec-left 0 (:seq "[" (:choice _covergroup_expression :blank) "]"))
                     :blank)
                    "="
                    "default"
                    (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank))
                   (:seq
                    bins_keyword
                    _bin_identifier
                    "="
                    "default"
                    "sequence"
                    (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank)))
  bins_keyword (:choice "bins" "illegal_bins" "ignore_bins")
  trans_list (:prec-left 0
              (:seq
               (:seq "(" trans_set ")")
               (:repeat (:prec-left 0 (:seq "," (:seq "(" trans_set ")"))))))
  trans_set (:prec-left 0
             (:seq trans_range_list (:repeat (:prec-left 0 (:seq "=>" trans_range_list)))))
  trans_range_list (:choice
                    trans_item
                    (:seq trans_item "[*" repeat_range "]")
                    (:seq trans_item "[–>" repeat_range "]")
                    (:seq trans_item "[=" repeat_range "]"))
  trans_item covergroup_range_list
  repeat_range (:seq
                _covergroup_expression
                (:choice (:prec-left 0 (:seq ":" _covergroup_expression)) :blank))
  cover_cross (:seq
               (:choice (:prec-left 0 (:seq cross_identifier ":")) :blank)
               "cross"
               list_of_cross_items
               (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank)
               cross_body)
  list_of_cross_items (:seq
                       _cross_item
                       ","
                       (:prec-left 0
                        (:seq _cross_item (:repeat (:prec-left 0 (:seq "," _cross_item))))))
  _cross_item (:choice cover_point_identifier)
  cross_body (:choice (:seq "{" (:repeat (:prec-left 0 (:seq cross_body_item ";"))) "}") ";")
  cross_body_item (:choice function_declaration (:seq bins_selection_or_option ";"))
  bins_selection_or_option (:choice
                            (:seq (:repeat attribute_instance) coverage_option)
                            (:seq (:repeat attribute_instance) bins_selection))
  bins_selection (:seq
                  bins_keyword
                  _bin_identifier
                  "="
                  select_expression
                  (:choice (:prec-right 11 (:seq "iff" "(" expression ")")) :blank))
  select_expression (:choice
                     select_condition
                     (:prec-left 36 (:seq "!" select_condition))
                     (:prec-left 25 (:seq select_expression "&&" select_expression))
                     (:prec-left 24 (:seq select_expression "||" select_expression))
                     (:prec-left 37 (:seq "(" select_expression ")"))
                     (:seq
                      select_expression
                      "with"
                      "("
                      _with_covergroup_expression
                      ")"
                      (:choice
                       (:prec-left 0 (:seq "matches" _integer_covergroup_expression))
                       :blank))
                     cross_identifier
                     (:seq
                      _cross_set_expression
                      (:choice
                       (:prec-left 0 (:seq "matches" _integer_covergroup_expression))
                       :blank)))
  select_condition (:seq
                    "binsof"
                    "("
                    bins_expression
                    ")"
                    (:choice (:prec-left 0 (:seq "intersect" "{" covergroup_range_list "}")) :blank))
  bins_expression (:choice
                   _variable_identifier
                   (:prec-left 37
                    (:seq
                     cover_point_identifier
                     (:choice (:prec-left 0 (:seq "." _bin_identifier)) :blank))))
  covergroup_range_list (:prec-left 0
                         (:seq
                          covergroup_value_range
                          (:repeat (:prec-left 0 (:seq "," covergroup_value_range)))))
  covergroup_value_range (:choice
                          _covergroup_expression
                          (:seq "[" _covergroup_expression ":" _covergroup_expression "]"))
  _with_covergroup_expression _covergroup_expression
  _set_covergroup_expression _covergroup_expression
  _integer_covergroup_expression _covergroup_expression
  _cross_set_expression _covergroup_expression
  _covergroup_expression expression
  let_declaration (:seq
                   "let"
                   let_identifier
                   (:choice (:prec-left 0 (:seq "(" (:choice let_port_list :blank) ")")) :blank)
                   "="
                   expression
                   ";")
  let_identifier _identifier
  let_port_list (:prec-left 0
                 (:seq let_port_item (:repeat (:prec-left 0 (:seq "," let_port_item)))))
  let_port_item (:seq
                 (:repeat attribute_instance)
                 (:choice let_formal_type1 :blank)
                 formal_port_identifier
                 (:repeat _variable_dimension)
                 (:choice (:prec-left 0 (:seq "=" expression)) :blank))
  let_formal_type1 (:choice data_type_or_implicit1 "untyped")
  let_expression (:prec-left 0
                  (:seq
                   (:choice package_scope :blank)
                   let_identifier
                   (:choice
                    (:prec-left 0 (:seq "(" (:choice let_list_of_arguments :blank) ")"))
                    :blank)))
  let_list_of_arguments (:choice
                         (:prec-left 0
                          (:seq
                           (:seq "." _identifier "(" (:choice let_actual_arg :blank) ")")
                           (:repeat
                            (:prec-left 0
                             (:seq
                              ","
                              (:seq "." _identifier "(" (:choice let_actual_arg :blank) ")")))))))
  let_actual_arg expression
  gate_instantiation (:seq
                      (:choice
                       (:seq
                        cmos_switchtype
                        (:choice delay3 :blank)
                        (:prec-left 0
                         (:seq
                          cmos_switch_instance
                          (:repeat (:prec-left 0 (:seq "," cmos_switch_instance))))))
                       (:seq
                        enable_gatetype
                        (:choice drive_strength :blank)
                        (:choice delay3 :blank)
                        (:prec-left 0
                         (:seq
                          enable_gate_instance
                          (:repeat (:prec-left 0 (:seq "," enable_gate_instance))))))
                       (:seq
                        mos_switchtype
                        (:choice delay3 :blank)
                        (:prec-left 0
                         (:seq
                          mos_switch_instance
                          (:repeat (:prec-left 0 (:seq "," mos_switch_instance))))))
                       (:seq
                        n_input_gatetype
                        (:choice drive_strength :blank)
                        (:choice delay2 :blank)
                        (:prec-left 0
                         (:seq
                          n_input_gate_instance
                          (:repeat (:prec-left 0 (:seq "," n_input_gate_instance))))))
                       (:seq
                        n_output_gatetype
                        (:choice drive_strength :blank)
                        (:choice delay2 :blank)
                        (:prec-left 0
                         (:seq
                          n_output_gate_instance
                          (:repeat (:prec-left 0 (:seq "," n_output_gate_instance))))))
                       (:seq
                        pass_en_switchtype
                        (:choice delay2 :blank)
                        (:prec-left 0
                         (:seq
                          pass_enable_switch_instance
                          (:repeat (:prec-left 0 (:seq "," pass_enable_switch_instance))))))
                       (:seq
                        pass_switchtype
                        (:prec-left 0
                         (:seq
                          pass_switch_instance
                          (:repeat (:prec-left 0 (:seq "," pass_switch_instance))))))
                       (:seq
                        "pulldown"
                        (:choice pulldown_strength :blank)
                        (:prec-left 0
                         (:seq
                          pull_gate_instance
                          (:repeat (:prec-left 0 (:seq "," pull_gate_instance))))))
                       (:seq
                        "pullup"
                        (:choice pullup_strength :blank)
                        (:prec-left 0
                         (:seq
                          pull_gate_instance
                          (:repeat (:prec-left 0 (:seq "," pull_gate_instance)))))))
                      ";")
  cmos_switch_instance (:seq
                        (:choice name_of_instance :blank)
                        "("
                        output_terminal
                        ","
                        input_terminal
                        ","
                        ncontrol_terminal
                        ","
                        pcontrol_terminal
                        ")")
  enable_gate_instance (:seq
                        (:choice name_of_instance :blank)
                        "("
                        output_terminal
                        ","
                        input_terminal
                        ","
                        enable_terminal
                        ")")
  mos_switch_instance (:seq
                       (:choice name_of_instance :blank)
                       "("
                       output_terminal
                       ","
                       input_terminal
                       ","
                       enable_terminal
                       ")")
  n_input_gate_instance (:seq
                         (:choice name_of_instance :blank)
                         "("
                         output_terminal
                         ","
                         (:prec-left 0
                          (:seq input_terminal (:repeat (:prec-left 0 (:seq "," input_terminal)))))
                         ")")
  n_output_gate_instance (:seq
                          (:choice name_of_instance :blank)
                          "("
                          (:prec-left 0
                           (:seq
                            output_terminal
                            (:repeat (:prec-left 0 (:seq "," output_terminal)))))
                          ","
                          input_terminal
                          ")")
  pass_switch_instance (:seq
                        (:choice name_of_instance :blank)
                        "("
                        inout_terminal
                        ","
                        inout_terminal
                        ")")
  pass_enable_switch_instance (:seq
                               (:choice name_of_instance :blank)
                               "("
                               inout_terminal
                               ","
                               inout_terminal
                               ","
                               enable_terminal
                               ")")
  pull_gate_instance (:seq (:choice name_of_instance :blank) "(" output_terminal ")")
  pulldown_strength (:choice
                     (:seq "(" strength0 "," strength1 ")")
                     (:seq "(" strength1 "," strength0 ")")
                     (:seq "(" strength0 ")"))
  pullup_strength (:choice
                   (:seq "," strength0 "," strength1 ")")
                   (:seq "," strength1 "," strength0 ")")
                   (:seq "," strength1 ")"))
  enable_terminal expression
  inout_terminal net_lvalue
  input_terminal expression
  ncontrol_terminal expression
  output_terminal net_lvalue
  pcontrol_terminal expression
  cmos_switchtype (:choice "cmos" "rcmos")
  enable_gatetype (:choice "bufif0" "bufif1" "notif0" "notif1")
  mos_switchtype (:choice "nmos" "pmos" "rnmos" "rpmos")
  n_input_gatetype (:choice "and" "nand" "or" "nor" "xor" "xnor")
  n_output_gatetype (:choice "buf" "not")
  pass_en_switchtype (:choice "tranif0" "tranif1" "rtranif1" "rtranif0")
  pass_switchtype (:choice "tran" "rtran")
  module_instantiation (:seq
                        _module_identifier
                        (:choice parameter_value_assignment :blank)
                        (:prec-left 0
                         (:seq
                          hierarchical_instance
                          (:repeat (:prec-left 0 (:seq "," hierarchical_instance)))))
                        ";")
  parameter_value_assignment (:seq "#" "(" (:choice list_of_parameter_assignments :blank) ")")
  list_of_parameter_assignments (:choice
                                 (:prec-left 0
                                  (:seq
                                   ordered_parameter_assignment
                                   (:repeat (:prec-left 0 (:seq "," ordered_parameter_assignment)))))
                                 (:prec-left 0
                                  (:seq
                                   named_parameter_assignment
                                   (:repeat (:prec-left 0 (:seq "," named_parameter_assignment))))))
  ordered_parameter_assignment (:alias param_expression _ordered_parameter_assignment)
  named_parameter_assignment (:seq
                              "."
                              parameter_identifier
                              "("
                              (:choice param_expression :blank)
                              ")")
  hierarchical_instance (:seq name_of_instance "(" (:choice list_of_port_connections :blank) ")")
  name_of_instance (:seq instance_identifier (:repeat unpacked_dimension))
  list_of_port_connections (:choice
                            (:prec-left 0
                             (:seq
                              named_port_connection
                              (:repeat (:prec-left 0 (:seq "," named_port_connection)))))
                            (:prec-left 0
                             (:seq
                              ordered_port_connection
                              (:repeat (:prec-left 0 (:seq "," ordered_port_connection))))))
  ordered_port_connection (:seq (:repeat attribute_instance) expression)
  named_port_connection (:seq
                         (:repeat attribute_instance)
                         (:choice
                          (:seq
                           "."
                           port_identifier
                           (:choice
                            (:prec-left 0 (:seq "(" (:choice expression :blank) ")"))
                            :blank))
                          ".*"))
  interface_instantiation (:seq
                           interface_identifier
                           (:choice parameter_value_assignment :blank)
                           (:prec-left 0
                            (:seq
                             hierarchical_instance
                             (:repeat (:prec-left 0 (:seq "," hierarchical_instance)))))
                           ";")
  program_instantiation (:seq
                         program_identifier
                         (:choice parameter_value_assignment :blank)
                         (:prec-left 0
                          (:seq
                           hierarchical_instance
                           (:repeat (:prec-left 0 (:seq "," hierarchical_instance)))))
                         ";")
  checker_instantiation (:seq
                         ps_checker_identifier
                         name_of_instance
                         "("
                         (:choice
                          (:prec-left 0
                           (:seq
                            (:choice
                             (:prec-left 0
                              (:seq
                               (:repeat attribute_instance)
                               (:choice _property_actual_arg :blank)))
                             :blank)
                            (:repeat
                             (:prec-left 0
                              (:seq
                               ","
                               (:choice
                                (:prec-left 0
                                 (:seq
                                  (:repeat attribute_instance)
                                  (:choice _property_actual_arg :blank)))
                                :blank))))))
                          (:prec-left 0
                           (:seq
                            (:choice
                             (:seq
                              (:repeat attribute_instance)
                              "."
                              formal_port_identifier
                              (:choice
                               (:prec-left 0 (:seq "(" (:choice _property_actual_arg :blank) ")"))
                               :blank))
                             (:seq (:repeat attribute_instance) ".*"))
                            (:repeat
                             (:prec-left 0
                              (:seq
                               ","
                               (:choice
                                (:seq
                                 (:repeat attribute_instance)
                                 "."
                                 formal_port_identifier
                                 (:choice
                                  (:prec-left 0
                                   (:seq "(" (:choice _property_actual_arg :blank) ")"))
                                  :blank))
                                (:seq (:repeat attribute_instance) ".*"))))))))
                         ")"
                         ";")
  generate_region (:seq "generate" (:repeat _generate_item) "endgenerate")
  loop_generate_construct (:seq
                           "for"
                           "("
                           genvar_initialization
                           ";"
                           _genvar_expression
                           ";"
                           genvar_iteration
                           ")"
                           generate_block)
  genvar_initialization (:seq (:choice "genvar" :blank) genvar_identifier "=" constant_expression)
  genvar_iteration (:choice
                    (:seq genvar_identifier assignment_operator _genvar_expression)
                    (:seq inc_or_dec_operator genvar_identifier)
                    (:seq genvar_identifier inc_or_dec_operator))
  _conditional_generate_construct (:choice if_generate_construct case_generate_construct)
  if_generate_construct (:prec-left 0
                         (:seq
                          "if"
                          "("
                          constant_expression
                          ")"
                          generate_block
                          (:choice (:prec-left 0 (:seq "else" generate_block)) :blank)))
  case_generate_construct (:seq
                           "case"
                           "("
                           constant_expression
                           ")"
                           case_generate_item
                           (:repeat case_generate_item)
                           "endcase")
  case_generate_item (:choice
                      (:seq
                       (:prec-left 0
                        (:seq
                         constant_expression
                         (:repeat (:prec-left 0 (:seq "," constant_expression)))))
                       ":"
                       generate_block)
                      (:seq "default" (:choice ":" :blank) generate_block))
  generate_block (:choice
                  _generate_item
                  (:seq
                   (:choice (:prec-left 0 (:seq generate_block_identifier ":")) :blank)
                   "begin"
                   (:choice (:prec-left 0 (:seq ":" generate_block_identifier)) :blank)
                   (:repeat _generate_item)
                   "end"
                   (:choice (:prec-left 0 (:seq ":" generate_block_identifier)) :blank)))
  _generate_item (:choice
                  module_or_generate_item
                  interface_or_generate_item
                  _checker_or_generate_item)
  udp_nonansi_declaration (:seq
                           (:repeat attribute_instance)
                           "primitive"
                           _udp_identifier
                           "("
                           udp_port_list
                           ")"
                           ";")
  udp_ansi_declaration (:seq
                        (:repeat attribute_instance)
                        "primitive"
                        _udp_identifier
                        "("
                        udp_declaration_port_list
                        ")"
                        ";")
  udp_declaration (:choice
                   (:seq
                    udp_nonansi_declaration
                    udp_port_declaration
                    (:repeat udp_port_declaration)
                    _udp_body
                    "endprimitive"
                    (:choice (:prec-left 0 (:seq ":" _udp_identifier)) :blank))
                   (:seq
                    udp_ansi_declaration
                    _udp_body
                    "endprimitive"
                    (:choice (:prec-left 0 (:seq ":" _udp_identifier)) :blank))
                   (:seq "extern" udp_nonansi_declaration)
                   (:seq "extern" udp_ansi_declaration)
                   (:seq
                    (:repeat attribute_instance)
                    "primitive"
                    _udp_identifier
                    "("
                    ".*"
                    ")"
                    ";"
                    (:repeat udp_port_declaration)
                    _udp_body
                    "endprimitive"
                    (:choice (:prec-left 0 (:seq ":" _udp_identifier)) :blank)))
  udp_port_list (:seq
                 output_port_identifier
                 ","
                 (:prec-left 0
                  (:seq
                   input_port_identifier
                   (:repeat (:prec-left 0 (:seq "," input_port_identifier))))))
  udp_declaration_port_list (:seq
                             udp_output_declaration
                             ","
                             (:prec-left 0
                              (:seq
                               udp_input_declaration
                               (:repeat (:prec-left 0 (:seq "," udp_input_declaration))))))
  udp_port_declaration (:seq
                        (:choice udp_output_declaration udp_input_declaration udp_reg_declaration)
                        ";")
  udp_output_declaration (:seq
                          (:repeat attribute_instance)
                          "output"
                          (:choice
                           port_identifier
                           (:seq
                            "reg"
                            port_identifier
                            (:choice (:prec-left 0 (:seq "=" constant_expression)) :blank))))
  udp_input_declaration (:seq (:repeat attribute_instance) "input" list_of_udp_port_identifiers)
  udp_reg_declaration (:seq (:repeat attribute_instance) "reg" _variable_identifier)
  _udp_body (:choice combinational_body sequential_body)
  combinational_body (:seq "table" (:repeat1 combinational_entry) "endtable")
  combinational_entry (:seq level_input_list ":" output_symbol ";")
  sequential_body (:seq
                   (:choice udp_initial_statement :blank)
                   "table"
                   (:repeat1 sequential_entry)
                   "endtable")
  udp_initial_statement (:seq "initial" output_port_identifier "=" init_val ";")
  init_val (:choice "1'b0" "1'b1" "1'bx" "1'bX" "1'B0" "1'B1" "1'Bx" "1'BX" "1" "0")
  sequential_entry (:seq _seq_input_list ":" _current_state ":" next_state ";")
  _seq_input_list (:choice level_input_list edge_input_list)
  level_input_list (:repeat1 level_symbol)
  edge_input_list (:seq (:repeat level_symbol) edge_indicator (:repeat level_symbol))
  edge_indicator (:choice (:seq "(" level_symbol level_symbol ")") edge_symbol)
  _current_state level_symbol
  next_state (:choice output_symbol "-")
  output_symbol (:pattern "[01xX]")
  level_symbol (:pattern "[01xX?bB]")
  edge_symbol (:pattern "[rRfFpPnN*]")
  udp_instantiation (:seq
                     _udp_identifier
                     (:choice drive_strength :blank)
                     (:choice delay2 :blank)
                     (:prec-left 0
                      (:seq udp_instance (:repeat (:prec-left 0 (:seq "," udp_instance)))))
                     ";")
  udp_instance (:seq
                (:choice name_of_instance :blank)
                "("
                output_terminal
                ","
                (:prec-left 0
                 (:seq input_terminal (:repeat (:prec-left 0 (:seq "," input_terminal)))))
                ")")
  continuous_assign (:seq
                     "assign"
                     (:choice
                      (:seq
                       (:choice drive_strength :blank)
                       (:choice delay3 :blank)
                       list_of_net_assignments)
                      (:seq (:choice delay_control :blank) list_of_variable_assignments))
                     ";")
  list_of_net_assignments (:prec-left 0
                           (:seq net_assignment (:repeat (:prec-left 0 (:seq "," net_assignment)))))
  list_of_variable_assignments (:prec-left 0
                                (:seq
                                 variable_assignment
                                 (:repeat (:prec-left 0 (:seq "," variable_assignment)))))
  net_alias (:prec-left 21
             (:seq
              "alias"
              net_lvalue
              "="
              (:prec-left 0
               (:seq
                (:seq "=" net_lvalue)
                (:repeat (:prec-left 0 (:seq "," (:seq "=" net_lvalue))))))
              ";"))
  net_assignment (:prec-left 21 (:seq net_lvalue "=" expression))
  initial_construct (:seq "initial" statement_or_null)
  always_construct (:seq always_keyword statement)
  always_keyword (:choice "always" "always_comb" "always_latch" "always_ff")
  final_construct (:seq "final" function_statement)
  blocking_assignment (:choice
                       (:prec-left 21 (:seq variable_lvalue "=" delay_or_event_control expression))
                       (:prec-left 21 (:seq nonrange_variable_lvalue "=" dynamic_array_new))
                       operator_assignment)
  operator_assignment (:prec-left 21 (:seq variable_lvalue assignment_operator expression))
  assignment_operator (:choice
                       "="
                       "+="
                       "-="
                       "*="
                       "/="
                       "%="
                       "&="
                       "|="
                       "^="
                       "<<="
                       ">>="
                       "<<<="
                       ">>>=")
  nonblocking_assignment (:prec-left 21
                          (:seq
                           variable_lvalue
                           "<="
                           (:choice delay_or_event_control :blank)
                           expression))
  procedural_continuous_assignment (:choice
                                    (:seq "assign" variable_assignment)
                                    (:seq "deassign" variable_lvalue)
                                    (:seq "force" variable_assignment)
                                    (:seq "force" net_assignment)
                                    (:seq "release" variable_lvalue)
                                    (:seq "release" net_lvalue))
  variable_assignment (:prec-left 21 (:seq variable_lvalue "=" expression))
  action_block (:choice
                statement_or_null
                (:seq (:choice statement :blank) "else" statement_or_null))
  seq_block (:seq
             "begin"
             (:choice (:prec-left 0 (:seq ":" _block_identifier)) :blank)
             (:repeat block_item_declaration)
             (:repeat statement_or_null)
             "end"
             (:choice (:prec-left 0 (:seq ":" _block_identifier)) :blank))
  par_block (:seq
             "fork"
             (:choice (:prec-left 0 (:seq ":" _block_identifier)) :blank)
             (:repeat block_item_declaration)
             (:repeat statement_or_null)
             join_keyword
             (:choice (:prec-left 0 (:seq ":" _block_identifier)) :blank))
  join_keyword (:choice "join" "join_any" "join_none")
  statement_or_null (:choice statement (:seq (:repeat attribute_instance) ";"))
  statement (:seq
             (:choice (:prec-left 0 (:seq _block_identifier ":")) :blank)
             (:repeat attribute_instance)
             statement_item)
  statement_item (:choice
                  (:seq blocking_assignment ";")
                  (:seq nonblocking_assignment ";")
                  (:seq procedural_continuous_assignment ";")
                  (:seq system_tf_call ";")
                  case_statement
                  conditional_statement
                  (:seq inc_or_dec_expression ";")
                  disable_statement
                  event_trigger
                  loop_statement
                  jump_statement
                  par_block
                  seq_block
                  procedural_timing_control_statement
                  wait_statement
                  _procedural_assertion_statement
                  (:seq clocking_drive ";")
                  randcase_statement
                  expect_property_statement)
  function_statement statement
  function_statement_or_null (:choice function_statement (:seq (:repeat attribute_instance) ";"))
  variable_identifier_list (:prec-left 0
                            (:seq
                             _variable_identifier
                             (:repeat (:prec-left 0 (:seq "," _variable_identifier)))))
  procedural_timing_control_statement (:seq _procedural_timing_control statement_or_null)
  delay_or_event_control (:choice
                          delay_control
                          event_control
                          (:seq "repeat" "(" expression ")" event_control))
  delay_control (:seq "#" (:choice delay_value (:seq "(" mintypmax_expression ")")))
  event_control (:choice
                 (:seq "@" _hierarchical_event_identifier)
                 (:seq "@" "(" event_expression ")")
                 "@*"
                 (:seq "@" "(" "*" ")")
                 (:seq "@" ps_or_hierarchical_sequence_identifier))
  event_expression (:choice
                    (:prec-left 0 (:seq event_expression "or" event_expression))
                    (:prec-left 0 (:seq event_expression "," event_expression))
                    (:seq (:choice edge_identifier :blank) expression))
  _procedural_timing_control (:choice delay_control event_control cycle_delay)
  jump_statement (:choice
                  (:seq "return" (:choice expression :blank) ";")
                  (:seq "break" ";")
                  (:seq "continue" ";"))
  wait_statement (:choice
                  (:seq "wait" "(" expression ")" statement_or_null)
                  (:seq "wait" "fork" ";")
                  (:seq
                   "wait_order"
                   "("
                   (:prec-left 0
                    (:seq
                     hierarchical_identifier
                     (:repeat (:prec-left 0 (:seq "," hierarchical_identifier)))))
                   ")"
                   action_block))
  event_trigger (:choice
                 (:seq "->" _hierarchical_event_identifier ";")
                 (:seq
                  "->>"
                  (:choice delay_or_event_control :blank)
                  _hierarchical_event_identifier
                  ";"))
  disable_statement (:choice
                     (:seq "disable" _hierarchical_task_identifier ";")
                     (:seq "disable" _hierarchical_block_identifier ";")
                     (:seq "disable" "fork" ";"))
  conditional_statement (:prec-left 0
                         (:seq
                          (:choice unique_priority :blank)
                          "if"
                          "("
                          cond_predicate
                          ")"
                          statement_or_null
                          (:choice (:prec-left 0 (:seq "else" statement_or_null)) :blank)))
  unique_priority (:choice "unique" "unique0" "priority")
  cond_predicate (:prec-left 37
                  (:seq
                   _expression_or_cond_pattern
                   (:repeat (:prec-left 0 (:seq "&&&" _expression_or_cond_pattern)))))
  _expression_or_cond_pattern (:choice expression cond_pattern)
  cond_pattern (:prec-left 26 (:seq expression "matches" pattern))
  case_statement (:seq
                  (:choice unique_priority :blank)
                  (:seq
                   case_keyword
                   "("
                   case_expression
                   ")"
                   (:choice
                    (:repeat1 case_item)
                    (:seq "matches" (:repeat1 case_pattern_item))
                    (:seq "inside" (:repeat1 case_inside_item))))
                  "endcase")
  case_keyword (:choice "case" "casez" "casex")
  case_expression expression
  case_item (:choice
             (:seq
              (:prec-left 0
               (:seq case_item_expression (:repeat (:prec-left 0 (:seq "," case_item_expression)))))
              ":"
              statement_or_null)
             (:seq "default" (:choice ":" :blank) statement_or_null))
  case_pattern_item (:choice
                     (:seq
                      pattern
                      (:choice (:prec-left 0 (:seq "&&&" expression)) :blank)
                      ":"
                      statement_or_null)
                     (:seq "default" (:choice ":" :blank) statement_or_null))
  case_inside_item (:choice
                    (:seq open_range_list ":" statement_or_null)
                    (:seq "default" (:choice ":" :blank) statement_or_null))
  case_item_expression expression
  randcase_statement (:seq "randcase" randcase_item (:repeat randcase_item) "endcase")
  randcase_item (:seq expression ":" statement_or_null)
  open_range_list (:prec-left 0
                   (:seq open_value_range (:repeat (:prec-left 0 (:seq "," open_value_range)))))
  open_value_range value_range
  pattern (:choice
           (:seq "." _variable_identifier)
           ".*"
           constant_expression
           (:seq "tagged" member_identifier (:choice pattern :blank))
           (:seq "'{" (:prec-left 0 (:seq pattern (:repeat (:prec-left 0 (:seq "," pattern))))) "}")
           (:seq
            "'{"
            (:prec-left 0
             (:seq
              (:seq member_identifier ":" pattern)
              (:repeat (:prec-left 0 (:seq "," (:seq member_identifier ":" pattern))))))
            "}"))
  assignment_pattern (:seq
                      "'{"
                      (:choice
                       (:prec-left 0
                        (:seq expression (:repeat (:prec-left 0 (:seq "," expression)))))
                       (:prec-left 0
                        (:seq
                         (:seq _array_pattern_key ":" expression)
                         (:repeat
                          (:prec-left 0 (:seq "," (:seq _array_pattern_key ":" expression))))))
                       (:seq
                        constant_expression
                        "{"
                        (:prec-left 0
                         (:seq expression (:repeat (:prec-left 0 (:seq "," expression)))))
                        "}"))
                      "}")
  _structure_pattern_key (:choice member_identifier assignment_pattern_key)
  _array_pattern_key (:choice constant_expression assignment_pattern_key)
  assignment_pattern_key (:choice _simple_type "default")
  assignment_pattern_expression (:seq
                                 (:choice _assignment_pattern_expression_type :blank)
                                 assignment_pattern)
  _assignment_pattern_expression_type (:choice ps_type_identifier integer_atom_type type_reference)
  constant_assignment_pattern_expression assignment_pattern_expression
  assignment_pattern_net_lvalue (:seq
                                 "'{"
                                 (:prec-left 0
                                  (:seq net_lvalue (:repeat (:prec-left 0 (:seq "," net_lvalue)))))
                                 "}")
  assignment_pattern_variable_lvalue (:seq
                                      "'{"
                                      (:prec-left 0
                                       (:seq
                                        variable_lvalue
                                        (:repeat (:prec-left 0 (:seq "," variable_lvalue)))))
                                      "}")
  loop_statement (:choice
                  (:seq "forever" statement_or_null)
                  (:seq "repeat" "(" expression ")" statement_or_null)
                  (:seq "while" "(" expression ")" statement_or_null)
                  (:seq
                   "for"
                   "("
                   (:choice for_initialization :blank)
                   ";"
                   (:choice expression :blank)
                   ";"
                   (:choice for_step :blank)
                   ")"
                   statement_or_null)
                  (:seq "do" statement_or_null "while" "(" expression ")" ";")
                  (:seq
                   "foreach"
                   "("
                   ps_or_hierarchical_array_identifier
                   "["
                   (:choice loop_variables1 :blank)
                   "]"
                   ")"
                   statement))
  for_initialization (:choice
                      list_of_variable_assignments
                      (:prec-left 0
                       (:seq
                        for_variable_declaration
                        (:repeat (:prec-left 0 (:seq "," for_variable_declaration))))))
  for_variable_declaration (:seq
                            (:choice "var" :blank)
                            data_type
                            (:prec-left 0
                             (:seq
                              (:seq _variable_identifier "=" expression)
                              (:repeat
                               (:prec-left 0 (:seq "," (:seq _variable_identifier "=" expression)))))))
  for_step (:prec-left 0
            (:seq _for_step_assignment (:repeat (:prec-left 0 (:seq "," _for_step_assignment)))))
  _for_step_assignment (:choice operator_assignment inc_or_dec_expression function_subroutine_call)
  loop_variables1 (:seq
                   index_variable_identifier
                   (:repeat (:prec-left 0 (:seq "," (:choice index_variable_identifier :blank)))))
  subroutine_call_statement (:choice
                             (:seq subroutine_call ";")
                             (:seq "void'" "(" function_subroutine_call ")" ";"))
  _assertion_item (:choice concurrent_assertion_item deferred_immediate_assertion_item)
  deferred_immediate_assertion_item (:seq
                                     (:choice (:prec-left 0 (:seq _block_identifier ":")) :blank)
                                     _deferred_immediate_assertion_statement)
  _procedural_assertion_statement (:choice
                                   _concurrent_assertion_statement
                                   _immediate_assertion_statement
                                   checker_instantiation)
  _immediate_assertion_statement (:choice
                                  _simple_immediate_assertion_statement
                                  _deferred_immediate_assertion_statement)
  _simple_immediate_assertion_statement (:choice
                                         simple_immediate_assert_statement
                                         simple_immediate_assume_statement
                                         simple_immediate_cover_statement)
  simple_immediate_assert_statement (:seq "assert" "(" expression ")" action_block)
  simple_immediate_assume_statement (:seq "assume" "(" expression ")" action_block)
  simple_immediate_cover_statement (:seq "cover" "(" expression ")" statement_or_null)
  _deferred_immediate_assertion_statement (:choice
                                           deferred_immediate_assert_statement
                                           deferred_immediate_assume_statement
                                           deferred_immediate_cover_statement)
  deferred_immediate_assert_statement (:seq
                                       "assert"
                                       (:choice "#0" "final")
                                       "("
                                       expression
                                       ")"
                                       action_block)
  deferred_immediate_assume_statement (:seq
                                       "assume"
                                       (:choice "#0" "final")
                                       "("
                                       expression
                                       ")"
                                       action_block)
  deferred_immediate_cover_statement (:seq
                                      "cover"
                                      (:choice "#0" "final")
                                      "("
                                      expression
                                      ")"
                                      statement_or_null)
  clocking_declaration (:choice
                        (:seq
                         (:choice "default" :blank)
                         "clocking"
                         (:choice clocking_identifier :blank)
                         clocking_event
                         ";"
                         (:repeat clocking_item)
                         "endclocking"
                         (:choice (:prec-left 0 (:seq ":" clocking_identifier)) :blank))
                        (:seq
                         "global"
                         "clocking"
                         (:choice clocking_identifier :blank)
                         clocking_event
                         ";"
                         "endclocking"
                         (:choice (:prec-left 0 (:seq ":" clocking_identifier)) :blank)))
  clocking_event (:seq "@" (:choice _identifier (:seq "(" event_expression ")")))
  clocking_item (:choice
                 (:seq "default" default_skew ";")
                 (:seq clocking_direction list_of_clocking_decl_assign ";")
                 (:seq (:repeat attribute_instance) _assertion_item_declaration))
  default_skew (:choice
                (:seq "input" clocking_skew)
                (:seq "output" clocking_skew)
                (:seq "input" clocking_skew "output" clocking_skew))
  clocking_direction (:choice
                      (:seq "input" (:choice clocking_skew :blank))
                      (:seq "output" (:choice clocking_skew :blank))
                      (:seq
                       "input"
                       (:choice clocking_skew :blank)
                       "output"
                       (:choice clocking_skew :blank))
                      (:seq "inout"))
  list_of_clocking_decl_assign (:prec-left 0
                                (:seq
                                 clocking_decl_assign
                                 (:repeat (:prec-left 0 (:seq "," clocking_decl_assign)))))
  clocking_decl_assign (:seq
                        _signal_identifier
                        (:choice (:prec-left 0 (:seq "=" expression)) :blank))
  clocking_skew (:choice (:seq edge_identifier (:choice delay_control :blank)) delay_control)
  clocking_drive (:prec-left 21
                  (:seq clockvar_expression "<=" (:choice cycle_delay :blank) expression))
  cycle_delay (:prec-left 0
               (:seq "##" (:choice integral_number _identifier (:seq "(" expression ")"))))
  clockvar hierarchical_identifier
  clockvar_expression (:seq clockvar (:choice select1 :blank))
  specify_block (:seq "specify" (:repeat _specify_item) "endspecify")
  _specify_item (:choice
                 specparam_declaration
                 pulsestyle_declaration
                 showcancelled_declaration
                 path_declaration
                 _system_timing_check)
  pulsestyle_declaration (:seq
                          (:choice "pulsestyle_onevent" "pulsestyle_ondetect")
                          list_of_path_outputs
                          ";")
  showcancelled_declaration (:seq
                             (:choice "showcancelled" "noshowcancelled")
                             list_of_path_outputs
                             ";")
  path_declaration (:seq
                    (:choice
                     simple_path_declaration
                     edge_sensitive_path_declaration
                     state_dependent_path_declaration)
                    ";")
  simple_path_declaration (:seq
                           (:choice parallel_path_description full_path_description)
                           "="
                           path_delay_value)
  parallel_path_description (:seq
                             "("
                             specify_input_terminal_descriptor
                             (:choice polarity_operator :blank)
                             "=>"
                             specify_output_terminal_descriptor
                             ")")
  full_path_description (:seq
                         "("
                         list_of_path_inputs
                         (:choice polarity_operator :blank)
                         "*>"
                         list_of_path_outputs
                         ")")
  list_of_path_inputs (:prec-left 0
                       (:seq
                        specify_input_terminal_descriptor
                        (:repeat (:prec-left 0 (:seq "," specify_input_terminal_descriptor)))))
  list_of_path_outputs (:prec-left 0
                        (:seq
                         specify_output_terminal_descriptor
                         (:repeat (:prec-left 0 (:seq "," specify_output_terminal_descriptor)))))
  specify_input_terminal_descriptor (:seq
                                     input_identifier
                                     (:choice
                                      (:prec-left 0 (:seq "[" _constant_range_expression "]"))
                                      :blank))
  specify_output_terminal_descriptor (:seq
                                      output_identifier
                                      (:choice
                                       (:prec-left 0 (:seq "[" _constant_range_expression "]"))
                                       :blank))
  input_identifier (:choice
                    input_port_identifier
                    inout_port_identifier
                    (:seq interface_identifier "." port_identifier))
  output_identifier (:choice
                     output_port_identifier
                     inout_port_identifier
                     (:seq interface_identifier "." port_identifier))
  path_delay_value (:choice
                    list_of_path_delay_expressions
                    (:seq "(" list_of_path_delay_expressions ")"))
  list_of_path_delay_expressions (:prec-left 0
                                  (:seq
                                   path_delay_expression
                                   (:repeat (:prec-left 0 (:seq "," path_delay_expression)))))
  path_delay_expression constant_mintypmax_expression
  edge_sensitive_path_declaration (:seq
                                   (:choice
                                    parallel_edge_sensitive_path_description
                                    full_edge_sensitive_path_description)
                                   "="
                                   path_delay_value)
  parallel_edge_sensitive_path_description (:seq
                                            "("
                                            (:choice edge_identifier :blank)
                                            specify_input_terminal_descriptor
                                            (:choice polarity_operator :blank)
                                            "=>"
                                            "("
                                            specify_output_terminal_descriptor
                                            (:choice polarity_operator :blank)
                                            ":"
                                            data_source_expression
                                            ")"
                                            ")")
  full_edge_sensitive_path_description (:seq
                                        "("
                                        (:choice edge_identifier :blank)
                                        list_of_path_inputs
                                        (:choice polarity_operator :blank)
                                        "*>"
                                        "("
                                        list_of_path_outputs
                                        (:choice polarity_operator :blank)
                                        ":"
                                        data_source_expression
                                        ")"
                                        ")")
  data_source_expression expression
  edge_identifier (:choice "posedge" "negedge" "edge")
  state_dependent_path_declaration (:choice
                                    (:seq
                                     "if"
                                     "("
                                     module_path_expression
                                     ")"
                                     simple_path_declaration)
                                    (:seq
                                     "if"
                                     "("
                                     module_path_expression
                                     ")"
                                     edge_sensitive_path_declaration)
                                    (:seq "ifnone" simple_path_declaration))
  polarity_operator (:choice "+" "-")
  _system_timing_check (:choice
                        $setup_timing_check
                        $hold_timing_check
                        $setuphold_timing_check
                        $recovery_timing_check
                        $removal_timing_check
                        $recrem_timing_check
                        $skew_timing_check
                        $timeskew_timing_check
                        $fullskew_timing_check
                        $period_timing_check
                        $width_timing_check
                        $nochange_timing_check)
  $setup_timing_check (:seq
                       "$setup"
                       "("
                       data_event
                       ","
                       reference_event
                       ","
                       timing_check_limit
                       (:choice (:prec-left 0 (:seq "," (:choice notifier :blank))) :blank)
                       ")"
                       ";")
  $hold_timing_check (:seq
                      "$hold"
                      "("
                      reference_event
                      ","
                      data_event
                      ","
                      timing_check_limit
                      (:choice (:prec-left 0 (:seq "," (:choice notifier :blank))) :blank)
                      ")"
                      ";")
  $setuphold_timing_check (:seq
                           "$setuphold"
                           "("
                           reference_event
                           ","
                           data_event
                           ","
                           timing_check_limit
                           ","
                           timing_check_limit
                           (:choice
                            (:prec-left 0
                             (:seq
                              ","
                              (:choice notifier :blank)
                              (:choice
                               (:prec-left 0
                                (:seq
                                 ","
                                 (:choice timestamp_condition :blank)
                                 (:choice
                                  (:prec-left 0
                                   (:seq
                                    ","
                                    (:choice timecheck_condition :blank)
                                    (:choice
                                     (:prec-left 0
                                      (:seq
                                       ","
                                       (:choice delayed_reference :blank)
                                       (:choice
                                        (:prec-left 0 (:seq "," (:choice delayed_data :blank)))
                                        :blank)))
                                     :blank)))
                                  :blank)))
                               :blank)))
                            :blank)
                           ")"
                           ";")
  $recovery_timing_check (:seq
                          "$recovery"
                          "("
                          reference_event
                          ","
                          data_event
                          ","
                          timing_check_limit
                          (:choice (:prec-left 0 (:seq "," (:choice notifier :blank))) :blank)
                          ")"
                          ";")
  $removal_timing_check (:seq
                         "$removal"
                         "("
                         reference_event
                         ","
                         data_event
                         ","
                         timing_check_limit
                         (:choice (:prec-left 0 (:seq "," (:choice notifier :blank))) :blank)
                         ")"
                         ";")
  $recrem_timing_check (:seq
                        "$recrem"
                        "("
                        reference_event
                        ","
                        data_event
                        ","
                        timing_check_limit
                        ","
                        timing_check_limit
                        (:choice
                         (:prec-left 0
                          (:seq
                           ","
                           (:choice notifier :blank)
                           (:choice
                            (:prec-left 0
                             (:seq
                              ","
                              (:choice timestamp_condition :blank)
                              (:choice
                               (:prec-left 0 (:seq "," (:choice timecheck_condition :blank)))
                               :blank)
                              (:choice
                               (:prec-left 0
                                (:seq
                                 ","
                                 (:choice delayed_reference :blank)
                                 (:choice
                                  (:prec-left 0 (:seq "," (:choice delayed_data :blank)))
                                  :blank)))
                               :blank)))
                            :blank)))
                         :blank)
                        ")"
                        ";")
  $skew_timing_check (:seq
                      "$skew"
                      "("
                      reference_event
                      ","
                      data_event
                      ","
                      timing_check_limit
                      (:choice (:prec-left 0 (:seq "," (:choice notifier :blank))) :blank)
                      ")"
                      ";")
  $timeskew_timing_check (:seq
                          "$timeskew"
                          "("
                          reference_event
                          ","
                          data_event
                          ","
                          timing_check_limit
                          (:choice
                           (:prec-left 0
                            (:seq
                             ","
                             (:choice notifier :blank)
                             (:choice
                              (:prec-left 0
                               (:seq
                                ","
                                (:choice event_based_flag :blank)
                                (:choice
                                 (:prec-left 0 (:seq "," (:choice remain_active_flag :blank)))
                                 :blank)))
                              :blank)))
                           :blank)
                          ")"
                          ";")
  $fullskew_timing_check (:seq
                          "$fullskew"
                          "("
                          reference_event
                          ","
                          data_event
                          ","
                          timing_check_limit
                          ","
                          timing_check_limit
                          (:choice
                           (:prec-left 0
                            (:seq
                             ","
                             (:choice notifier :blank)
                             (:choice
                              (:prec-left 0
                               (:seq
                                ","
                                (:choice event_based_flag :blank)
                                (:choice
                                 (:prec-left 0 (:seq "," (:choice remain_active_flag :blank)))
                                 :blank)))
                              :blank)))
                           :blank)
                          ")"
                          ";")
  $period_timing_check (:seq
                        "$period"
                        "("
                        controlled_reference_event
                        ","
                        timing_check_limit
                        (:choice (:prec-left 0 (:seq "," (:choice notifier :blank))) :blank)
                        ")"
                        ";")
  $width_timing_check (:seq
                       "$width"
                       "("
                       controlled_reference_event
                       ","
                       timing_check_limit
                       ","
                       threshold
                       (:choice (:prec-left 0 (:seq "," (:choice notifier :blank))) :blank)
                       ")"
                       ";")
  $nochange_timing_check (:seq
                          "$nochange"
                          "("
                          reference_event
                          ","
                          data_event
                          ","
                          start_edge_offset
                          ","
                          end_edge_offset
                          (:choice (:prec-left 0 (:seq "," (:choice notifier :blank))) :blank)
                          ")"
                          ";")
  timecheck_condition mintypmax_expression
  controlled_reference_event (:alias controlled_timing_check_event controlled_reference_event)
  data_event timing_check_event
  delayed_data (:seq terminal_identifier (:choice constant_mintypmax_expression :blank))
  delayed_reference (:seq terminal_identifier (:choice constant_mintypmax_expression :blank))
  end_edge_offset mintypmax_expression
  event_based_flag constant_expression
  notifier _variable_identifier
  reference_event timing_check_event
  remain_active_flag constant_mintypmax_expression
  timestamp_condition mintypmax_expression
  start_edge_offset mintypmax_expression
  threshold constant_expression
  timing_check_limit expression
  timing_check_event (:seq
                      (:choice timing_check_event_control :blank)
                      _specify_terminal_descriptor
                      (:choice (:prec-left 0 (:seq "&&&" timing_check_condition)) :blank))
  controlled_timing_check_event (:seq
                                 timing_check_event_control
                                 _specify_terminal_descriptor
                                 (:choice (:prec-left 0 (:seq "&&&" timing_check_condition)) :blank))
  timing_check_event_control (:choice "posedge" "negedge" "edge" edge_control_specifier)
  _specify_terminal_descriptor (:choice
                                specify_input_terminal_descriptor
                                specify_output_terminal_descriptor)
  edge_control_specifier (:seq
                          "edge"
                          "["
                          (:prec-left 0
                           (:seq
                            edge_descriptor
                            (:repeat (:prec-left 0 (:seq "," edge_descriptor)))))
                          "]")
  edge_descriptor (:choice "01" "10" (:pattern "[xXzZ][01]") (:pattern "[01][xXzZ]"))
  timing_check_condition (:choice
                          scalar_timing_check_condition
                          (:seq "(" scalar_timing_check_condition ")"))
  scalar_timing_check_condition (:choice
                                 expression
                                 (:seq "~" expression)
                                 (:seq expression "==" scalar_constant)
                                 (:seq expression "===" scalar_constant)
                                 (:seq expression "!=" scalar_constant)
                                 (:seq expression "!==" scalar_constant))
  scalar_constant (:choice "1'b0" "1'b1" "1'B0" "1'B1" "'b0" "'b1" "'B0" "'B1" "1" "0")
  concatenation (:seq
                 "{"
                 (:prec-left 20 (:seq expression (:repeat (:prec-left 0 (:seq "," expression)))))
                 "}")
  constant_concatenation (:seq
                          "{"
                          (:prec-left 20
                           (:seq
                            constant_expression
                            (:repeat (:prec-left 0 (:seq "," constant_expression)))))
                          "}")
  constant_multiple_concatenation (:prec-left 20
                                   (:seq "{" constant_expression constant_concatenation "}"))
  module_path_concatenation (:seq
                             "{"
                             (:prec-left 20
                              (:seq
                               module_path_expression
                               (:repeat (:prec-left 0 (:seq "," module_path_expression)))))
                             "}")
  module_path_multiple_concatenation (:prec-left 20
                                      (:seq "{" constant_expression module_path_concatenation "}"))
  multiple_concatenation (:prec-left 20 (:seq "{" expression concatenation "}"))
  streaming_concatenation (:prec-left 20
                           (:seq
                            "{"
                            stream_operator
                            (:choice slice_size :blank)
                            stream_concatenation
                            "}"))
  stream_operator (:choice ">>" "<<")
  slice_size (:choice _simple_type constant_expression)
  stream_concatenation (:prec-left 20
                        (:seq
                         "{"
                         (:prec-left 0
                          (:seq
                           stream_expression
                           (:repeat (:prec-left 0 (:seq "," stream_expression)))))
                         "}"))
  stream_expression (:seq
                     expression
                     (:choice (:prec-left 0 (:seq "with" "[" array_range_expression "]")) :blank))
  array_range_expression (:seq
                          expression
                          (:choice
                           (:choice
                            (:seq ":" expression)
                            (:seq "+:" expression)
                            (:seq "-:" expression))
                           :blank))
  empty_unpacked_array_concatenation (:seq "{" "}")
  constant_function_call function_subroutine_call
  tf_call (:prec-left 0
           (:seq
            _hierarchical_tf_identifier
            (:repeat attribute_instance)
            (:choice list_of_arguments_parent :blank)))
  system_tf_call (:prec-left 0
                  (:seq
                   system_tf_identifier
                   (:choice
                    (:choice
                     list_of_arguments_parent
                     (:seq
                      "("
                      (:choice
                       (:seq data_type (:choice (:prec-left 0 (:seq "," expression)) :blank))
                       (:prec-left 0
                        (:seq
                         (:prec-left 0
                          (:seq expression (:repeat (:prec-left 0 (:seq "," expression)))))
                         (:choice (:prec-left 0 (:seq "," (:choice clocking_event :blank))) :blank))))
                      ")"))
                    :blank)))
  subroutine_call (:choice
                   tf_call
                   system_tf_call
                   method_call
                   (:seq (:choice (:prec-left 0 (:seq "std" "::")) :blank) randomize_call))
  function_subroutine_call subroutine_call
  list_of_arguments (:choice
                     (:prec-left 0
                      (:seq
                       (:seq "." _identifier "(" (:choice expression :blank) ")")
                       (:repeat
                        (:prec-left 0
                         (:seq "," (:seq "." _identifier "(" (:choice expression :blank) ")")))))))
  list_of_arguments_parent (:seq
                            "("
                            (:choice
                             (:prec-left 0
                              (:seq expression (:repeat (:prec-left 0 (:seq "," expression)))))
                             (:seq
                              (:repeat
                               (:prec-left 0
                                (:seq "," "." _identifier "(" (:choice expression :blank) ")"))))
                             (:prec-left 0
                              (:seq
                               (:seq "," "." _identifier "(" (:choice expression :blank) ")")
                               (:repeat
                                (:prec-left 0
                                 (:seq
                                  ","
                                  (:seq "," "." _identifier "(" (:choice expression :blank) ")")))))))
                            ")")
  method_call (:seq _method_call_root "." method_call_body)
  method_call_body (:choice
                    (:prec-left 0
                     (:seq
                      method_identifier
                      (:repeat attribute_instance)
                      (:choice list_of_arguments_parent :blank)))
                    _built_in_method_call)
  _built_in_method_call (:choice array_manipulation_call randomize_call)
  array_manipulation_call (:prec-left 0
                           (:seq
                            array_method_name
                            (:repeat attribute_instance)
                            (:choice list_of_arguments_parent :blank)
                            (:choice (:prec-left 0 (:seq "with" "(" expression ")")) :blank)))
  randomize_call (:prec-left 0
                  (:seq
                   "randomize"
                   (:repeat attribute_instance)
                   (:choice
                    (:prec-left 0
                     (:seq "(" (:choice (:choice variable_identifier_list "null") :blank) ")"))
                    :blank)
                   (:choice
                    (:prec-left 0
                     (:seq
                      "with"
                      (:choice
                       (:prec-left 0 (:seq "(" (:choice identifier_list :blank) ")"))
                       :blank)
                      constraint_block))
                    :blank)))
  _method_call_root (:choice primary implicit_class_handle)
  array_method_name (:choice method_identifier "unique" "and" "or" "xor")
  inc_or_dec_expression (:choice
                         (:seq inc_or_dec_operator (:repeat attribute_instance) variable_lvalue)
                         (:seq variable_lvalue (:repeat attribute_instance) inc_or_dec_operator))
  conditional_expression (:prec-right 23
                          (:seq
                           cond_predicate
                           "?"
                           (:repeat attribute_instance)
                           expression
                           ":"
                           expression))
  constant_expression (:choice
                       constant_primary
                       (:prec-left 36
                        (:seq unary_operator (:repeat attribute_instance) constant_primary))
                       (:prec-left 33
                        (:seq
                         constant_expression
                         (:choice "+" "-")
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 34
                        (:seq
                         constant_expression
                         (:choice "*" "/" "%")
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 30
                        (:seq
                         constant_expression
                         (:choice "==" "!=" "===" "!==" "==?" "!=?")
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 25
                        (:seq
                         constant_expression
                         "&&"
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 24
                        (:seq
                         constant_expression
                         "||"
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 35
                        (:seq
                         constant_expression
                         "**"
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 31
                        (:seq
                         constant_expression
                         (:choice "<" "<=" ">" ">=")
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 29
                        (:seq
                         constant_expression
                         "&"
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 27
                        (:seq
                         constant_expression
                         "|"
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 28
                        (:seq
                         constant_expression
                         (:choice "^" "^~" "~^")
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 32
                        (:seq
                         constant_expression
                         (:choice ">>" "<<" ">>>" "<<<")
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-left 22
                        (:seq
                         constant_expression
                         (:choice "->" "<->")
                         (:repeat attribute_instance)
                         constant_expression))
                       (:prec-right 23
                        (:seq
                         constant_expression
                         "?"
                         (:repeat attribute_instance)
                         constant_expression
                         ":"
                         constant_expression)))
  constant_mintypmax_expression (:seq
                                 constant_expression
                                 (:choice
                                  (:prec-left 0
                                   (:seq ":" constant_expression ":" constant_expression))
                                  :blank))
  constant_param_expression (:choice constant_mintypmax_expression data_type "$")
  param_expression (:choice mintypmax_expression data_type "$")
  _constant_range_expression (:choice constant_expression _constant_part_select_range)
  _constant_part_select_range (:choice constant_range constant_indexed_range)
  constant_range (:seq constant_expression ":" constant_expression)
  constant_indexed_range (:seq constant_expression (:choice "+:" "-:") constant_expression)
  expression (:choice
              primary
              (:prec-left 36 (:seq unary_operator (:repeat attribute_instance) primary))
              (:prec-left 36 inc_or_dec_expression)
              (:prec-left 37 (:seq "(" operator_assignment ")"))
              (:prec-left 33
               (:seq expression (:choice "+" "-") (:repeat attribute_instance) expression))
              (:prec-left 34
               (:seq expression (:choice "*" "/" "%") (:repeat attribute_instance) expression))
              (:prec-left 30
               (:seq
                expression
                (:choice "==" "!=" "===" "!==" "==?" "!=?")
                (:repeat attribute_instance)
                expression))
              (:prec-left 25 (:seq expression "&&" (:repeat attribute_instance) expression))
              (:prec-left 24 (:seq expression "||" (:repeat attribute_instance) expression))
              (:prec-left 35 (:seq expression "**" (:repeat attribute_instance) expression))
              (:prec-left 31
               (:seq expression (:choice "<" "<=" ">" ">=") (:repeat attribute_instance) expression))
              (:prec-left 29 (:seq expression "&" (:repeat attribute_instance) expression))
              (:prec-left 27 (:seq expression "|" (:repeat attribute_instance) expression))
              (:prec-left 28
               (:seq expression (:choice "^" "^~" "~^") (:repeat attribute_instance) expression))
              (:prec-left 32
               (:seq
                expression
                (:choice ">>" "<<" ">>>" "<<<")
                (:repeat attribute_instance)
                expression))
              (:prec-left 22
               (:seq expression (:choice "->" "<->") (:repeat attribute_instance) expression))
              conditional_expression
              inside_expression
              tagged_union_expression)
  tagged_union_expression (:prec-left 0
                           (:seq "tagged" member_identifier (:choice expression :blank)))
  inside_expression (:prec-left 31 (:seq expression "inside" "{" open_range_list "}"))
  value_range (:choice expression (:seq "[" expression ":" expression "]"))
  mintypmax_expression (:seq
                        expression
                        (:choice (:prec-left 0 (:seq ":" expression ":" expression)) :blank))
  module_path_conditional_expression (:seq
                                      module_path_expression
                                      "?"
                                      (:repeat attribute_instance)
                                      module_path_expression
                                      ":"
                                      module_path_expression)
  module_path_expression (:choice module_path_primary)
  module_path_mintypmax_expression (:seq
                                    module_path_expression
                                    (:choice
                                     (:prec-left 0
                                      (:seq ":" module_path_expression ":" module_path_expression))
                                     :blank))
  _part_select_range (:choice constant_range indexed_range)
  indexed_range (:seq expression (:choice "+:" "-:") constant_expression)
  _genvar_expression constant_expression
  constant_primary (:choice
                    primary_literal
                    (:seq ps_parameter_identifier (:choice constant_select1 :blank))
                    (:seq
                     constant_concatenation
                     (:choice (:prec-left 0 (:seq "[" _constant_range_expression "]")) :blank))
                    (:seq
                     constant_multiple_concatenation
                     (:choice (:prec-left 0 (:seq "[" _constant_range_expression "]")) :blank))
                    (:seq "(" constant_mintypmax_expression ")")
                    type_reference
                    "null")
  module_path_primary (:choice
                       _number
                       _identifier
                       module_path_concatenation
                       module_path_multiple_concatenation
                       function_subroutine_call
                       (:seq "(" module_path_mintypmax_expression ")"))
  primary (:choice
           primary_literal
           (:seq
            (:choice (:choice class_qualifier package_scope) :blank)
            hierarchical_identifier
            (:choice select1 :blank))
           empty_unpacked_array_concatenation
           (:seq concatenation (:choice (:prec-left 0 (:seq "[" range_expression "]")) :blank))
           (:seq
            multiple_concatenation
            (:choice (:prec-left 0 (:seq "[" range_expression "]")) :blank))
           function_subroutine_call
           let_expression
           (:seq "(" mintypmax_expression ")")
           cast
           assignment_pattern_expression
           streaming_concatenation
           sequence_method_call
           "this"
           "$"
           "null")
  class_qualifier (:seq
                   (:choice (:prec-left 0 (:seq "local" "::")) :blank)
                   (:choice (:seq implicit_class_handle ".") class_scope))
  range_expression (:choice expression _part_select_range)
  primary_literal (:choice
                   _number
                   time_literal
                   unbased_unsized_literal
                   string_literal
                   simple_text_macro_usage)
  time_literal (:choice (:seq unsigned_number time_unit) (:seq fixed_point_number time_unit))
  time_unit (:choice "s" "ms" "us" "ns" "ps" "fs")
  string_literal (:seq
                  "\""
                  (:repeat
                   (:choice
                    (:token-immediate (:pattern "[^\\\\\"]+"))
                    (:token-immediate (:seq "\\" (:pattern ".")))
                    (:token-immediate (:seq "\\" "\n"))))
                  "\"")
  implicit_class_handle (:choice
                         (:prec-left 0
                          (:seq "this" (:choice (:prec-left 0 (:seq "." "super")) :blank)))
                         "super")
  bit_select1 (:prec-left 37 (:repeat1 (:seq "[" expression "]")))
  select1 (:choice
           (:prec-left 37
            (:seq
             (:repeat (:prec-left 0 (:seq "." member_identifier (:choice bit_select1 :blank))))
             "."
             member_identifier
             (:choice bit_select1 :blank)
             (:choice (:prec-left 0 (:seq "[" _part_select_range "]")) :blank)))
           (:prec-left 37
            (:seq bit_select1 (:choice (:prec-left 0 (:seq "[" _part_select_range "]")) :blank)))
           (:prec-left 37 (:seq (:seq "[" _part_select_range "]"))))
  nonrange_select1 (:choice
                    (:prec-left 37
                     (:seq
                      (:repeat
                       (:prec-left 0 (:seq "." member_identifier (:choice bit_select1 :blank))))
                      "."
                      member_identifier
                      (:choice bit_select1 :blank)))
                    bit_select1)
  constant_bit_select1 (:repeat1 (:prec-left 37 (:seq "[" constant_expression "]")))
  constant_select1 (:choice
                    (:seq
                     "["
                     (:repeat (:prec-left 0 (:seq constant_expression "]" "[")))
                     (:choice constant_expression _constant_part_select_range)
                     "]"))
  constant_cast (:seq casting_type "'" "(" constant_expression ")")
  _constant_let_expression let_expression
  cast (:seq casting_type "'" "(" expression ")")
  net_lvalue (:choice
              (:seq ps_or_hierarchical_net_identifier (:choice constant_select1 :blank))
              (:prec-left 20
               (:seq
                "{"
                (:prec-left 0 (:seq net_lvalue (:repeat (:prec-left 0 (:seq "," net_lvalue)))))
                "}"))
              (:seq
               (:choice _assignment_pattern_expression_type :blank)
               assignment_pattern_net_lvalue))
  variable_lvalue (:choice
                   (:prec-left 37
                    (:seq
                     (:choice (:choice (:seq implicit_class_handle ".") package_scope) :blank)
                     _hierarchical_variable_identifier
                     (:choice select1 :blank)))
                   (:prec-left 20
                    (:seq
                     "{"
                     (:prec-left 0
                      (:seq variable_lvalue (:repeat (:prec-left 0 (:seq "," variable_lvalue)))))
                     "}"))
                   (:prec-left 21
                    (:seq
                     (:choice _assignment_pattern_expression_type :blank)
                     assignment_pattern_variable_lvalue))
                   streaming_concatenation)
  nonrange_variable_lvalue (:prec-left 37
                            (:seq
                             (:choice
                              (:choice (:seq implicit_class_handle ".") package_scope)
                              :blank)
                             _hierarchical_variable_identifier
                             (:choice nonrange_select1 :blank)))
  unary_operator (:choice "+" "-" "!" "~" "&" "~&" "|" "~|" "^" "~^" "^~")
  inc_or_dec_operator (:choice "++" "--")
  _number (:choice integral_number real_number)
  integral_number (:choice decimal_number octal_number binary_number hex_number)
  decimal_number (:choice
                  unsigned_number
                  (:token
                   (:seq
                    (:choice
                     (:prec-left 0 (:seq (:pattern "[1-9][0-9_]*") (:pattern "\\s*")))
                     :blank)
                    (:pattern "'[sS]?[dD]")
                    (:pattern "\\s*")
                    (:pattern "[0-9][0-9_]*")))
                  (:token
                   (:seq
                    (:choice
                     (:prec-left 0 (:seq (:pattern "[1-9][0-9_]*") (:pattern "\\s*")))
                     :blank)
                    (:pattern "'[sS]?[dD]")
                    (:pattern "\\s*")
                    (:pattern "[xXzZ?][_]*"))))
  binary_number (:token
                 (:seq
                  (:choice (:prec-left 0 (:seq (:pattern "[1-9][0-9_]*") (:pattern "\\s*"))) :blank)
                  (:pattern "'[sS]?[bB]")
                  (:pattern "\\s*")
                  (:pattern "[01xXzZ?][01xXzZ?_]*")))
  octal_number (:token
                (:seq
                 (:choice (:prec-left 0 (:seq (:pattern "[1-9][0-9_]*") (:pattern "\\s*"))) :blank)
                 (:pattern "'[sS]?[oO]")
                 (:pattern "\\s*")
                 (:pattern "[0-7xXzZ?][0-7xXzZ?_]*")))
  hex_number (:token
              (:seq
               (:choice (:prec-left 0 (:seq (:pattern "[1-9][0-9_]*") (:pattern "\\s*"))) :blank)
               (:pattern "'[sS]?[hH]")
               (:pattern "\\s*")
               (:pattern "[0-9a-fA-FxXzZ?][0-9a-fA-FxXzZ?_]*")))
  non_zero_unsigned_number (:token (:pattern "[1-9][0-9_]*"))
  real_number (:choice
               fixed_point_number
               (:token (:pattern "[0-9][0-9_]*(\\.[0-9][0-9_]*)?[eE][+-]?[0-9][0-9_]*")))
  fixed_point_number (:token (:pattern "[0-9][0-9_]*\\.[0-9][0-9_]*"))
  unsigned_number (:token (:pattern "[0-9][0-9_]*"))
  unbased_unsized_literal (:choice "'0" "'1" (:pattern "'[xXzZ]"))
  attribute_instance (:seq
                      "(*"
                      (:prec-left 0 (:seq attr_spec (:repeat (:prec-left 0 (:seq "," attr_spec)))))
                      "*)")
  attr_spec (:seq _attr_name (:choice (:prec-left 0 (:seq "=" constant_expression)) :blank))
  _attr_name _identifier
  comment (:token
           (:choice
            (:seq "//" (:pattern ".*"))
            (:seq "/*" (:pattern "[^*]*\\*+([^/*][^*]*\\*+)*") "/")))
  _array_identifier _identifier
  _block_identifier _identifier
  _bin_identifier _identifier
  c_identifier (:pattern "[a-zA-Z_][a-zA-Z0-9_]*")
  cell_identifier (:alias _identifier cell_identifier)
  checker_identifier (:alias _identifier checker_identifier)
  class_identifier (:alias _identifier class_identifier)
  class_variable_identifier _variable_identifier
  clocking_identifier (:alias _identifier clocking_identifier)
  config_identifier (:alias _identifier config_identifier)
  const_identifier (:alias _identifier const_identifier)
  constraint_identifier (:alias _identifier constraint_identifier)
  covergroup_identifier (:alias _identifier covergroup_identifier)
  cover_point_identifier (:alias _identifier cover_point_identifier)
  cross_identifier (:alias _identifier cross_identifier)
  dynamic_array_variable_identifier (:alias _variable_identifier dynamic_array_variable_identifier)
  enum_identifier (:alias _identifier enum_identifier)
  escaped_identifier (:seq "\\" (:pattern "[^\\s]*"))
  formal_identifier (:alias _identifier formal_identifier)
  formal_port_identifier (:alias _identifier formal_port_identifier)
  function_identifier (:alias _identifier function_identifier)
  generate_block_identifier (:alias _identifier generate_block_identifier)
  genvar_identifier (:alias _identifier genvar_identifier)
  _hierarchical_array_identifier hierarchical_identifier
  _hierarchical_block_identifier hierarchical_identifier
  _hierarchical_event_identifier hierarchical_identifier
  hierarchical_identifier (:prec-left 0
                           (:seq
                            (:choice (:prec-left 0 (:seq "$root" ".")) :blank)
                            (:repeat
                             (:prec-left 0
                              (:seq _identifier (:choice constant_bit_select1 :blank) ".")))
                            _identifier))
  _hierarchical_net_identifier hierarchical_identifier
  _hierarchical_parameter_identifier hierarchical_identifier
  _hierarchical_property_identifier hierarchical_identifier
  _hierarchical_sequence_identifier hierarchical_identifier
  _hierarchical_task_identifier hierarchical_identifier
  _hierarchical_tf_identifier hierarchical_identifier
  _hierarchical_variable_identifier hierarchical_identifier
  _identifier (:choice simple_identifier escaped_identifier)
  index_variable_identifier (:alias _identifier index_variable_identifier)
  interface_identifier (:alias _identifier interface_identifier)
  interface_instance_identifier (:alias _identifier interface_instance_identifier)
  inout_port_identifier (:alias _identifier inout_port_identifier)
  input_port_identifier (:alias _identifier input_port_identifier)
  instance_identifier (:alias _identifier instance_identifier)
  library_identifier (:alias _identifier library_identifier)
  member_identifier (:alias _identifier member_identifier)
  method_identifier (:alias _identifier method_identifier)
  modport_identifier (:alias _identifier modport_identifier)
  _module_identifier _identifier
  _net_identifier _identifier
  _net_type_identifier _identifier
  output_port_identifier (:alias _identifier output_port_identifier)
  package_identifier (:alias _identifier package_identifier)
  package_scope (:choice (:seq package_identifier "::") (:seq "$unit" "::"))
  parameter_identifier (:alias _identifier parameter_identifier)
  port_identifier (:alias _identifier port_identifier)
  production_identifier (:alias _identifier production_identifier)
  program_identifier (:alias _identifier program_identifier)
  property_identifier (:alias _identifier property_identifier)
  ps_class_identifier (:seq (:choice package_scope :blank) class_identifier)
  ps_covergroup_identifier (:seq (:choice package_scope :blank) covergroup_identifier)
  ps_checker_identifier (:seq (:choice package_scope :blank) checker_identifier)
  ps_identifier (:seq (:choice package_scope :blank) _identifier)
  ps_or_hierarchical_array_identifier (:seq
                                       (:choice
                                        (:choice
                                         (:seq implicit_class_handle ".")
                                         class_scope
                                         package_scope)
                                        :blank)
                                       _hierarchical_array_identifier)
  ps_or_hierarchical_net_identifier (:choice
                                     (:prec-left 37
                                      (:seq (:choice package_scope :blank) _net_identifier))
                                     _hierarchical_net_identifier)
  ps_or_hierarchical_property_identifier (:choice
                                          (:seq (:choice package_scope :blank) property_identifier)
                                          _hierarchical_property_identifier)
  ps_or_hierarchical_sequence_identifier (:choice
                                          (:seq (:choice package_scope :blank) _sequence_identifier)
                                          _hierarchical_sequence_identifier)
  ps_or_hierarchical_tf_identifier (:choice
                                    (:seq (:choice package_scope :blank) tf_identifier)
                                    _hierarchical_tf_identifier)
  ps_parameter_identifier (:choice
                           (:seq
                            (:choice (:choice package_scope class_scope) :blank)
                            parameter_identifier)
                           (:seq
                            (:repeat
                             (:prec-left 0
                              (:seq
                               generate_block_identifier
                               (:choice (:prec-left 0 (:seq "[" constant_expression "]")) :blank)
                               ".")))
                            parameter_identifier))
  ps_type_identifier (:seq
                      (:choice (:choice (:seq "local" "::") package_scope class_scope) :blank)
                      _type_identifier)
  _sequence_identifier _identifier
  _signal_identifier _identifier
  simple_identifier (:pattern "[a-zA-Z_][a-zA-Z0-9_$]*")
  specparam_identifier (:alias _identifier specparam_identifier)
  system_tf_identifier (:pattern "\\$[a-zA-Z0-9_$]+")
  task_identifier (:alias _identifier task_identifier)
  tf_identifier (:alias _identifier tf_identifier)
  terminal_identifier (:alias _identifier terminal_identifier)
  topmodule_identifier (:alias _identifier topmodule_identifier)
  _type_identifier _identifier
  _udp_identifier _identifier
  _variable_identifier _identifier}}
