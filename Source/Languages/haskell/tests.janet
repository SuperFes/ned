#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). hspec's describe/context/it/specify and tasty's
#; testGroup/testCase/testProperty, named by their string argument --
#; applied with `$` (the definition is the whole infix, body included) or
#; directly.

((infix
   left_operand: (apply
     function: (variable) @_fn
     argument: (literal
       (string) @test.name))) @test.definition
 (:any-of? @_fn "describe" "context" "it" "specify" "prop" "testGroup" "testCase" "testProperty"))

((apply
   function: (apply
     function: (variable) @_fn
     argument: (literal
       (string) @test.name))) @test.definition
 (:any-of? @_fn "describe" "context" "it" "specify" "prop" "testGroup" "testCase" "testProperty"))
