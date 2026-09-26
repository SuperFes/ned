#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; A `pkl:test` module's `facts { ["name"] { ... } }` and
#; `examples { ["name"] { ... } }` entries.

((module
   (classProperty
     (identifier) @_block
     (objectBody (objectEntry . (slStringLiteralExpr) @test.name) @test.definition)))
 (:any-of? @_block "facts" "examples"))
