#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Alcotest's test_case, and ppx_expect/ppx_inline_test's
#; named let%expect_test and let%test.

((application_expression
   function: (value_path (value_name) @_fn)
   .
   argument: (string) @test.name) @test.definition
 (:eq? @_fn "test_case"))

((value_definition
   (attribute_id) @_attribute
   (let_binding pattern: (string) @test.name)) @test.definition
 (:any-of? @_attribute "test" "expect_test" "test_unit" "test_module"))
