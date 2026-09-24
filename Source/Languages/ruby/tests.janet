#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). RSpec (and minitest/spec) blocks named by their first
#; argument -- a string, or the class under test -- and minitest's test_*
#; methods inside a *Test class.

((call
   method: (identifier) @_block
   arguments: (argument_list
     .
     [(string) (constant) (scope_resolution)] @test.name)
   block: [(do_block) (block)]) @test.definition
 (:any-of? @_block "describe" "context" "it" "specify" "example" "feature" "scenario"))

((method
   name: (identifier) @test.name) @test.definition
 (:match? @test.name "^test_"))

((class
   name: (constant) @test.name) @test.definition
 (:match? @test.name "Test$"))
