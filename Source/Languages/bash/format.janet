# configurable-formatter-rules follow-up: the thirteenth language --
# verified live that `function_definition` is the ONLY construct in this
# grammar whose own node span starts and ends with a genuine SINGLE-byte
# delimiter (all three syntactic forms -- "f() { }", "function g { }",
# "function h() { }" -- produce the identical
# `function_definition body: (compound_statement)` node). Every other
# brace-placement-shaped construct here (if/while/for/case) is delimited
# by KEYWORD tokens (then/fi, do/done, in/esac) rather than a single
# character -- this file originally shipped with only `brace.function`/
# `def.toplevel` for exactly that reason, the same limitation the Lua
# rollout hit and fixed (`FormatCapture::openLength`/`closeLength`,
# `Editor/Mode.cpp`'s paired-capture correlation, `Editor/
# FormatBracePlacement.h`/`FormatSpacing.h` reading the real delimiter
# token instead of a hardcoded single byte). Revisited once that
# generalization existed and proved out on Lua -- this file now carries
# the full `brace.control`/`control.parens` set too, using the exact same
# paired `.open`/`.close` mechanism, verified live against a real sexp
# dump for every construct below before writing any query.
#
# **A real, Go-ASI-class :placement hazard, found live and guarded in
# C++, not just documented**: unlike every brace-carrying language in this
# template, a `:same-line` placement is what's DANGEROUS for brace.control
# here (every OTHER placement is safe) -- confirmed live with a real
# `bash -n`: "while true do"/"if true then" (same-line's own plain-space
# gap) are hard syntax errors, since `do`/`then` are bash reserved words
# requiring a real statement TERMINATOR (a semicolon or a newline) before
# them, never merely whitespace. A newline-based gap (`:next-line`/
# `:next-line-indented`) IS a valid terminator, so those stay safe.
# `Editor/FormatBracePlacement.h`'s own `PlacementUnsafeForLanguage` now
# takes the capture NAME too (previously language-only, Go's own guard
# never needed one) and declines `:same-line` for `brace.control`
# specifically -- `brace.function` is unaffected (bash's function bodies
# are real braces, needing no terminator at all, confirmed live
# `:same-line` works fine there). The guard is deliberately a blanket
# decline for the whole capture NAME rather than per-construct: `in`
# (case_statement) and a C-style for-loop's own `do` both have an
# OPTIONAL terminator in the grammar and are actually safe for
# `:same-line` (confirmed live), but brace.control has no per-instance
# signal available at this layer to tell them apart from while/until/
# for/if's own genuinely unsafe shapes -- declining the safe cases too is
# the same "decline rather than risk corruption" tradeoff Go's own guard
# already accepts.
(function_definition body: (compound_statement) @brace.function)

# collapse-simple follow-up: compound_statement wraps its own commands
# DIRECTLY (verified live, no intermediate wrapper the way go's `block`
# has), so the "." anchor sits right against it the same as cpp/
# javascript/java/rust/c rather than go's one-level-deeper anchor.
(function_definition body: (compound_statement . (_) .) @brace.function.simple)

# brace.control: while/until (the SAME `while_statement` node type,
# differing only in which leading keyword literal matched -- verified
# live), for/select (SAME `for_statement` node type, same reasoning), and
# a C-style `for ((;;))`'s own do/done alternative ALL delegate their body
# to a shared `do_group` node ("do" (statement)? "done"), captured ONCE
# here rather than once per statement kind that can contain one. `do_group`
# is the one keyword-delimited construct in this whole file whose OWN node
# span starts exactly at "do" and ends exactly at "done" (verified against
# `grammar.json`'s own rule, not assumed) -- which is what lets it ALSO
# carry a `.simple` marker below, a real asymmetry from if/case (neither of
# which starts its own span at the keyword this file pairs on: if_statement
# starts at "if", case_statement at "case", not "then"/"in").
(do_group "do" @brace.control.open "done" @brace.control.close)
(do_group . (_) .) @brace.control.simple

