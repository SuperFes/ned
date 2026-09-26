#; Local-binding query -- see c-locals.scm's header for the three rules every
#; locals query here follows.
#;
#; A statement is a scope, a subquery's included: a CTE name binds in its
#; WITH statement, and a table alias in the query that names it, where a
#; correlated subquery sees it too. Neither is visible outside its file.
#; Tables and aliases are different namespaces -- `FROM a b JOIN b c` joins
#; the table `b`, not the alias -- and a column's qualifier (`r.id`) may be
#; either, so it is read in both. Unquoted names are case-insensitive.

(statement) @local.scope
(subquery) @local.scope

((cte . (identifier) @local.definition.cte)
 (:set! local.file-private "true")
 (:set! local.namespace "relation")
 (:set! local.case-insensitive "true"))

((relation alias: (identifier) @local.definition.alias)
 (:set! local.file-private "true")
 (:set! local.namespace "alias")
 (:set! local.case-insensitive "true"))

((relation (object_reference !schema name: (identifier) @local.reference))
 (:set! local.namespace "relation")
 (:set! local.case-insensitive "true"))

((field (object_reference !schema name: (identifier) @local.reference))
 (:set! local.namespace "relation")
 (:set! local.case-insensitive "true"))
((field (object_reference !schema name: (identifier) @local.reference))
 (:set! local.namespace "alias")
 (:set! local.case-insensitive "true"))
