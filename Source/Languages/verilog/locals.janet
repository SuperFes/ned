#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; Only what nothing outside can name binds here: a function's or task's
#; own variables, a block's inside one, and a `for` loop's variable. A
#; module's nets and variables are reachable by hierarchical name from
#; another file (a testbench's `dut.count`), and a function's arguments by
#; name at a call (`f(.n(1))`), so both stay unbound and decline. A member
#; (`s.total`) and a named argument or port are no variables.

(function_body_declaration) @local.scope
(task_body_declaration) @local.scope
((seq_block) @local.scope
 (:has-ancestor? @local.scope function_body_declaration))
((seq_block) @local.scope
 (:has-ancestor? @local.scope task_body_declaration))
(loop_statement) @local.scope

(block_item_declaration
  (data_declaration
    (list_of_variable_decl_assignments
      (variable_decl_assignment . (simple_identifier) @local.definition.var))))
(for_variable_declaration (simple_identifier) @local.definition.var)

(simple_identifier) @local.reference

(member_identifier (simple_identifier) @local.skip)
(let_list_of_arguments (simple_identifier) @local.skip)
(list_of_arguments (simple_identifier) @local.skip)
(named_port_connection (port_identifier (simple_identifier) @local.skip))