# :collapse-empty follow-up, a real bash-specific fact worth recording
# rather than assumed to carry over from Lua's own precedent: a truly
# empty do/done body ("do done", nothing between) is a hard SYNTAX ERROR
# in real bash, confirmed live with `bash -n` -- unlike Lua's `do end`,
# which is perfectly valid. The same holds for then/fi (`if x; then fi`),
# a bare subshell (`( )`), and the standalone group command (`{ }`) below
# -- all four confirmed live the same way. So :collapse-empty's own
# `isEmpty` check can only ever become true on these four capture shapes
# starting from source that was ALREADY invalid bash before this pass
# ever ran -- neither direction of :collapse-empty can make such input
# more or less broken, so the feature is real but practically inert for
# them on any file that was valid to begin with, not a hazard needing a
# Go-ASI-style guard (Go's own danger was a placement rule breaking
# PREVIOUSLY VALID code; nothing here can do that). case/esac is the one
# genuine exception -- `case $x in esac` (zero case_items) IS valid bash,
# confirmed live, so :collapse-empty is a real, reachable, useful lever
# there specifically.

# if/elif/else/fi: paired on the OUTER if_statement's own "then"/"fi" --
# spans the whole chain even when elif/else clauses sit in between
# (verified live: there is only ever one literal "fi" token, closing every
# branch, not one per branch -- the exact same shape lua/format.janet's own
# if/then/end pairing already established, `elif_clause`/`else_clause`
# themselves get no capture of their own for the identical reason Lua's
# elseif/else branches don't: neither has a delimiter pair naming just its
# own body). No `.simple` marker -- if_statement's own span starts at "if",
# not "then", so there is no single node whose span matches this paired
# capture's synthesized range exactly.
(if_statement "then" @brace.control.open "fi" @brace.control.close)

# case/esac: paired on case_statement's own "in"/"esac" -- same "no single
# node spans it" reasoning as if/then/fi (case_statement's own span starts
# at "case"), so no `.simple` marker here either.
(case_statement "in" @brace.control.open "esac" @brace.control.close)

# A C-style for-loop's body can ALSO be a genuine brace group instead of
# do/done (`for ((i=0;i<10;i++)) { echo $i; }`) -- confirmed this is real,
# accepted bash syntax via a live `bash -n`/execution check, not merely
# something the grammar happens to parse. Single-byte "{"/"}" delimiters,
# so this is a plain whole-node capture (no pairing needed) exactly like
# brace.function's own compound_statement -- a SECOND, independent
# brace-carrying shape in this grammar, not a variant of the first.
(c_style_for_statement body: (compound_statement) @brace.control)
(c_style_for_statement body: (compound_statement . (_) .) @brace.control.simple)

# subshell "(...)" and a bare standalone group command "{ ...; }" (real,
# distinct bash constructs -- confirmed live `_statement_not_subshell`
# lists `compound_statement` as an ordinary bare statement alongside
# `subshell`, not just as a function's own body or the C-style for-loop
# alternative above). subshell's own node span starts at "(" and ends at
# ")" -- both single-byte, so (unlike do_group/if/case above) this is a
# plain whole-node capture too, with a `.simple` marker for the same
# reason. The bare group command needs an EXPLICIT exclusion of the two
# other compound_statement contexts already captured above (function
# bodies, the C-style for-loop's own alternative body) -- without it, the
# SAME node would be captured twice under different names at the identical
# byte range, and a project configuring DIFFERENT :break rules for
# brace.function/brace.control would then compute two conflicting edits at
# the same position. `:not-has-parent?` (Editor/Grammar/QueryPredicates.cpp,
# already used elsewhere in this codebase for capture filtering) checks the
# IMMEDIATE parent only -- exactly right here, since both excluded contexts
# are compound_statement's own direct parent, and a group command legitimately
# nested INSIDE a function body (not AS its body) must still be captured.
(subshell) @brace.control
(subshell . (_) .) @brace.control.simple
((compound_statement) @brace.control
  (:not-has-parent? @brace.control "function_definition")
  (:not-has-parent? @brace.control "c_style_for_statement"))
