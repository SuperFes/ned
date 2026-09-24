#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). ExUnit's `test "name" do` (optionally taking a context
#; pattern) and `describe "name" do`.

((call
   target: (identifier) @_macro
   (arguments
     .
     (string) @test.name)
   (do_block)) @test.definition
 (:any-of? @_macro "test" "describe"))
