# configurable-formatter-rules follow-up: the thirteenth language, and a
# genuinely PARTIAL case rather than a full brace-carrying one -- verified
# live that `function_definition` is the ONLY brace-delimited construct
# in this grammar at all (all three syntactic forms -- "f() { }",
# "function g { }", "function h() { }" -- produce the identical
# `function_definition body: (compound_statement)` node), while
# if/while/for/case all use keyword delimiters instead (then/fi, do/done,
# in/esac), never braces, and a subshell uses "(...)" parens, not braces
# either. So this file names ONLY `brace.function`/`def.toplevel` -- no
# `brace.control`, `brace.class`, `control.parens`, or `def.method` exist
# here at all, a real language absence rather than a scope cut (the same
# kind go/format.janet's own missing `def.method` and c/format.janet's
# own missing `def.method` already document, just a different capture
# missing this time).
(function_definition body: (compound_statement) @brace.function)

# collapse-simple follow-up: compound_statement wraps its own commands
# DIRECTLY (verified live, no intermediate wrapper the way go's `block`
# has), so the "." anchor sits right against it the same as cpp/
# javascript/java/rust/c rather than go's one-level-deeper anchor.
(function_definition body: (compound_statement . (_) .) @brace.function.simple)

# blank-lines-kind follow-up: def.toplevel, the same name every prior
# language's file carries. A nested function definition ("f() { g() { }
# }") is deliberately NOT captured as its own def.toplevel/def.method --
# bash has no type/class concept for a "method" to belong to, and
# capturing arbitrary function nesting the way Python declines to for its
# own nested defs is the same precedent, not a new one.
(program (function_definition) @def.toplevel)
(program . (function_definition) @def.toplevel.first)