((compound_statement . (_) .) @brace.control.simple
  (:not-has-parent? @brace.control.simple "function_definition")
  (:not-has-parent? @brace.control.simple "c_style_for_statement"))

# control.parens: if/while/until's own condition is a `test_command` --
# `[ expr ]` or `[[ expr ]]` (verified live both parse to the same node
# type, differing only in bracket width: 1 byte vs. 2, both handled
# correctly by the same openLength/closeLength generalization). Unlike
# every prior language's own control.parens (a REDUNDANT, optional paren
# lever over a bare expression field), bash's test brackets are the
# MANDATORY condition syntax itself -- the same "mandatory yet still needs
# pairing" shape C#'s own condition parens turned out to have
# (csharp/format.janet's own comment), for an unrelated grammar reason
# (C#'s condition field is a bare expression with anonymous paren tokens;
# bash's condition IS the bracket-delimited test_command node itself, with
# no wrapping needed at all -- both still route through the SAME paired
# mechanism since `[[` isn't 1 byte).
#
# **A real, verified :within=false hazard, unique to `[[`/`]]` among
# every :within lever this whole template has shipped**: `[[` and `]]`
# are bash RESERVED WORDS requiring whitespace separation from their own
# content, unlike single `[`/`]` (an ordinary command name/argument, no
# such requirement) or a C-style for-loop's own `((`/`))` (unambiguous
# against adjacent non-identifier bytes either way, confirmed live it
# needs no separating space at all). Confirmed live with a real `bash`
# run, not assumed: `[[-f x]]` fails as `[[-f: command not found` (the
# WHOLE "[[-f" reads as one word), and `[[ -f x]]` (space missing on the
# CLOSE side only) fails to parse at all. So a project configuring
# `:within false` for `bash/control.parens` -- a reasonable-sounding
# "no space just inside the delimiter" style choice, the same one a
# project might make for ordinary parens -- would silently corrupt every
# `[[ ... ]]` test in the file, while leaving `[ ... ]`/`(( ... ))` fine.
# Deliberately left unguarded in C++ (unlike Go's own ASI hazard,
# `FormatBracePlacement.cpp`'s `PlacementUnsafeForLanguage`) -- Go's
# danger fires from the MOST common brace-placement styles applied
# uniformly with no escape valve; this one requires a specific, far less
# common "compact test brackets" preference most shell style guides
# actively advise against, and unlike Go's single language+placement
# check, discriminating it would need inspecting each capture's own
# TEXT (`[[` vs `[`/`((`), not just its language and rule -- judged not
# worth the added complexity in a shared, generic function until a real
# project hits it. Documented here instead, the same "verify and record,
# don't silently smooth over" standard C#'s own catch_declaration finding
# and PHP's own colon-syntax indent gap already set.
#
# A C-style for-loop's own "((;;))"
# clause is captured the same way every C-family language's own for-loop
# clause is (cpp/javascript/java/rust's own for-loop parens precedent) --
# its own node span starts at "for", not "((", so pairing is required
# here too, not optional.
(test_command "[" @control.parens.open "]" @control.parens.close)
(test_command "[[" @control.parens.open "]]" @control.parens.close)
(c_style_for_statement "((" @control.parens.open "))" @control.parens.close)

# blank-lines-kind follow-up: def.toplevel, the same name every prior
# language's file carries. A nested function definition ("f() { g() { }
# }") is deliberately NOT captured as its own def.toplevel/def.method --
# bash has no type/class concept for a "method" to belong to, and
# capturing arbitrary function nesting the way Python declines to for its
# own nested defs is the same precedent, not a new one.
(program (function_definition) @def.toplevel)
(program . (function_definition) @def.toplevel.first)
