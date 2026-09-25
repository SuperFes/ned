#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Test::More and Test2's named subtests.

((ambiguous_function_call_expression
   function: (function) @_fn
   arguments: (list_expression . [(interpolated_string_literal) (string_literal)] @test.name)) @test.definition
 (:eq? @_fn "subtest"))

((function_call_expression
   function: (function) @_fn
   arguments: (list_expression . [(interpolated_string_literal) (string_literal)] @test.name)) @test.definition
 (:eq? @_fn "subtest"))
