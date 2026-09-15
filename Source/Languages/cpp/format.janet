# configurable-formatter-rules follow-up: the pilot Break-kind (kind 3)
# capture proving the format.janet -> Mode::formatCaptures -> Editor/
# FormatRules.h -> applied-edit chain end to end (Docs/FormattingRules.md).
# One capture only, deliberately: a function definition's own body, whose
# start byte IS the brace whose placement this names -- `function_definition`
# has a "body" field per tree-sitter-cpp's node-types.json, typed either
# compound_statement (the ordinary case) or try_statement (a function-try-
# block, `int f() try { ... } catch (...) { ... }`); only the ordinary case
# is captured here, matching this pilot's own deliberately narrow scope.
#
# Unconfigured (the default -- no built-in placement default is shipped
# yet), this capture is inert: Editor/FormatRules.h's BreakRuleFor returns
# every field unset, and Editor/FormatBracePlacement.h emits no edit for an
# unset placement. Configure one to see it do anything:
#   (ned/set-format-brace-placement "brace.function" "next-line")
(function_definition body: (compound_statement) @brace.function)

# capture-coverage-widening follow-up: every other brace-carrying construct
# JetBrains' own "Braces Layout" pane treats as its own placement row --
# control-flow bodies share one name (JetBrains groups "Other statements and
# blocks" the same way), type/namespace bodies get their own since a project
# very often styles those differently from a control-flow block. Each
# pattern's field-typed child (`(compound_statement)`, not a bare
# `(statement)`) is what keeps a braceless body ("if (x) return;") from
# ever matching at all -- there's no brace there to place.
(if_statement consequence: (compound_statement) @brace.control)
(while_statement body: (compound_statement) @brace.control)
(for_statement body: (compound_statement) @brace.control)
(switch_statement body: (compound_statement) @brace.control)
(catch_clause body: (compound_statement) @brace.control)
(class_specifier body: (field_declaration_list) @brace.class)
(struct_specifier body: (field_declaration_list) @brace.class)
(namespace_definition body: (declaration_list) @brace.namespace)

# Space-kind (kind 2) pilot capture, Editor/FormatSpacing.h: an if/while
# statement's own condition_clause -- its span is exactly the "(...)"
# (condition_clause's own first/last byte are the parens themselves per
# tree-sitter-cpp's node-types.json), which is what lets one capture name
# serve :before/:after/:within uniformly. Shared with any future language
# whose own format.janet names the same "control.parens" capture for its
# own if/while equivalent -- one rule, several grammars, per
# Docs/FormattingCapabilities.md's own "collapse across languages" stance.
(if_statement condition: (condition_clause) @control.parens)
(while_statement condition: (condition_clause) @control.parens)

# capture-coverage-widening follow-up: switch's condition is the same
# condition_clause shape as if/while's, and catch's own parameter_list is
# ALSO a single node spanning exactly "(...)" -- both qualify for the same
# capture name and the same "before/after/within" contract with no new
# C++ code at all (ComputeSpaceEdits reads the capture NAME, never the
# grammar node type it came from).
(switch_statement condition: (condition_clause) @control.parens)
(catch_clause parameters: (parameter_list) @control.parens)

# paired-delimiter-captures follow-up: a for-loop's own
# "(init; condition; update)" has no single node spanning the whole
# parenthesized clause the way if/while/switch's condition_clause does --
# initializer/condition/update are three independent, individually-optional
# fields with the "(" ")" themselves as bare anonymous tokens in between.
# Captured directly as a matched pair of single-token captures instead --
# Mode.cpp's formatCaptures closure correlates a "<name>.open"/"<name>.close"
# pair found in the SAME pattern match (never across two different
# for-loops -- verified live against tree-sitter-cpp before this landed)
# into one synthesized "control.parens" capture spanning open to close,
# indistinguishable from condition_clause's own whole-span capture to
# every consumer (Editor/FormatSpacing.h needs no changes for this).
(for_statement "(" @control.parens.open ")" @control.parens.close)

# collapse-simple follow-up: a second pattern per brace-carrying construct,
# anchored to "exactly one named child" via tree-sitter's "." (immediate-
# sibling) anchors -- verified live (Docs/FormattingRules.md) to answer
# "does this body have exactly one top-level statement" correctly
# regardless of what that one statement itself contains (a nested block,
# an if with its own block, ...), and to correctly report "not simple" for
# both an empty body and a multi-statement one. The marker is never
# emitted as a capture in its own right -- Mode.cpp's formatCaptures
# closure correlates it against the base "brace.*" capture sharing its
# exact byte range and discards the marker itself.
(function_definition body: (compound_statement . (_) .) @brace.function.simple)
(if_statement consequence: (compound_statement . (_) .) @brace.control.simple)
(while_statement body: (compound_statement . (_) .) @brace.control.simple)
(for_statement body: (compound_statement . (_) .) @brace.control.simple)
(switch_statement body: (compound_statement . (_) .) @brace.control.simple)
(catch_clause body: (compound_statement . (_) .) @brace.control.simple)

