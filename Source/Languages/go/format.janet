# configurable-formatter-rules follow-up: the fourth brace-carrying
# language over cpp/javascript/java's own capture template, over
# tree-sitter-go's node types -- every shape verified live against the
# real grammar before this file was written, same discipline as every
# prior language.
#
# A function/method's own body -- optional in the grammar (a bodyless
# `func f() int` external/assembly declaration simply doesn't match).
(function_declaration body: (block) @brace.function)
(method_declaration body: (block) @brace.function)

# Every other brace-carrying statement shares "brace.control", matching
# every other language's own grouping. if/for's own body sits directly on
# a field the ordinary way; switch/select do NOT -- neither node names its
# own body with a field OR a wrapping node at all (verified live), so
# those two use the paired "{"/"}" token mechanism instead (the same one
# for-loop condition parens needed in cpp/javascript/java). Correlated by
# Mode.cpp into the identical FormatCapture shape either way -- a query
# author's choice, invisible to every consumer.
(if_statement consequence: (block) @brace.control)
(for_statement body: (block) @brace.control)
(expression_switch_statement "{" @brace.control.open "}" @brace.control.close)
(type_switch_statement "{" @brace.control.open "}" @brace.control.close)
(select_statement "{" @brace.control.open "}" @brace.control.close)

# brace.class: a struct's own field list. struct_type's OWN span starts at
# the "struct" keyword, not at "{" (verified live), so this reaches one
# level deeper into its nested field_declaration_list, the node that
# actually spans just the delimited body.
(type_spec type: (struct_type (field_declaration_list) @brace.class))

# brace.interface: kept a DISTINCT name from brace.class (an interface
# body is a different JetBrains-style construct, same precedent cpp's own
# brace.namespace set) -- interface_type has no nested wrapper node at all
# (unlike struct_type), so this is the paired mechanism again.
(interface_type "{" @brace.interface.open "}" @brace.interface.close)

# control.parens: if/switch's own condition/value is a plain expression
# field, not a required-parenthesized one -- Go's idiomatic style omits
# parens entirely ("if x {", "switch x {"), but the grammar still allows
# writing them, which parses as a real parenthesized_expression -- same
# narrow lever python/format.janet's own control.parens is, verified live
# it only matches when parens are actually present. There is no `while` in
# Go (`for` covers it) and a for-loop's own three-part clause has NO
# wrapping parens at all in this grammar -- writing them is a syntax
# error, not merely non-idiomatic, so no paired-parens capture exists for
# for-loops here (unlike cpp/javascript/java's own for-loops).
(if_statement condition: (parenthesized_expression) @control.parens)
(expression_switch_statement value: (parenthesized_expression) @control.parens)

# collapse-simple follow-up: same "<name>.simple" marker convention, one
# per statement-block construct above (brace.class/brace.interface
# excluded -- collapse-simple is a statement-block concept, not a type-body
# one, the same scope cpp/javascript/java's own files already draw).
# switch/select have no single node to anchor a "." pair against (no
# wrapping list node, same reason they needed the paired brace mechanism
# above), so they carry no .simple marker -- a real, documented scope cut,
# not an oversight. Go's own "block" is NOT anchored directly, unlike
# every other language's block-shaped node: a Go block wraps its
# statements in exactly one intermediate "statement_list" child (at most
# one -- an EMPTY block has none at all), so "." against block itself
# would see one child (the whole list) regardless of how many statements
# are inside it. A live probe caught this before it shipped: capturing
# "(block . (_) .)" reported isSimple for a TWO-statement body. Anchoring
# one level deeper, against statement_list's own children, while still
# capturing the outer block (so the marker's byte range matches the base
# capture's), is what makes this agree with every other language's
# behavior.
(function_declaration body: (block (statement_list . (_) .)) @brace.function.simple)
(method_declaration body: (block (statement_list . (_) .)) @brace.function.simple)
(if_statement consequence: (block (statement_list . (_) .)) @brace.control.simple)
(for_statement body: (block (statement_list . (_) .)) @brace.control.simple)
