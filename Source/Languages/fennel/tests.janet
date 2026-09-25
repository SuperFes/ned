#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). fennel-test's `(deftest name ...)`, busted's describe/it
#; family, and faith's test functions -- faith runs the `test`-prefixed keys
#; of the table a module returns, which are its top-level `test-*` functions
#; in practice.

((list
   .
   (symbol) @_head
   .
   (symbol) @test.name) @test.definition
 (:eq? @_head "deftest"))

((list
   .
   (symbol) @_head
   .
   (string) @test.name) @test.definition
 (:any-of? @_head "describe" "context" "insulate" "expose" "it" "spec" "test" "pending"))

((program
   (fn name: (symbol) @test.name) @test.definition)
 (:match? @test.name "^test"))
