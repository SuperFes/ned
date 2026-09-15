# configurable-formatter-rules follow-up: the fifteenth language, and an
# even more MINIMAL partial case than bash's own original shape --
# verified live against a real sexp dump (every construct below, plus a
# comprehensive set of hazard probes) before writing any query, same
# discipline as every prior language.
#
# `if`/`while`/`for`/`switch`/`function` bodies have NO capturable
# brace.function/brace.control at all -- a real structural absence, not a
# scope cut, and a DIFFERENT reason than bash's own do/then (which at
# least HAD a movable "do"/"then" keyword token to pair on): fish's
# grammar rule for each of these ends its own header directly in a
# `_terminator` (the SAME token -- ";"/newline -- that already separates
# any two ordinary statements), with no SEPARATE opening keyword sitting
# between the condition/header and the body at all (confirmed against
# `grammar.json`'s own rules, not node-types.json's field summary alone).
# There is nothing here for `:placement` to move: the terminator IS the
# boundary, not a repositionable delimiter the way bash's "do"/Lua's
# "then" are, and turning ";" into a newline (or back) is a different
# KIND of edit than this mechanism was built for (it repositions
# whitespace around a token, never rewrites the token's own text).

# brace.control: `begin_statement` is the ONLY construct in this whole
# grammar with a real, capturable delimiter pair -- and it has TWO
# distinct syntactic forms sharing the identical node type, discriminated
# here by which literal token is actually present rather than a `:match?`
# text predicate (unlike Kotlin's own ambiguity, which had no anonymous
# token to distinguish the two shapes by at all): `begin ... end`, and a
# `{ ... }` alternate form (confirmed live as real, executable fish
# syntax, not merely something the grammar tolerates).
(begin_statement "begin" @brace.control.open "end" @brace.control.close)
(begin_statement "{" @brace.control.open "}" @brace.control.close)

# collapse-simple follow-up: begin_statement's own NODE SPAN starts
# exactly at "begin"/"{" and ends exactly at "end"/"}" for each respective
# form (verified against `grammar.json`'s own rule -- unlike bash's
# if_statement/case_statement, whose own span starts at "if"/"case", NOT
# "then"/"in"), so a `.` anchor combined with the SAME literal tokens the
# base capture requires gives a marker whose byte range matches the
# paired capture's own synthesized range exactly -- the same real
# asymmetry bash's own do_group has, for the identical underlying reason.
(begin_statement "begin" . (_) . "end") @brace.control.simple
(begin_statement "{" . (_) . "}") @brace.control.simple

# **A real, GUARDED :placement hazard, the same shape bash's own do/then
# turned out to have, for the SAME underlying reason**: `begin_statement`
# is a bare, standalone statement (no header/condition of its own), so a
# `:placement` edit touches the gap between it and whatever PRECEDES it
# -- typically a real, terminator-ending previous statement. Confirmed
# live with a real fish RUN: "echo hi begin ... end" (same-line's own
# plain-space gap, swallowing the PRECEDING statement's own terminator)
# reads "begin" as an ARGUMENT to "echo" rather than starting a new
# block, leaving a dangling "end" ("'end' outside of a block"). Guarded
# by `Editor/FormatBracePlacement.h`'s own `PlacementUnsafeForLanguage`,
# extended for "fish"/"brace.control" the same way it already was for
# "bash"/"brace.control".
#
# **A SECOND real hazard, a GENERAL one this time, not fish-specific**:
# unlike bash's own "do"/"then" (which need a terminator immediately
# after them, an unrelated lexical fact), fish's "begin" does NOT need an
# immediate terminator -- "begin echo hi;end" runs fine. But ":within
# false" removing the space right after "begin" would still glue it
# directly onto a following WORD ("beginecho"), fusing two identifiers
# into one and leaving a dangling "end" again -- confirmed live. This is
# the exact same hazard collapse-empty's own glue logic already guards
# (Lua's "do"+"end" -> "doend"), just reached from :within instead of
# :collapse-empty -- fixed generally in `Editor/FormatSpacing.h` (a
# shared `IsWordByte` check per gap, not a per-language special case) so
# ANY future keyword-delimited language's own open-side word-fusion risk
# is already covered, not just fish's. The "{" form has no such risk
# ("{" is not a word byte -- "{echo hi;}" runs fine, confirmed live).

# blank-lines-kind follow-up: def.toplevel, the same name every prior
# language's own file carries. A nested function definition is
# deliberately NOT captured as its own def.toplevel -- fish genuinely
# allows nesting (verified live), and this is the same "direct child of
# the container" rule every prior language's def.toplevel already
# follows, no new precedent needed. No def.method at all -- fish has no
# type/class concept for a "method" to belong to, matching bash/Go's own
# precedent.
(program (function_definition) @def.toplevel)
(program . (function_definition) @def.toplevel.first)
