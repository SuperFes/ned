# ned-authored. A `let`'s bindings sit one level in (nixfmt); `in` and the body
# after it return to the `let`'s own level.
(let_expression (binding_set) @indent)
