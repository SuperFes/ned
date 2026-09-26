#; Test-discovery query (see cpp/tests.janet for the capture convention).
#; GUT and gdUnit4 both collect a test script's `test*`-named functions.

((function_definition
   (name) @test.name
   (:match? @test.name "^test")) @test.definition)
