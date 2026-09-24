# ned-authored, fnlfmt's layout. A call's arguments line up under its first
# argument (under the head when none shares its line); the forms fnlfmt
# treats as bodies sit two columns past their own paren.
[(fn) (lambda) (let) (each) (for) (match) (collect) (icollect) (accumulate)] @indent.body
(list
  .
  (symbol) @_head
  (:any-of? @_head
    "when" "do" "while" "macro" "case" "doto" "with-open" "eval-compiler" "faccumulate" "fcollect")) @indent.body

[(list) (global) (local) (var) (set)] @aligned.args
[(sequential_table) (table) (let_clause) (parameters) (for_clause) (sequential_table_binding)
 (quoted_sequential_table) (quoted_table)] @aligned
