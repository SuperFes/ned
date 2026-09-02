; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention, and bash-indents.scm's own comment for why
; if/for/while/function/begin/switch are captured directly rather than a
; separate body-wrapper node -- fish's own grammar has the same shape (no
; "do_group" equivalent at all; every block construct's body is direct
; children, closed by a single "end" keyword). Checked against
; tree-sitter-fish's own node-types.json plus a real parse dump.
(function_definition) @indent
(if_statement) @indent
(for_statement) @indent
(while_statement) @indent
(begin_statement) @indent
(switch_statement) @indent

(else_if_clause) @dedent
(else_clause) @dedent
(function_definition "end" @dedent)
(if_statement "end" @dedent)
(for_statement "end" @dedent)
(while_statement "end" @dedent)
(begin_statement "end" @dedent)
(switch_statement "end" @dedent)
