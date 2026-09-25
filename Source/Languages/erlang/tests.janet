#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). EUnit's *_test and *_test_ functions.

((fun_decl
   clause: (function_clause name: (atom) @test.name)) @test.definition
 (:match? @test.name "_test_?$"))
