# smart-indentation follow-up. See c-indents.scm's own header comment for the
# general convention and for what the delimiter imprint contributes without a
# capture -- checked against tree-sitter/tree-sitter-go's own
# grammar.js/node-types.json directly, not assumed. Worth knowing: the
# imprint also covers import_spec_list and var_spec_list (`import (\n\t"fmt"`),
# which the hand-written query never had -- measured over the grammar's own
# examples, every such line was flat under the query and indented in the
# file. "parameter_list" is Go's own node for a function/method's parameters
# AND its multi-value return type AND a method's receiver -- all three get
# @aligned for the same reason c-indents.scm's own parameter_list/
# argument_list do (a wrapped continuation line lines up under the first
# entry's own column, falling back to a plain indent when the opener is
# alone on its line).
(parameter_list) @aligned
(argument_list) @aligned

# lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
# STATEMENT/DECLARATION body an enclosing @aligned container's column
# alignment must not reach through -- alignment is a continuation-line rule
# ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
# ("go doStuff(func() {") is not a continuation of the argument list at
# all. Contributes no indent level of its own (the imprint's container for
# the same node does that); it only degrades an OUTER @aligned container
# back to plain level counting. Deliberately never applied to a
# data literal -- a multi-line initializer/object/array argument aligning
# its own body relative to the call's alignment column is existing,
# intentional behavior. See Editor/Indent.h.
(block) @align.barrier
(field_declaration_list) @align.barrier
(interface_type) @align.barrier
(expression_switch_statement) @align.barrier
(type_switch_statement) @align.barrier
(select_statement) @align.barrier

# ROADMAP.md watch-list entry: case/default clause headers were indenting
# one level too deep. `expression_case`/`default_case`/`type_case`/
# `communication_case` carry no delimiters of their own (confirmed via
# node-types.json -- each is just an optional trailing `statement_list`),
# so the imprint contributes nothing for them and every line starting
# inside the switch/select body's own `{`/`}` -- including the case labels
# themselves -- got the same single level the imprint's bracket container
# already assigns. gofmt (and every other Go formatter) aligns a case
# label back to its own switch/select, one level shallower than its own
# body -- the same C-like-but-not shape Python's elif/except/finally and
# bash's elif_clause/else_clause already have their own @dedent for.
(expression_case) @dedent
(default_case) @dedent
(type_case) @dedent
(communication_case) @dedent

