; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention and for what the delimiter imprint contributes without a
; capture, and bash-indents.scm's own comment on keyword-delimited bodies --
; fish is the purer case: every block construct (function/if/for/while/
; begin/switch) is `keyword ... end` with its commands as direct children,
; and every one of them indents from the imprint with nothing written here.
; Only the clause headers remain: else_if_clause/else_clause are siblings of
; the commands they follow and align back to the `if` line. Checked against
; tree-sitter-fish's own node-types.json plus a real parse dump.
(else_if_clause) @dedent
(else_clause) @dedent
