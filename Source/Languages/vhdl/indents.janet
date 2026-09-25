# ned-authored. VHDL's bodies are named nodes starting at their keyword (`is`,
# `begin`, `loop`, `generate`), so the lines after that keyword's row sit one
# level in, and each unit's `end ...` is a sibling after the body. A process's
# declarations without `is` start on their own line, which @indent covers too.
[(entity_head) (entity_body) (architecture_head) (concurrent_block) (sequential_block) (process_head)
 (subprogram_head) (package_declaration_body) (package_definition_body) (component_body) (block_head)
 (generate_body) (case_body) (loop_body) (record_type_definition) (protected_type_declaration)
 (protected_type_body)] @indent

# Branches and case alternatives: the lines after their own first row.
[(if_statement) (elsif_statement) (else_statement) (case_statement_alternative)] @indent.headed

# These keep their `end ...` inside themselves.
(record_type_definition (end_record) @dedent)
(protected_type_declaration (protected_type_declaration_end) @dedent)
(protected_type_body (protected_type_body_end) @dedent)

# `port (` ... `);`: the closer is followed by the clause's own `;`, which
# the imprint does not read as a bracket body.
[(generic_clause) (port_clause)] @indent.headed
(generic_clause ")" @dedent)
(port_clause ")" @dedent)

# Continuation lines -- see c-indents.scm. A selected or conditional
# assignment's later choices sit a level in.
[(concurrent_selected_signal_assignment) (concurrent_conditional_signal_assignment)
 (concurrent_simple_signal_assignment) (simple_waveform_assignment) (simple_variable_assignment)
 (conditional_signal_assignment) (selected_waveform_assignment) (selected_variable_assignment)] @indent.continuation
