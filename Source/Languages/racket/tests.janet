#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). rackunit's test-case and test-suite.

((list
   .
   (symbol) @_head
   .
   (string) @test.name) @test.definition
 (:any-of? @_head "test-case" "test-suite"))
