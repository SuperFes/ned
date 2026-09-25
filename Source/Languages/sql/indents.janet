# ned-authored. The imprint indents parenthesized subqueries, lists and column
# definitions. A clause keeps its keyword at the statement's level and indents
# whatever continues it onto later lines: a select list, a join's ON, a
# WHERE's further predicates. `from` isn't captured -- the clauses after it
# (JOIN, WHERE, GROUP BY, ...) are its children and stay at its level.
[(select) (where) (join) (group_by) (order_by) (returning) (values)] @indent.headed
(case (keyword_end) @dedent) @indent.headed

# INSERT's VALUES rows and UPDATE's SET assignments have no node of their own:
# they continue from the line after the keyword up to the next clause.
(insert (keyword_values) @indent.begin) @indent
(insert (keyword_values) @indent.begin (keyword_on) @indent.end) @indent
(update (keyword_set) @indent.begin) @indent
(update (keyword_set) @indent.begin [(where) (from)] @indent.end) @indent
