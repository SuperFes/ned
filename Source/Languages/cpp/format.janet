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
