#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). gleeunit's public *_test functions.

((function
   (visibility_modifier)
   name: (identifier) @test.name) @test.definition
 (:match? @test.name "_test$"))
