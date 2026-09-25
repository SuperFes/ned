#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). SRFI-64: a suite or group always names itself first; a test
#; case's leading name is optional and told apart only by its argument count
#; (test-assert 2, test-eqv/equal/eq 3, test-approximate 4, test-error 3).

((list . (symbol) @_head . (string) @test.name) @test.definition
 (:any-of? @_head "test-begin" "test-group" "test-group-with-cleanup"))

((list . (symbol) @_head . (string) @test.name . (_) .) @test.definition
 (:eq? @_head "test-assert"))

((list . (symbol) @_head . (string) @test.name . (_) . (_) .) @test.definition
 (:any-of? @_head "test-eqv" "test-equal" "test-eq" "test-error"))

((list . (symbol) @_head . (string) @test.name . (_) . (_) . (_) .) @test.definition
 (:eq? @_head "test-approximate"))
