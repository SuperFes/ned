# ned-authored. The imprint indents braced bodies, lists and argument lists.
# A binding's value written on the lines after `=` (or `<-`) sits a
# continuation step in. Operator chains are not continuations: `gleam
# format` lines a `|>` pipeline and a broken binary expression up under
# the first operand.
[(let) (let_assert) (use) (assert)] @indent.continuation
