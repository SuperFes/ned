# smart-indentation follow-up. See c-indents.scm's own header comment for
# the general convention and for what the delimiter imprint contributes
# without a capture -- checked against tree-sitter-rust's own node-types.json
# directly (the same discipline rust-imports.scm's own header comment
# establishes), not assumed. "parameters"/"arguments" get @aligned,
# mirroring cpp-indents.scm's own choice for the identical reasoning (a
# wrapped parameter/argument list aligns under the opening paren rather than
# indenting one level).
#
# "token_tree" (a macro_rules!/macro-invocation body) is the case worth
# remembering. The old query deliberately left it out: it can be "()"/"[]"/
# "{}"-delimited depending on how the macro was invoked, and a single
# @dedent capture can only ever name one closing token. The imprint reads
# the brackets off each INSTANCE (Editor/ImprintBracket.h's DelimitersOf), so
# `write!(\n    f,` indents and `)` dedents whichever bracket was used -- a
# question a query could not ask, answered with nothing written here.
(parameters) @aligned
(arguments) @aligned

# lambda-body-alignment follow-up: "@align.barrier" marks a brace-delimited
# STATEMENT/DECLARATION body an enclosing @aligned container's column
# alignment must not reach through -- alignment is a continuation-line rule
# ("foo(a,\n    b)"), and a block-bodied callable passed as an argument
# ("thread::spawn(move || {") is not a continuation of the argument list at
# all. Contributes no indent level of its own (the imprint's container for
# the same node does that); it only degrades an OUTER @aligned container
# back to plain level counting. Deliberately never applied to a
# data literal -- a multi-line struct/array/collection-literal argument
# aligning its own body relative to the call's alignment column is existing,
# intentional behavior. See Editor/Indent.h.
(block) @align.barrier
(declaration_list) @align.barrier
(field_declaration_list) @align.barrier
(enum_variant_list) @align.barrier
(match_block) @align.barrier