# blank-lines-kind rollout follow-up: def.toplevel/def.method, the same
# capture NAMES Python's own file pioneers -- one rule, several grammars,
# per Docs/FormattingCapabilities.md's "collapse across languages" stance.
# def.toplevel: a free function or a class/struct AT FILE SCOPE.
# `template_declaration` is captured too (not its own inner
# function_definition/class_specifier) -- verified live it wraps with no
# field name of its own the same way Python's own `decorated_definition`
# does, so a blank line lands above the "template<...>" line, not between
# it and what it templates.
(translation_unit [(function_definition) (class_specifier) (struct_specifier) (template_declaration)] @def.toplevel)
# def.method: an inline-bodied method inside a class/struct's own field
# list -- verified live `field_declaration_list` holds a real, complete
# `function_definition` node directly for an inline method (not merely a
# `field_declaration` naming an out-of-line one), the same node type
# def.toplevel's own function_definition already captures.
(field_declaration_list (function_definition) @def.method)

# ".first" markers: same convention as Python's own file -- true when
# nothing precedes this capture in its immediate container. A `#include`/
# `using`/preprocessor directive above the first real definition is a real
# preceding sibling (same lesson Python's own `import os` case already
# taught), so this correctly reports NOT first whenever one precedes --
# not a bug, the same honest behavior as every prior language's `.first`.
(translation_unit . [(function_definition) (class_specifier) (struct_specifier) (template_declaration)] @def.toplevel.first)
(field_declaration_list . (function_definition) @def.method.first)

# coverage-audit follow-up: constructs this rollout's own per-language
# passes skipped because they weren't the day's focus, not because the
# grammar lacks them -- found by a dedicated audit pass, each verified
# against grammar.json/node-types.json before landing here.
#
# do_statement's own body field is typed "statement" (grammar.json), the
# same abstract supertype if/while's own consequence/body fields already
# narrow to compound_statement -- do-while was simply never added.
(do_statement body: (compound_statement) @brace.control)
(do_statement body: (compound_statement . (_) .) @brace.control.simple)

# try_statement's OWN body (the "try { }" block itself, distinct from the
# already-captured catch_clause body) is a required compound_statement --
# confirmed via grammar.json, not just node-types.json's field list (the
# csharp/kotlin lesson). Function-try-blocks stay outside brace.function's
# own documented scope, untouched by this.
(try_statement body: (compound_statement) @brace.control)
(try_statement body: (compound_statement . (_) .) @brace.control.simple)

# union_specifier folds into brace.class -- c/format.janet already makes
# this "close enough" call for union+struct; cpp's own file never got the
# equivalent line ported over. Same field/type as struct_specifier's own
# capture above (verified via node-types.json).
(union_specifier body: (field_declaration_list) @brace.class)

# anon-function-policy-reversal follow-up: a lambda's own body is a
# REQUIRED compound_statement (grammar.json) -- always real braces, no
# expression-bodied alternative the way JS/C#/Rust's closures have, so no
# discriminator is needed. Previously excluded under this rollout's own
# "declarations, not expressions" scope cut; that policy is reversed as of
# this pass (see project memory) -- anonymous function bodies now get
# brace.function everywhere, matching whatever placement their language's
# declared functions already use.
(lambda_expression body: (compound_statement) @brace.function)
(lambda_expression body: (compound_statement . (_) .) @brace.function.simple)

# linkage_specification's own "body" field (grammar.json) is a CHOICE of
# function_definition/declaration/declaration_list -- only the last is the
# real brace-bodied `extern "C" { ... }` form; the other two are the
# single-declaration form with no braces at all. Folded into
# brace.namespace (same name/precedent namespace_definition already set) --
# structurally identical "keyword + brace-delimited declaration list"
# shape, verified live it's real, reachable syntax.
(linkage_specification body: (declaration_list) @brace.namespace)

# def.toplevel widened: union_specifier/enum_specifier were omitted here
# even though c/format.janet's own def.toplevel already names both -- a
# straight backport, no brace body to place for either (enum_specifier's
# own body is enumerator_list, not field_declaration_list -- blank-lines
# only, matching c/format.janet's own reasoning for the identical node).
(translation_unit [(union_specifier) (enum_specifier)] @def.toplevel)
(translation_unit . [(union_specifier) (enum_specifier)] @def.toplevel.first)
