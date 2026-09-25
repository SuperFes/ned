#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). FiveAM's test, Parachute's define-test and Rove's
#; deftest.

((list_lit
   .
   (sym_lit) @_head
   .
   (sym_lit) @test.name) @test.definition
 (:any-of? @_head "test" "define-test" "deftest" "fiveam:test" "parachute:define-test" "rove:deftest"))
