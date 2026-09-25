# ned-authored. A `let`'s bindings sit one level in (nixfmt); `in` and the body
# after it return to the `let`'s own level.
(let_expression (binding_set) @indent)

# nixfmt: a binding's value on the lines after `=` sits a level in, and so
# do an if's branches, with `then`/`else` at the if's own level when they
# start a line.
[(binding) (binary_expression) (apply_expression)] @indent.continuation
(if_expression) @indent.headed
(if_expression ["then" "else"] @dedent)
