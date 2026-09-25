#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Foundry's test*, testFuzz* and invariant* functions.

((function_definition
   name: (identifier) @test.name) @test.definition
 (:match? @test.name "^(test|invariant)"))
