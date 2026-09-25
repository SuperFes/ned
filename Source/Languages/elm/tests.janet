#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). elm-test's describe and test.

((function_call_expr
   target: (value_expr name: (value_qid (lower_case_identifier) @_fn))
   .
   arg: (string_constant_expr) @test.name) @test.definition
 (:any-of? @_fn "describe" "test" "fuzz" "todo"))
