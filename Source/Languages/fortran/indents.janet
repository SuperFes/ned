# ned-authored. Every Fortran construct is a header statement, a body and a
# named end statement, none of which the imprint can pair: the body is the
# construct's lines after its header (@indent.headed) and the end statement
# returns to the header's level.
[(program) (module) (submodule) (interface) (block_data) (subroutine) (module_procedure) (function)
 (derived_type_definition) (do_loop) (select_case_statement) (select_type_statement) (select_rank_statement)
 (block_construct) (associate_statement) (enum) (enumeration_type) (coarray_team_statement)
 (coarray_critical_statement)] @indent.headed

# if/where/forall also have a one-line form, which has no body to open.
(if_statement "then") @indent.headed
(where_statement (end_where_statement)) @indent.headed
(forall_statement (end_forall_statement)) @indent.headed

[(end_program_statement) (end_module_statement) (end_submodule_statement) (end_interface_statement)
 (end_block_data_statement) (end_subroutine_statement) (end_module_procedure_statement)
 (end_function_statement) (end_type_statement) (end_do_loop_statement) (end_if_statement)
 (end_where_statement) (end_forall_statement) (end_select_statement) (end_block_construct_statement)
 (end_associate_statement) (end_enum_statement) (end_enumeration_type_statement)
 (end_coarray_team_statement) (end_coarray_critical_statement)] @dedent

# Clauses sit at their construct's level; `case` with its `select` (fprettify).
[(elseif_clause) (else_clause) (elsewhere_clause) (case_statement) (type_statement) (rank_statement)] @dedent

# `contains` returns to the unit's own level; the procedures after it go one in.
(_ (internal_procedures) @indent.end) @indent.headed
(internal_procedures) @indent.headed
