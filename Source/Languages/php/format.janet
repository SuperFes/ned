# configurable-formatter-rules follow-up: the sixth language, and the
# first with a genuine THREE-way ambiguity for a single construct rather
# than the two-way ones every prior language had. PHP's if/elseif/else/
# while/for/foreach bodies are ALL typed to accept any of three different
# shapes: `{ ... }` (compound_statement, what this template captures),
# `: ... elseif (y): ... else: ... endif;`-style alternate syntax
# (colon_block, a real, still-common PHP construct with NO braces
# anywhere in the whole chain), or a single bare unbraced statement (like
# C's `if (x) return;`). Naming the field with `(compound_statement)` as
# the required TYPE is what discriminates all three live, verified for
# every construct below before this file was written -- including the
# FULL colon-alternate chain (`if (x): a(); elseif (y): b(); else: c();
# endif;`), not just a bare `if:...endif:` -- a colon-syntax or
# bare-statement body simply produces NO match anywhere in the chain, not
# a false or corrupted one.
#
# A function/method's own body (compound_statement is the only shape a
# function/method ever takes -- unlike if/while, PHP has no colon-syntax
# function body). Optional on method_declaration (an abstract/interface
# method has none).
(function_definition body: (compound_statement) @brace.function)
(method_declaration body: (compound_statement) @brace.function)

# brace.control: every other brace-carrying statement, one shared name
# matching every other language's own grouping. switch is the one
# construct here needing extra care beyond the type-discrimination trick
# above: PHP's grammar uses a SINGLE node type ("switch_block") for BOTH
# its brace form ("{ case ... }") and its own colon-alternate form
# (": case ... endswitch;") -- there is no second node type to discriminate
# by, so `(switch_block) @brace.control` would sometimes capture a node
# starting with "{" and sometimes one starting with ":", and this engine's
# ComputeBracePlacementEdits hardcodes that starting byte as a literal
# BRACE character to reposition or splice. Confirmed live this is a real
# hazard, not a hypothetical, before working around it: requiring the
# literal "{"/"}" tokens as a PAIRED capture (the same for-loop-condition
# mechanism every prior language already uses) only ever matches the brace
# form -- verified live that the colon form produces zero matches, not a
# false one.
(if_statement body: (compound_statement) @brace.control)
(while_statement body: (compound_statement) @brace.control)
(for_statement body: (compound_statement) @brace.control)
(foreach_statement body: (compound_statement) @brace.control)
(switch_block "{" @brace.control.open "}" @brace.control.close)
(catch_clause body: (compound_statement) @brace.control)
# try/finally/do bodies: the same brace.control grouping, missed when this
# file was first written (the catch_clause line above was standing in for the
# whole try statement). Each is the same `body: (compound_statement)` type
# discrimination every other pattern here uses, so the colon-alternate and
# bare-statement shapes stay unmatched exactly as they do above.
(try_statement body: (compound_statement) @brace.control)
(finally_clause body: (compound_statement) @brace.control)
(do_statement body: (compound_statement) @brace.control)

# elseif/else follow-up: `else_if_clause`/`else_clause` carry the EXACT
# same brace/colon-alternate/bare-statement three-way body shape
# `if_statement` itself has (verified live -- an "if (x): ... elseif (y):
# ... else: ... endif;" chain produces ZERO brace.control matches across
# every clause, not just the leading "if"), so the same
# `body: (compound_statement)` type-discrimination trick applies
# unchanged. Sharing "brace.control" with plain if/while/for rather than
# a distinct name -- an else-branch's own body is not a semantically
# different construct from an if's, just a different clause of the same
# statement, the same reasoning that already folds while/for/foreach
# under one name.
(else_if_clause body: (compound_statement) @brace.control)
(else_clause body: (compound_statement) @brace.control)

# brace.class: class_declaration AND trait_declaration share this name --
# a PHP trait is structurally and stylistically a reusable class body, the
# same "close enough to fold together" call cpp's brace.class already
# makes for both structs and classes. brace.interface is kept its OWN
# name, the same precedent Go's own brace.interface set (a distinct
# JetBrains-style construct, even though the underlying node shape --
# declaration_list -- is identical).
(class_declaration body: (declaration_list) @brace.class)
(trait_declaration body: (declaration_list) @brace.class)
(interface_declaration body: (declaration_list) @brace.interface)

# control.parens: unlike Python/Go's own narrow, rarely-matched lever,
# if/while/switch's own condition field is REQUIRED to be a
# parenthesized_expression in PHP's grammar (mandatory parens, matching
# cpp/javascript/java's own shape) -- verified live this fires on ordinary
# code, not just a redundant-parens edge case. A for-loop's own
# "(init; condition; update)" has no single spanning node here either
# (three independent, individually optional fields around bare anonymous
# tokens, the same shape every other language's for-loop has), so it uses
# the paired mechanism. A catch clause's own parameter is likewise a bare
# "(" ")" pair around its type/variable fields with no wrapping node --
# the same shape javascript/go's own catch has, unlike cpp's named
# "parameters" field.
(if_statement condition: (parenthesized_expression) @control.parens)
(else_if_clause condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)
(switch_statement condition: (parenthesized_expression) @control.parens)
(for_statement "(" @control.parens.open ")" @control.parens.close)
(catch_clause "(" @control.parens.open ")" @control.parens.close)

