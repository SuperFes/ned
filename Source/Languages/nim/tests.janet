#; Test-discovery query, ned-authored (see cpp/tests.janet for the capture
#; convention). std/unittest's suite and test blocks.

((call
   function: (identifier) @_fn
   (argument_list . (interpreted_string_literal) @test.name (statement_list))) @test.definition
 (:any-of? @_fn "suite" "test"))
