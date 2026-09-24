# ned-authored. The imprint indents keyword bodies up to their `end`; a clause
# continuing one (`elseif`, `else`, `catch`, `finally`) sits back at the
# construct's own level. A module body stays at column zero (Julia's style
# guide), which it already does: the imprint pairs no `module ... end`.
[(elseif_clause) (else_clause) (catch_clause) (finally_clause)] @dedent
