# configurable-formatter-rules follow-up: the first genuinely KEYWORD-
# delimited language in this template -- every prior language (cpp through
# Bash) either carried real single-character brace/paren delimiters or had
# none at all. Lua's blocks are delimited by multi-byte keyword tokens
# ("do"/"end", "then"/"end") with NO wrapping node of their own (verified
# live against a real parse -- tree-sitter-lua's if/while/for/do bodies are
# a bare "block" field, the keyword tokens sitting OUTSIDE it as direct
# siblings, the exact shape tree-sitter-python's own block field has, minus
# python's total absence of any delimiter at all). Editor/
# FormatBracePlacement.h/FormatSpacing.h previously hardcoded
# text[capture.startByte]/text[capture.endByte-1] as ONE-BYTE delimiter
# characters throughout -- generalized to FormatCapture::openLength/
# closeLength (Mode.h/Mode.cpp) before this file was written, verified via
# ned_tests's own suite rather than assumed correct by inspection.
#
# The paired "<name>.open"/"<name>.close" mechanism (already proven for
# C#/Kotlin/for-loops on single-byte tokens) needs no query-authoring
# change at all for a multi-byte one -- Mode.cpp's own correlation reads
# each raw capture's OWN span for the delimiter length, whatever it is.

# brace.function: a function's own "end" is real and simple (function_
# declaration/function_definition both end with a bare "end" token, no
# "do" -- Lua has no opening keyword for a function body at all, verified
# live: `_function_body` is "parameters, body, end", nothing between the
# closing ")" and the body). The closing ")" of the parameter list stands
# in for the missing opener -- it's the one token that's ALWAYS there
# (even a zero-parameter function has "()"), and it's exactly where a
# same-line/next-line placement decision for the body would apply for any
# brace language. Covers every DECLARATION shape uniformly -- free
# functions, `local function`, and dot/colon "method" syntax
# (`function M.f()`/`function M:f()`) all parse as the SAME
# function_declaration node, differing only in their own "name:" field's
# node type (verified live) -- Lua draws no structural method/function
# split the way a class body would, so unlike every OOP language in this
# template there is no def.method captured below either (see that
# comment). function_definition (an anonymous function EXPRESSION, e.g.
# `local f = function() end` or an inline callback) is deliberately left
# uncaptured, matching every prior language's own brace.function scope
# (javascript/cpp/... never capture a lambda/function-expression body
# either) even though it has the identical parameters/end shape and WOULD
# match if named here -- caught live by this file's own test before
# shipping, not merely asserted.
(function_declaration
  parameters: (parameters ")" @brace.function.open)
  "end" @brace.function.close)

# brace.control: while/for's own "do"/"end" pair, and a standalone
# do_statement's own (a bare `do ... end` block with no header at all --
# real Lua style, used to scope a local variable). if_statement's own
# "then"/"end" pair spans the OUTER statement's closer even when
# elseif/else clauses sit in between (verified live against the real
# grammar -- elseif_statement/else_statement are separate "alternative:"
# fields on the SAME if_statement node, and there is only ever one literal
# "end" token, closing the whole chain, not one per branch). That's
# structurally sound for :placement (the gap being controlled is always
# the one right before "then", regardless of what follows) and for the
# NextLineIndented closer-repositioning step (the "end" being repositioned
# is genuinely the chain's own closer) -- but it means collapse-empty/
# collapse-simple correctly never fire on an if with elseif/else (the
# interior isn't "all whitespace" or "exactly one statement" once a
# elseif/else clause's own tokens are in there), which needs no special
# case since capture.isSimple/the isEmpty check already decline on their
# own. elseif/else branches themselves get NO capture of their own -- a
# real absence, not a scope cut: neither has a delimiter pair naming just
# its own body (an elseif's "then" opens it, but its closer is whatever
# comes next -- another elseif's "then", "else", or the outer "end" --
# never a token belonging to the elseif itself).
(while_statement "do" @brace.control.open "end" @brace.control.close)
(for_statement "do" @brace.control.open "end" @brace.control.close)
(do_statement "do" @brace.control.open "end" @brace.control.close)
(if_statement "then" @brace.control.open "end" @brace.control.close)

# control.parens: if/while's own condition is a plain "expression" field,
# not a required-parenthesized one -- Lua's idiomatic style omits parens
# ("if x then"), but the grammar still allows writing them, which parses
# as a real parenthesized_expression node (verified live: "if x then"
# produces zero matches, "if (x) then" produces exactly one -- the same
# narrow lever python/format.janet's and go/format.janet's own
# control.parens already are). for's own clause takes no general boolean
# expression at all (a numeric range or an "in" iterator list), so it has
# no analogous capture here, matching every other language's own
# "for-loops don't get control.parens unless the grammar genuinely wraps
# them" precedent.
(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)

# collapse-simple follow-up: NO ".simple" marker anywhere in this file --
# a real, documented scope cut, not an oversight, matching go/format.janet's
# own switch/select and csharp/format.janet's own catch precedent exactly.
# The marker mechanism (Mode.cpp's own correlation) requires a SECOND
# pattern capturing the exact same byte range as the base capture from a
# SINGLE node; every capture in this file is instead synthesized from a
# paired "<name>.open"/"<name>.close" match (no single node spans
# ")"..."end" or "then"..."end" -- the block sits strictly BETWEEN the two
# keyword tokens, never wrapped by anything that also includes them), and
# there is no single node to anchor a matching ".simple" pattern against
# either. Extending the marker convention to a paired form of its own is
# real future work, not attempted here.

# blank-lines-kind rollout follow-up: def.toplevel, same name every prior
# language's own file carries -- function_declaration only (function_
# definition is an anonymous function EXPRESSION, e.g. `local f =
# function() end`, never itself a statement-level definition, matching
# every other language's own "declarations, not expressions" scope). A
# `local function f() end` declaration is NOT a distinct node type --
# verified live it parses to the same function_declaration node, merely
# wrapped by its parent's own "local_declaration:" field (there is no
# real "local_function" type in the grammar at all; an earlier draft of
# this file named one and failed to compile, the same class of mistake
# typescript/format.janet's own field-name lesson already recorded) -- so
# a bare, unqualified (function_declaration) already covers both forms.
# Lua gets NO def.method at all, a real language difference rather than a
# scope cut -- see brace.function's own comment: a "method" here is just
# a function_declaration whose name field happens to be dot/colon-
# qualified, structurally identical to and interleaved with ordinary
# functions in the same block, never nested inside a distinct class-body
# container the way every def.method-carrying language's own methods are.
# Matches Go's own precedent exactly (a receiver method is a top-level
# declaration too, for the same structural reason). ".first" is wired at
# exactly the SAME level def.toplevel's own primary capture is (chunk,
# Lua's module-top-level node) -- deliberately NOT at every "block" a
# function could be nested inside (an earlier draft did this and was
# wrong: it made isFirst fire for a function that's merely the first
# statement of some ARBITRARY nested do/if/while body, a scope no other
# language's own .first convention ever covers either -- python/go/js all
# wire .first at the identical module/class-body level their def.toplevel/
# def.method itself uses, never at a generic nested block).
(chunk (function_declaration) @def.toplevel)
(chunk . (function_declaration) @def.toplevel.first)
