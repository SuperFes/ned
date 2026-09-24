#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). ScalaTest's FunSuite/FunSpec and munit: `test("name") { }`,
#; `describe`/`it`; FlatSpec: `"Subject" should "behave" in { }` (and
#; `it should ...`), named by the behaviour.

((call_expression
   function: (call_expression
     function: (identifier) @_fn
     arguments: (arguments
       .
       (string) @test.name))
   arguments: (block)) @test.definition
 (:any-of? @_fn "test" "describe" "it"))

((infix_expression
   left: (infix_expression
     operator: (identifier) @_verb
     right: (string) @test.name)
   operator: (identifier) @_in
   right: (block)) @test.definition
 (:any-of? @_verb "should" "must" "can")
 (:eq? @_in "in"))
