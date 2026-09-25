#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). Test's named @testset blocks.

((macrocall_expression
   (macro_identifier (identifier) @_macro)
   (macro_argument_list . (string_literal) @test.name)) @test.definition
 (:eq? @_macro "testset"))
