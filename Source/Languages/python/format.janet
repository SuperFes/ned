# configurable-formatter-rules follow-up: Python is the edge case flagged
# back in the cpp/javascript/java rollouts -- verified live against
# tree-sitter-python's own node-types.json (not assumed) that its grammar
# has NO brace-delimited bodies anywhere: function_definition, if_statement,
# while_statement, for_statement, class_definition, try_statement's
# except/finally clauses, with_statement and match_statement's case_clause
# ALL use a bare "block" field with no wrapping delimiter tokens at all --
# indentation alone marks a body's extent. `brace.function`/`brace.control`/
# `brace.class`, the paired .open/.close mechanism, and `:collapse-empty`/
# `:collapse-simple` all fundamentally do not apply: ComputeBracePlacementEdits
# (FormatBracePlacement.cpp) hardcodes `text[capture.startByte]`/
# `text[capture.endByte - 1]` as single literal delimiter characters to
# reposition or splice -- there is no such character for a Python "block",
# so no capture of that shape belongs in this file at all.
#
# The one construct that DOES fit the existing template: if/while's own
# condition is an "expression" field, not a required parenthesized one --
# but the grammar still allows a user to write one anyway ("if (x):"), which
# parses as a real parenthesized_expression node. Verified live with a
# throwaway QueryMatcher probe that this only matches when parens are
# actually present in the source ("if x:" produces zero matches, "if (x):"
# produces exactly one) -- so :space rules on this capture are a real,
# narrow lever ("if a project's style guide permits/forbids redundant
# condition parens, keep the spacing inside them consistent") rather than
# a no-op. match_statement's own subject and case_clause's guard have the
# exact same "expression, not required-parenthesized" shape and are left
# uncaptured for now -- a match subject is routinely a tuple already
# wrapped for unrelated reasons, muddying what "redundant" even means there.
(if_statement condition: (parenthesized_expression) @control.parens)
(while_statement condition: (parenthesized_expression) @control.parens)

# blank-lines-kind pilot (kind 6): Python's own real formatting need, per
# PEP8 -- "surround top-level function/class definitions with two blank
# lines" and "methods inside a class are surrounded by a single blank
# line". def.toplevel/def.method both capture the OUTERMOST node (a
# decorated_definition when the def/class carries a decorator, so a blank
# line lands above the decorator, not between it and the header it
# decorates -- verified live a decorated def/class still matches this
# alternation at the module/class-body level, since decorated_definition's
# own "definition:" field is the ONLY place the inner function_definition/
# class_definition node appears in the tree).
(module [(function_definition) (class_definition) (decorated_definition)] @def.toplevel)
(class_definition
  body: (block [(function_definition) (decorated_definition)] @def.method))

# ".first" marker (Mode.cpp's own correlation convention, alongside
# ".simple"): true when nothing precedes this capture in its immediate
# container -- the ONE case a minimum-blank-lines-before rule must not
# force a blank line into (there is nothing above it to separate from but
# the container's own opening line: right after "class C:", or the very
# start of the module). Verified live this correctly does NOT fire after a
# class's own leading docstring (a real preceding sibling, not an extra),
# matching the conservative "declines rather than guesses" precedent
# collapse-simple already set.
(module . [(function_definition) (class_definition) (decorated_definition)] @def.toplevel.first)
(class_definition
  body: (block . [(function_definition) (decorated_definition)] @def.method.first))
