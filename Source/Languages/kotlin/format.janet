# configurable-formatter-rules follow-up: the eleventh language, and a
# genuine outlier -- tree-sitter-kotlin (fwcd's grammar) declares ZERO
# fields anywhere in the whole grammar (confirmed against node-types.json,
# not assumed), so every capture here is a bare node-type match rather
# than a field-qualified one.
#
# A real, new-to-this-rollout hazard: `function_body` is the SAME node
# type for both a real "{ ... }" block AND Kotlin's own brace-less
# single-expression function body ("fun f(x: Int) = x + 1" parses to
# `(function_body (additive_expression ...))`, no distinguishing wrapper
# at all) -- verified live. A bare `(function_body) @brace.function`
# capture would sometimes hand ComputeBracePlacementEdits a span with no
# literal brace in it whatsoever, the same severity class as PHP's
# switch_block hazard. Fixed with this codebase's `:match?` predicate
# (Editor/Grammar/QueryPredicates.cpp, already used elsewhere e.g.
# rust/tests.janet's own `#[test]` detection) checking the captured span
# itself starts with a literal "{" -- verified live: zero captures for an
# expression body, correct exact-brace spans for both an empty and a real
# block. This is the first TEXT predicate this whole rollout has needed
# for a format capture; every prior discrimination problem (Go's ASI,
# PHP's 3-way body, C#'s catch span) was solved with pure structure --
# Kotlin's grammar offers no structural handle to use instead.
(function_declaration (function_body) @brace.function (:match? @brace.function "^\\{"))

# brace.control: if/while/for/when-entry's own body is
# `control_structure_body`, with the IDENTICAL brace-vs-bare-expression
# ambiguity function_body has ("if (x) 1 else 2" wraps each branch in a
# bare `control_structure_body (integer_literal)`, no braces) -- verified
# live, same fix. `if_expression` has NO field distinguishing its "then"
# branch's control_structure_body from its "else" branch's (unlike every
# prior language, which all exclude the else branch via field selection)
# -- a real, grammar-forced deviation from that precedent, not an
# oversight: this pattern captures BOTH branches uniformly, since there
# is no structural handle to single one out.
(if_expression (control_structure_body) @brace.control (:match? @brace.control "^\\{"))
(while_statement (control_structure_body) @brace.control (:match? @brace.control "^\\{"))
(for_statement (control_structure_body) @brace.control (:match? @brace.control "^\\{"))
(when_entry (control_structure_body) @brace.control (:match? @brace.control "^\\{"))
# catch_block's own span covers "catch (Type name) { ... }" as ONE node
# (the same shape try_expression's own "statements" child confirms this
# grammar keeps a construct's header and body inside one flat node, not a
# distinct body-wrapper) -- its own braces are anonymous tokens, captured
# with the SAME paired mechanism control.parens already needs below,
# just for "{" "}" instead of "(" ")".
(catch_block "{" @brace.control.open "}" @brace.control.close)

# brace.class: class_declaration and object_declaration both wrap a
# plain `class_body`, itself unambiguous (Kotlin has no brace-less class
# body form) -- no predicate needed. There is NO separate
# `interface_declaration` node type in this grammar at all (confirmed
# against node-types.json) -- a Kotlin `interface` parses as the exact
# same `class_declaration` node a `class` does, discriminated only by an
# anonymous "interface" keyword token with no field naming it, so there
# is no `brace.interface` capture in this file: an interface's own body
# is captured as `brace.class`, a real grammar limitation rather than an
# oversight. `companion_object` wraps its own `class_body` the identical
# way, so it gets the same treatment.
(class_declaration (class_body) @brace.class)
(object_declaration (class_body) @brace.class)
(companion_object (class_body) @brace.class)

# control.parens: if/while/for's own condition, and when's own subject,
# have no field OR wrapping node spanning "(...)" (every condition is a
# bare, unwrapped expression sitting directly among the construct's other
# children) -- confirmed live, needing the SAME paired mechanism a
# for-loop's own clause uses in every other language, here for even the
# simple if/while case. A catch clause's own "(name: Type)" is likewise
# unwrapped.
(if_expression "(" @control.parens.open ")" @control.parens.close)
(while_statement "(" @control.parens.open ")" @control.parens.close)
(for_statement "(" @control.parens.open ")" @control.parens.close)
(catch_block "(" @control.parens.open ")" @control.parens.close)
# when's own subject is DIFFERENT from if/while/for/catch's own condition:
# `when_subject` is a real named node whose OWN rule (grammar.json, not
# node-types.json's field summary -- the same lesson C#'s catch_declaration
# taught) literally opens with "(" and closes with ")", so its span
# already covers the whole clause -- a bare paired-token pattern on
# when_expression itself does NOT match at all (verified live: 0 captures,
# not a false one, before this was caught and fixed).
(when_expression (when_subject) @control.parens)

# collapse-simple follow-up: `.simple` markers reuse the SAME "wraps a
# `statements` node with exactly one child" structural fact the base
# captures' own `:match?` predicate exists to protect -- a pattern
# requiring a literal `statements` child already structurally excludes
# both the brace-less expression-body case (no `statements` child at
# all) and the truly-empty-braces case (no `statements` child either), so
# neither needs its own `:match?` repeated here. catch_block carries NO
# `.simple` marker at all -- it uses the paired "{"/"}"-token mechanism
# above rather than a real node capture, the same reason go's own
# switch/select and php's own switch_block carry none either (there is
# no single node whose own span matches the synthesized capture's to
# anchor a "." pair against).
(function_declaration (function_body (statements . (_) .)) @brace.function.simple)
(if_expression (control_structure_body (statements . (_) .)) @brace.control.simple)
(while_statement (control_structure_body (statements . (_) .)) @brace.control.simple)
(for_statement (control_structure_body (statements . (_) .)) @brace.control.simple)
(when_entry (control_structure_body (statements . (_) .)) @brace.control.simple)

# blank-lines-kind follow-up: def.toplevel/def.method, the same names
# every prior language's file carries. `data class`/`enum class` are
# still plain `class_declaration` nodes (their own `modifiers` child adds
# "data"/keeps "class", neither changes the node TYPE), so they're
# already covered with no extra pattern.
(source_file [(function_declaration) (class_declaration) (object_declaration)] @def.toplevel)
# def.method: class_body is shared by class/interface/object/companion
# bodies alike (see brace.class's own comment above), so one pattern
# covers a method nested in any of them, including a companion object's
# own methods (nested two levels deep, still just `class_body
# (function_declaration)` at the point that matters).
(class_body (function_declaration) @def.method)

(source_file . [(function_declaration) (class_declaration) (object_declaration)] @def.toplevel.first)
(class_body . (function_declaration) @def.method.first)
