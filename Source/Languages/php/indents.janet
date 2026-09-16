# ROADMAP.md watch-list entry: the colon-alternate `if (...): ... endif;`
# body was never indented at all (plain `indent-buffer`/`ned --format`,
# no format.janet rules involved). Root cause, confirmed against
# grammar.json: PHP's `if_statement` is a CHOICE of two full shapes -- the
# brace form (body: `statement`, which for `{ ... }` resolves to
# `compound_statement`, already a Bracket body in ImprintTables.cpp's
# `kPhp[]`) and the colon form (body: `colon_block`, closed by a literal
# "endif" that belongs to `if_statement` ITSELF, not to `colon_block` --
# `colon_block`'s own grammar rule is just `":" (statement)*`, no closer at
# all). The imprint's per-rule inference has nothing to key off for either
# node in the colon form, so nothing is a container and every line -- body
# AND `elseif`/`else` headers alike -- sits at column 0 relative to `if`.
#
# Fixed the same way ruby/indents.janet's `do`/`method` gap is: condition
# the capture on the literal child that only the colon form has, which is
# also what keeps this from firing on the brace form (whose `if_statement`
# has no "endif" child at all) -- one query covers both shapes with no
# discriminator needed beyond structure. `else_if_clause`/`else_clause` are
# the SAME node type for both forms (colon-form's own `else_if_clause_2`/
# `else_clause_2` grammar rules are `alias()`ed to the ordinary name), so
# their @dedent applies unconditionally -- verified live it is a no-op for
# the brace form, which already resolves those header lines correctly with
# no capture at all (nothing there was ever a container to escape).
#
# switch_statement's own colon form (`switch (...): case 1: ... endswitch;`)
# is NOT covered here. Its body field is always `switch_block` regardless
# of form (the brace-vs-colon CHOICE is inside switch_block's own rule,
# not switch_statement's), and while investigating this fix a SEPARATE,
# pre-existing bug surfaced live: even the already-working BRACE form never
# gives `case`/`default` bodies their own extra level (`case_statement`/
# `default_statement` carry no delimiters at all, unlike Go's exactly
# analogous clauses, so `case 1:\n    echo 1;` and its label land at the
# same column). Fixing colon-form switch properly needs that same
# case/default-body question answered for both forms at once, which is
# more than this fix's scope -- logged in ROADMAP.md's watch list instead
# of folded in here.
(if_statement "endif") @indent
(if_statement "endif" @dedent)
(else_if_clause) @dedent
(else_clause) @dedent

# while_statement/for_statement/foreach_statement have the identical
# colon-alternate shape (`while (...): ... endwhile;`,
# `for (...): ... endfor;`, `foreach (...): ... endforeach;`), each closed
# by a literal belonging to the statement itself rather than to a nested
# body node -- same fix, no clause headers to dedent since none of the
# three has an elseif/else-shaped alternative.
(while_statement "endwhile") @indent
(while_statement "endwhile" @dedent)
(for_statement "endfor") @indent
(for_statement "endfor" @dedent)
(foreach_statement "endforeach") @indent
(foreach_statement "endforeach" @dedent)
