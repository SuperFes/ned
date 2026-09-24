# ned-authored, the MATLAB editor's defaults: control-flow bodies one level in,
# `case` one level inside its `switch`, class blocks indented, and a function
# body indented only when nested or a method (a file's own top-level function
# body stays at column zero). A construct's lines after its header sit inside
# it up to `end`, so Enter in a body not yet written still indents.
[(if_statement) (for_statement) (while_statement) (try_statement) (switch_statement) (case_clause)
 (otherwise_clause) (class_definition) (properties) (methods) (events) (enumeration)
 (arguments_statement)] @indent.headed
(methods (function_definition) @indent.headed)
(block (function_definition) @indent.headed)

[(elseif_clause) (else_clause) (catch_clause)] @dedent
(if_statement "end" @dedent)
(for_statement "end" @dedent)
(while_statement "end" @dedent)
(try_statement "end" @dedent)
(switch_statement "end" @dedent)
(class_definition "end" @dedent)
(properties "end" @dedent)
(methods "end" @dedent)
(events "end" @dedent)
(enumeration "end" @dedent)
(arguments_statement "end" @dedent)
(function_definition "end" @dedent)