# collapse-simple follow-up: same "<name>.simple" marker convention, one
# per statement-block construct above (brace.class/brace.interface
# excluded, the same statement-block-only scope every prior language's
# file draws). switch has no single node to anchor a "." pair against (no
# wrapping list node, the same reason it needed the paired brace mechanism
# above), so it carries no .simple marker here either -- a real,
# documented scope cut, not an oversight, the same one Go's own
# switch/select draw.
(function_definition body: (compound_statement . (_) .) @brace.function.simple)
(method_declaration body: (compound_statement . (_) .) @brace.function.simple)
(if_statement body: (compound_statement . (_) .) @brace.control.simple)
(else_if_clause body: (compound_statement . (_) .) @brace.control.simple)
(else_clause body: (compound_statement . (_) .) @brace.control.simple)
(while_statement body: (compound_statement . (_) .) @brace.control.simple)
(for_statement body: (compound_statement . (_) .) @brace.control.simple)
(foreach_statement body: (compound_statement . (_) .) @brace.control.simple)
(catch_clause body: (compound_statement . (_) .) @brace.control.simple)

# blank-lines-kind rollout follow-up: def.toplevel/def.method, the same
# names every prior language's file carries. `namespace_definition` is
# deliberately NOT included (matching cpp's own namespace_definition
# exclusion from def.toplevel) -- a namespace is a container, not a
# definition, the same distinction drawn there.
(program [(function_definition) (class_declaration) (trait_declaration) (interface_declaration)
          (enum_declaration)] @def.toplevel)
# def.method: class_declaration/trait_declaration/interface_declaration
# all share the SAME body node type (declaration_list) -- confirmed
# against a live parse, the same "one node type, several owners" shape
# already load-bearing for this file's own brace.class/brace.interface
# captures above -- so one pattern covers a method in any of the three.
(declaration_list (method_declaration) @def.method)

(program . [(function_definition) (class_declaration) (trait_declaration) (interface_declaration)
            (enum_declaration)] @def.toplevel.first)
(declaration_list . (method_declaration) @def.method.first)

# coverage-audit follow-up: match_expression's own body field
# (match_block) has NO colon-alternate form the way switch_block does --
# confirmed via node-types.json, a single required shape -- so it's safe
# to capture directly with no paired-token workaround.
(match_expression body: (match_block) @brace.control)

# enum_declaration's own body field (enum_declaration_list -- a DIFFERENT
# node type from class/trait/interface's shared declaration_list, so it
# needs its own line even though it shares the capture NAME) was only
# ever named for def.toplevel -- never given a brace.class capture,
# inconsistent with class/trait/interface above.
(enum_declaration body: (enum_declaration_list) @brace.class)

# anon-function-policy-reversal follow-up (see project memory): a real
# prior gap regardless of the policy question -- closures
# (`function() { }`, extremely common/idiomatic in PHP) have a REQUIRED
# compound_statement body, no colon-alternate/bare-statement ambiguity
# the way if/while have (unlike a declared function, a closure has no
# colon-syntax form at all) -- a clean, unambiguous addition. Anonymous
# classes (`new class { }`) share the same declaration_list body every
# named class already does, folded into brace.class the same way.
(anonymous_function body: (compound_statement) @brace.function)
(anonymous_function body: (compound_statement . (_) .) @brace.function.simple)
(anonymous_class body: (declaration_list) @brace.class)

# rewrite-kind widening (kind 9): a second rewrite family, structurally
# unlike rewrite.quote's own pure delimiter swap -- a keyword-token
# replacement. Confirmed against tree-sitter-php's own node-types.json
# that "elseif" is its own distinct anonymous token (not "else"+"if"
# glued together), so a bare literal-token capture on else_if_clause's own
# leading keyword names it directly, independent of which of the three
# body shapes (brace/colon-alternate/bare-statement) this particular
# clause happens to use -- the keyword itself precedes the body field
# entirely, so brace.control's own three-way body-shape concern (this
# file's own header comment) never enters into it. Editor/FormatRewrite.cpp
# rewrites the captured token to "else if" whenever
# ned/set-format-rewrite-expand-elseif is on for this capture name.
# Unconfigured, inert:
#   (ned/set-format-rewrite-expand-elseif "rewrite.elseif" true)
(else_if_clause "elseif" @rewrite.elseif)

# keyword-break follow-up: the continuation KEYWORDS themselves, as their own
# captures -- the one thing brace placement structurally cannot reach. A body
# capture's span starts at its opening brace, so no rule about it can say
# anything about a keyword standing outside and before it, which is exactly
# where Allman's own `else` needs to move:
#
#   }              }
#   } else   ->    else
#   {              {
#
# Naming the bare token, not the clause: Editor/FormatBreak.h's :before/:after
# rewrite the whitespace immediately outside a capture's own span, so the span
# has to BE the keyword.
#
# ONE shared name across every continuation keyword, not one name each --
# "break before a control keyword" is a single stylistic decision in every
# style guide that has an opinion about it, and this is the same grouping
# `brace.control` above already makes for the bodies of the same statements
# (matching JetBrains' own "Other statements and blocks"). A project that
# genuinely needs `else` and `catch` to differ can still scope by language;
# splitting the name per keyword is the change to make if a real case turns
# up, not before.
#
# PHP's `elseif` is one token (the alternate `else if` spelling parses as an
# else_clause wrapping an if_statement, whose `else` the first pattern
# catches like any other).
# Unconfigured, inert:
#   (ned/set-format-break-before "break.control" true)
(else_clause "else" @break.control)
(else_if_clause "elseif" @break.control)
(catch_clause "catch" @break.control)
(finally_clause "finally" @break.control)
(do_statement "while" @break.control)
