; smart-indentation follow-up. See c-indents.scm's own header comment for the
; general convention. Checked against tree-sitter-bash's own node-types.json
; plus a real parse dump -- bash's if_statement/case_statement have no
; separate body-wrapper node the way for/while's own "do_group" is (their
; commands are direct children, with "then"/"fi"/"in"/"esac" as anonymous
; sibling tokens), so if_statement/case_statement are captured directly
; rather than a nonexistent body node. elif_clause/else_clause mirror
; Python's own elif_clause/else_clause exactly (Editor/Indent.cpp's
; self-exclusion walk handles the "these aren't themselves @indent-captured,
; but their own row happens to matter" case generically).
(compound_statement) @indent
(do_group) @indent
(subshell) @indent
(if_statement) @indent
(case_statement) @indent
(array) @indent

(elif_clause) @dedent
(else_clause) @dedent
(compound_statement "}" @dedent)
(do_group "done" @dedent)
(subshell ")" @dedent)
(if_statement "fi" @dedent)
(case_statement "esac" @dedent)
(array ")" @dedent)
