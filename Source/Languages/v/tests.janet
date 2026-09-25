#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). V's test_* functions.

((function_declaration
   name: (identifier) @test.name) @test.definition
 (:match? @test.name "^test_"))
