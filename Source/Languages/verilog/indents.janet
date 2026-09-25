# ned-authored. The imprint pairs begin/end, case/endcase, class/endclass and
# the rest; these three open with a named header node (`module_header`) or
# close inside one (`function_body_declaration`), so neither end is a keyword
# of the construct's own. Headed: the body starts on the line after the header.
(module_declaration "endmodule") @indent.headed
(module_declaration "endmodule" @dedent)
(function_declaration) @indent.headed
(function_body_declaration "endfunction" @dedent)
(task_declaration) @indent.headed
(task_body_declaration "endtask" @dedent)

# Continuation lines -- see c-indents.scm.
[(net_assignment) (blocking_assignment) (nonblocking_assignment)] @indent.continuation
