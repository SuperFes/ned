# configurable-formatter-rules follow-up: seventeenth language, and the
# proven `def...end` keyword-delimited path Ruby was flagged as a
# candidate for back when Lua/Bash/Fish's own paired `.open`/`.close`
# mechanism was built. Verified live throughout: `tree-sitter parse`/
# `tree-sitter query` against the real vendored grammar (no ned/kotlinc-
# style toolchain available in this environment -- no `ruby` binary is
# installed here at all, unlike every prior language's own real-compiler
# check; validated instead by re-parsing `ned --format`'s own output with
# ned's own parse engine and confirming zero ERROR/MISSING nodes, the
# same honest structural-only substitute Kotlin used before `kotlinc` was
# installed).
#
# **The one genuinely new wrinkle: Ruby's OWN grammar makes the opening
# keyword OPTIONAL for every single construct in this family** -- unlike
# Lua/Bash/Fish, where a real opening keyword (function/do/then/begin) is
# always present. `if`/`unless`'s own body wrapper (`then`) only carries
# a literal "then" token when the user actually writes one (`if x then`);
# ordinarily `if x\n ... end` has no separate opening token between the
# condition and the body at all -- just an ordinary statement terminator,
# confirmed via `grammar.json`'s own CHOICE rule for `then`, not assumed.
# `while`/`until`'s own body wrapper (`do`) has the identical optionality.
# Consequence, confirmed live with `tree-sitter query` (0 matches without
# the keyword, 1 with it): brace.control for if/unless/while/until is
# real, but only ever fires for the keyword-having form -- the far more
# common keyword-less form is a genuine structural absence (nothing to
# place), not a scope cut, the same "decline rather than approximate"
# call every prior "no distinguishing shape" case in this rollout made.

# brace.function: `method`/`singleton_method` (`def ... end`). Anchored
# on the NAME field rather than the parameter list's own closing paren
# (Lua's own precedent) -- confirmed live that Ruby's parameter list has
# THREE distinct shapes (parenthesized, bare/no-parens, or absent
# entirely for a zero-arg method with no parens at all), all three
# sharing the identical aliased node type "method_parameters", with no
# way to tell a bare list from an absent one structurally. Anchoring on
# the closing paren (like Lua) would need a second pattern for the other
# two shapes, and that second pattern would ALSO match the parenthesized
# case (nothing excludes it), producing two overlapping candidate opens
# for the same method -- a real correctness hazard, not a hypothetical
# one. The name field is present in every non-endless shape uniformly,
# so ONE pattern covers all three parameter styles with no double-match
# risk, at the cost of a minor, documented imprecision: for a
# parenthesized method, the interior collapse-empty/:within logic sees
# the parameter list's own text as part of the "interior", so
# `def f()\nend` (empty body, non-empty-looking interior) won't
# collapse-empty even though its body genuinely is empty. Confirmed live
# this narrows correctly for the common case that matters: a bare/no-arg
# method's own interior is exactly the body, so `def f\nend` DOES
# collapse-empty correctly.
#
# Ruby's "endless method" (`def f = 1`, no "end" at all -- a real
# expression-bodied form, not a rare edge) needs NO discriminator at all,
# unlike Kotlin's identically-shaped ambiguity: confirmed live via
# `tree-sitter query` that requiring a literal "end" token as this
# node's own child is already a real structural fact an endless method's
# own node simply doesn't have, so the pairing naturally produces zero
# matches for it -- no `:match?` predicate needed, a simpler case than
# Kotlin's `function_body` (which is the SAME node type either way and
# offered no structural handle at all).
(method name: (_) @brace.function.open "end" @brace.function.close)
(singleton_method name: (_) @brace.function.open "end" @brace.function.close)

# Anonymous blocks passed to a method call -- Ruby's own closure/lambda
# shape (`{ |x| ... }` and `do |x| ... end`, and `->(x) { ... }`'s own
# body reuses the identical `block` node type, confirmed live), captured
# under the reversed anon-fn policy alongside every other language's own
# closure form. `block`'s own delimiters really are the single-byte "{"/
# "}" (confirmed live), so it's captured directly like cpp's own
# `compound_statement` -- no pairing needed, default openLength/
# closeLength of 1 is already correct, and it gets a real `.simple`
# marker the same way. `do_block` is NOT capturable directly the same
# way despite ALSO being a real single node whose own span already
# starts/ends exactly at its delimiters: a bare `(do_block) @brace.
# function` capture would silently default `openLength`/`closeLength` to
# 1, corrupting every collapse-empty/:within edit onto "d"/"e" alone
# instead of the real "do"(2)/"end"(3) -- caught live by this rollout's
# own apply-and-check discipline, not by inspection, confirmed via a real
# scratch probe dump showing open=1/close=1 where 2/3 were expected.
# Paired the same way begin/while's own multi-byte keywords are instead
# -- since "do"/"end" are do_block's own first/last children, the
# synthesized span is byte-identical to the node's natural one, so the
# `.simple` marker below (still capturing the whole node) correlates
# against it exactly as it would have against a direct capture.
(block) @brace.function
(block body: (block_body . (_) .)) @brace.function.simple
(do_block "do" @brace.function.open "end" @brace.function.close)
(do_block body: (body_statement . (_) .)) @brace.function.simple

