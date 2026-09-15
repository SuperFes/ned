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
