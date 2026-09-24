# ned-authored. `do ... end` comes from the imprint. The clauses that split a
# do block (else/rescue/catch/after) line up with the line that opened it,
# and a `->` clause's body may start on the line after its arrow.
[(else_block) (rescue_block) (catch_block) (after_block)] @dedent
(stab_clause) @indent.headed