# brace.control: if/unless/while/until, ONLY for the keyword-having form
# (see the file-level note above) -- the keyword sits nested one level
# inside the body-wrapper field (`then`/`do`), correlated against the
# statement's own "end" one level up in the same match.
(if consequence: (then "then" @brace.control.open) "end" @brace.control.close)
(unless consequence: (then "then" @brace.control.open) "end" @brace.control.close)
(while body: (do "do" @brace.control.open "end" @brace.control.close))
(until body: (do "do" @brace.control.open "end" @brace.control.close))

# `begin...end` -- Ruby's own try-equivalent. Its OWN body gets
# brace.control (matching every prior language's own "a try block's own
# body, not just catch/finally" precedent from the coverage audit); its
# own "begin" keyword is UNCONDITIONAL (confirmed via `grammar.json` --
# not wrapped in any CHOICE, unlike if/while's own optional then/do), so
# this one pattern covers every begin/end block with no keyword-optional
# caveat at all.
#
# rescue/ensure/else clauses are deliberately declined -- confirmed live
# each shares the ONE enclosing "end" rather than owning a closing token
# of its own (rescue's own body is a `then`-wrapper with the SAME
# then-keyword optionality as if/while, but with no independent "end" to
# pair it against), the identical "no distinguishing shape" reasoning
# already declined Kotlin's catch_block/PHP's per-clause bodies for.
(begin "begin" @brace.control.open "end" @brace.control.close)

# `case`/`case_match` (value-form and pattern-matching-form switch) are
# ALSO declined entirely, a real absence rather than an oversight:
# confirmed via `grammar.json` that case has no opening keyword of its
# own at all analogous to bash's own "case ... IN ... esac" -- "when"/
# "in" belong to each individual clause, not to the case statement as a
# whole, and (like rescue above) no clause owns a closing token of its
# own to pair against either. Only `control.parens` below reaches case's
# own value.

# brace.class: `class` and the singleton-class reopen form
# (`class << self ... end`) fold together the same way struct+class do
# in cpp/PHP/Rust -- anchored on the field that's ALWAYS present before
# the body in each respective shape (name for `class`, value for
# `singleton_class`) rather than chasing an optional superclass, the
# identical "one uniform anchor, minor documented imprecision over a
# rare optional field" call brace.function's own name-anchor makes above.
(class name: (_) @brace.class.open "end" @brace.class.close)
(singleton_class value: (_) @brace.class.open "end" @brace.class.close)

# brace.namespace: `module` -- no optional field between name and body at
# all, so this one is fully precise, no imprecision to document.
(module name: (_) @brace.namespace.open "end" @brace.namespace.close)

# control.parens: if/unless/while/until's own condition and case/
# case_match's own value are ordinary expressions with no required
# parens at all (Python/Go/Lua's own narrow lever) -- confirmed live via
# `tree-sitter parse` that writing them anyway (`if (x)`) produces a real
# `parenthesized_statements` node in the condition/value field, matched
# only when actually present.
(if condition: (parenthesized_statements) @control.parens)
(unless condition: (parenthesized_statements) @control.parens)
(while condition: (parenthesized_statements) @control.parens)
(until condition: (parenthesized_statements) @control.parens)
(case value: (parenthesized_statements) @control.parens)
(case_match value: (parenthesized_statements) @control.parens)

# blank-lines-kind: def.toplevel over method/singleton_method/class/
# module directly inside `program` (confirmed live no intermediate
# wrapper node exists -- `_statements` is a hidden grammar rule, so its
# children inline directly into `program`, unlike Go's own
# `statement_list` wrapper); def.method over method/singleton_method
# directly inside a class/module/singleton_class's own body_statement,
# the identical inlining fact one level down. A method reached only
# through a call argument (Ruby's `private def foo; end` idiom, where
# `def` is structurally an argument to a `private` method call rather
# than a direct child of body_statement) is a real, deliberately
# undetected miss for THESE position-based captures specifically --
# brace.function/brace.control above still capture it correctly
# regardless of how it's called, only the blank-line placement rule
# misses it, the same class of narrow miss Go/PHP's own unusual-wrapping
# cases already accepted elsewhere in this rollout.
(program (method) @def.toplevel)
(program (singleton_method) @def.toplevel)
(program (class) @def.toplevel)
(program (module) @def.toplevel)
(program . (method) @def.toplevel.first)
(program . (singleton_method) @def.toplevel.first)
(program . (class) @def.toplevel.first)
(program . (module) @def.toplevel.first)

(class body: (body_statement (method) @def.method))
(class body: (body_statement (singleton_method) @def.method))
(module body: (body_statement (method) @def.method))
(module body: (body_statement (singleton_method) @def.method))
(singleton_class body: (body_statement (method) @def.method))
(singleton_class body: (body_statement (singleton_method) @def.method))
(class body: (body_statement . (method) @def.method.first))
(class body: (body_statement . (singleton_method) @def.method.first))
(module body: (body_statement . (method) @def.method.first))
(module body: (body_statement . (singleton_method) @def.method.first))
(singleton_class body: (body_statement . (method) @def.method.first))
(singleton_class body: (body_statement . (singleton_method) @def.method.first))
