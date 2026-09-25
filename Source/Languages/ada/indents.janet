# ned-authored, GNAT style. Ada's keywords are case-insensitive patterns, so
# the imprint pairs none of them; its bodies are named parts, though.
# Declarations and statements each sit one level in.
[(non_empty_declarative_part) (handled_sequence_of_statements) (variant_list)] @indent
(record_definition (component_list) @indent)

# A construct's own statements directly under it: the lines after its header.
[(package_declaration) (protected_body) (if_statement) (case_statement) (case_statement_alternative) (variant)
 (loop_statement) (selective_accept) (asynchronous_select) (conditional_entry_call) (timed_entry_call)
 (parallel_block_statement)] @indent.headed

# `exception` returns to the construct's own level and its handlers go one in;
# the handler section is no node of its own, so the construct itself holds it.
(handled_sequence_of_statements "exception" @indent.end) @indent
(_ (handled_sequence_of_statements "exception" @indent.begin)) @indent
(exception_handler) @indent.headed

# Clause keywords and `end` at the construct's level.
(subprogram_body "end" @dedent)
(package_body "end" @dedent)
(package_declaration "end" @dedent)
(task_body "end" @dedent)
(protected_body "end" @dedent)
(entry_body "end" @dedent)
(block_statement "end" @dedent)
(if_statement "end" @dedent)
(case_statement "end" @dedent)
(loop_statement "end" @dedent)
(selective_accept "end" @dedent)
(asynchronous_select "end" @dedent)
(conditional_entry_call "end" @dedent)
(timed_entry_call "end" @dedent)
(accept_statement "end" @dedent)
(extended_return_statement "end" @dedent)
(record_definition "end" @dedent)
(variant_part "end" @dedent)
(record_representation_clause "end" @dedent)
(parallel_block_statement "end" @dedent)
(package_declaration "private" @dedent)
(if_statement "else" @dedent)
(elsif_statement_item) @dedent
(selective_accept ["or" "else"] @dedent)
(timed_entry_call "or" @dedent)
(conditional_entry_call "else" @dedent)
(asynchronous_select "then" @dedent)

# A task or protected definition starts at its first entry and owns its
# `private` and `end`: its interior stops at the first of those, and the
# private part is the declaration's own, between them.
(task_definition "private" @indent.end) @indent
(task_definition "end" @indent.end) @indent
(protected_definition "private" @indent.end) @indent
(protected_definition "end" @indent.end) @indent
(_ (task_definition "private" @indent.begin "end" @indent.end)) @indent
(_ (protected_definition "private" @indent.begin "end" @indent.end)) @indent

# Continuation lines -- see c-indents.scm.
[(assignment_statement) (simple_return_statement)] @indent.continuation
