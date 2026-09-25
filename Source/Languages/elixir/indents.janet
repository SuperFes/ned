# ned-authored. `do ... end` comes from the imprint. The clauses that split a
# do block (else/rescue/catch/after) line up with the line that opened it,
# and a `->` clause's body may start on the line after its arrow.
[(else_block) (rescue_block) (catch_block) (after_block)] @dedent
(stab_clause) @indent.headed

# An operator's right side on the next line goes a continuation step in;
# a pipeline's stages stay under its first operand, as mix format writes them.
((binary_operator
   operator: _ @_operator) @indent.continuation
 (:not-eq? @_operator "|>"))
