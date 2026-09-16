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
# ROADMAP.md watch-list entry, closed 2026-09-16: neither `switch` form
# ever gave `case`/`default` bodies their own extra level -- confirmed via
# node-types.json, `case_statement`/`default_statement` carry no
# delimiters at all (just an optional trailing statement list, no literal
# of their own), unlike Go's exactly analogous clauses. Capturing the
# whole node is enough with no closer to name: a case's own byte range
# already ends exactly where the next case/default (or switch_block's own
# closer) begins -- tree-sitter siblings are contiguous -- and the walk's
# own self-exclusion (Indent.cpp's `selfOpensHere`) keeps the "case 1:"
# label line itself from being indented by its own capture, the same way
# it already excludes a bracket container's own opening line.
#
# `switch_block`'s colon form (`switch (...): case 1: ... endswitch;`) was
# ALSO unindented altogether -- its brace-vs-colon CHOICE lives inside
# switch_block's own grammar rule (unlike if/while/for/foreach, where the
# CHOICE is in the outer statement), so the pre-existing kPhp[] imprint
# entry -- inferred from the brace form's "{"/"}" -- simply finds no
# opener at all for a colon-form instance and contributes nothing. Same
# conditioned-on-a-literal-child fix as this file's other entries; the
# imprint's own bracket-based entry keeps handling the brace form
# untouched (verified live), since a query capture and the imprint only
# ever contribute ONE count for the same node no matter how many sources
# name it (see Indent.cpp's own doc comment).
(case_statement) @indent
(default_statement) @indent
(switch_block "endswitch") @indent
(switch_block "endswitch" @dedent)

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
