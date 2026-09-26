#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; rescript-test's and the Jest bindings' `test("name", fn)` / `it` /
#; `describe`, with their async variants.

((call_expression
   (value_identifier) @_fn
   (arguments . (string) @test.name)
   (:any-of? @_fn "test" "testAsync" "it" "itAsync" "describe" "describeAsync")) @test.definition)
